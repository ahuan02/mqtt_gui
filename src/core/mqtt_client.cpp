#include "core/mqtt_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <process.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <utility>

namespace mqtt {

namespace {

uint64_t now_ms() { return ::GetTickCount64(); }

// 轻量互斥：SRWLOCK 比 std::mutex 更快，且不会引入 libwinpthread 依赖
struct SrwGuard {
    explicit SrwGuard(SRWLOCK& lock) : lock_(lock) { ::AcquireSRWLockExclusive(&lock_); }
    ~SrwGuard() { ::ReleaseSRWLockExclusive(&lock_); }
    SrwGuard(const SrwGuard&) = delete;
    SrwGuard& operator=(const SrwGuard&) = delete;

    SRWLOCK& lock_;
};

constexpr size_t   kMaxPacketBytes   = 32u << 20;
constexpr uint32_t kConnectTimeoutMs = 8000;

std::string socket_error_text(int code) {
    char* buffer = nullptr;
    const DWORD length = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

    std::string text = (length && buffer) ? std::string(buffer, length)
                                          : ("错误码 " + std::to_string(code));
    if (buffer) ::LocalFree(buffer);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.'))
        text.pop_back();
    return text;
}

const char* connack_reason(uint8_t code) {
    switch (code) {
        case 1: return "协议版本不被接受";
        case 2: return "客户端标识被拒绝";
        case 3: return "服务端不可用";
        case 4: return "用户名或密码错误";
        case 5: return "未授权";
        default: return "未知原因";
    }
}

void put_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void put_string(std::vector<uint8_t>& out, std::string_view text) {
    const size_t length = std::min<size_t>(text.size(), 0xFFFF);
    put_u16(out, static_cast<uint16_t>(length));
    out.insert(out.end(), text.data(), text.data() + length);
}

void put_varint(std::vector<uint8_t>& out, size_t value) {
    do {
        uint8_t digit = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        if (value) digit |= 0x80;
        out.push_back(digit);
    } while (value);
}

std::vector<uint8_t> make_packet(uint8_t header, std::vector<uint8_t> body) {
    std::vector<uint8_t> packet;
    packet.reserve(body.size() + 5);
    packet.push_back(header);
    put_varint(packet, body.size());
    packet.insert(packet.end(), body.begin(), body.end());
    return packet;
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

}  // namespace

// ---------------------------------------------------------------------------
// 实现
// ---------------------------------------------------------------------------
struct Client::Impl {
    explicit Impl(std::atomic<State>& shared_state) : state(shared_state) {}
    ~Impl() {
        if (wake_event) ::CloseHandle(wake_event);
        if (net_event) ::CloseHandle(net_event);
    }

    std::atomic<State>& state;
    HANDLE              thread{nullptr};
    std::atomic<bool>   quit{false};
    Options             pending_options;

    HANDLE wake_event{nullptr};   // 发送队列唤醒（手动重置）
    HANDLE net_event{nullptr};    // socket 网络事件
    SOCKET sock{INVALID_SOCKET};

    // 跨线程队列
    SRWLOCK                           tx_lock = SRWLOCK_INIT;
    std::deque<std::vector<uint8_t>>  tx_queue;
    SRWLOCK                           rx_lock = SRWLOCK_INIT;
    std::vector<Message>              rx_queue;
    std::vector<std::string>          rx_events;

    std::atomic<uint16_t> next_packet_id{1};
    std::atomic<uint64_t> stat_rx_packets{0};
    std::atomic<uint64_t> stat_tx_packets{0};
    std::atomic<uint64_t> stat_rx_bytes{0};
    std::atomic<uint64_t> stat_tx_bytes{0};
    std::atomic<uint64_t> stat_rx_messages{0};

    // 仅工作线程访问
    std::vector<uint8_t>         out_buf;
    size_t                       out_offset{0};
    std::vector<uint8_t>         in_buf;
    size_t                       in_offset{0};
    uint64_t                     last_tx{0};
    uint64_t                     connect_start{0};
    uint16_t                     keep_alive{60};
    int                          pings_outstanding{0};
    bool                         dead{false};
    std::string                  dead_reason;

    // -- 事件 ---------------------------------------------------------------
    void emit(std::string text) {
        SrwGuard guard(rx_lock);
        rx_events.push_back(std::move(text));
    }

    void fail(std::string reason) {
        if (dead) return;
        dead = true;
        dead_reason = std::move(reason);
    }

    // -- socket -------------------------------------------------------------
    bool open_socket(const Options& options, std::string& error);
    bool flush();
    void on_readable();
    void parse();
    void on_packet(uint8_t header, const uint8_t* body, size_t length);
    void on_timer();
    void pull_tx();

    bool connected() const { return state.load(std::memory_order_acquire) == State::Connected; }

    void enqueue(std::vector<uint8_t> packet) {
        {
            SrwGuard guard(tx_lock);
            tx_queue.push_back(std::move(packet));
        }
        if (wake_event) ::SetEvent(wake_event);
    }

    bool send_connect(const Options& options);
    bool publish(std::string_view topic, std::string_view payload, uint8_t qos, bool retain);
    bool subscribe(std::string_view filter, uint8_t qos);
    bool unsubscribe(std::string_view filter);

    void run(Options options);

    static unsigned __stdcall thread_entry(void* context) {
        auto* self = static_cast<Impl*>(context);
        self->run(self->pending_options);
        return 0;
    }
};

bool Client::Impl::open_socket(const Options& options, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_text[8];
    std::snprintf(port_text, sizeof port_text, "%u", static_cast<unsigned>(options.port));

    addrinfo* result = nullptr;
    if (::getaddrinfo(options.host.c_str(), port_text, &hints, &result) != 0) {
        error = "域名解析失败: " + options.host;
        return false;
    }

    int last_error = 0;
    SOCKET candidate = INVALID_SOCKET;

    for (addrinfo* entry = result; entry; entry = entry->ai_next) {
        candidate = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (candidate == INVALID_SOCKET) {
            last_error = ::WSAGetLastError();
            continue;
        }

        u_long nonblocking = 1;
        ::ioctlsocket(candidate, FIONBIO, &nonblocking);

        if (::connect(candidate, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) == SOCKET_ERROR) {
            const int code = ::WSAGetLastError();
            if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
                last_error = code;
                ::closesocket(candidate);
                candidate = INVALID_SOCKET;
                continue;
            }

            fd_set writable, excepted;
            FD_ZERO(&writable);
            FD_SET(candidate, &writable);
            FD_ZERO(&excepted);
            FD_SET(candidate, &excepted);
            timeval timeout{6, 0};

            const int ready = ::select(0, nullptr, &writable, &excepted, &timeout);
            if (ready <= 0 || FD_ISSET(candidate, &excepted)) {
                last_error = (ready == 0) ? WSAETIMEDOUT : ::WSAGetLastError();
                ::closesocket(candidate);
                candidate = INVALID_SOCKET;
                continue;
            }

            int socket_error = 0;
            int length = static_cast<int>(sizeof socket_error);
            ::getsockopt(candidate, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&socket_error), &length);
            if (socket_error != 0) {
                last_error = socket_error;
                ::closesocket(candidate);
                candidate = INVALID_SOCKET;
                continue;
            }
        }
        break;
    }

    ::freeaddrinfo(result);

    if (candidate == INVALID_SOCKET) {
        error = "无法连接 " + options.host + ":" + port_text + " (" + socket_error_text(last_error) + ")";
        return false;
    }

    sock = candidate;

    BOOL enabled = TRUE;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof enabled);
    ::setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&enabled), sizeof enabled);

    if (::WSAEventSelect(sock, net_event, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR) {
        error = "注册网络事件失败 (" + socket_error_text(::WSAGetLastError()) + ")";
        ::closesocket(sock);
        sock = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool Client::Impl::flush() {
    while (out_offset < out_buf.size()) {
        const int sent = ::send(sock,
                                reinterpret_cast<const char*>(out_buf.data() + out_offset),
                                static_cast<int>(out_buf.size() - out_offset), 0);
        if (sent > 0) {
            out_offset += static_cast<size_t>(sent);
            stat_tx_bytes.fetch_add(static_cast<uint64_t>(sent), std::memory_order_relaxed);
            last_tx = now_ms();
            continue;
        }

        const int code = ::WSAGetLastError();
        if (code == WSAEWOULDBLOCK) return true;   // 等 FD_WRITE 再续

        fail("发送失败: " + socket_error_text(code));
        return false;
    }
    out_buf.clear();
    out_offset = 0;
    return true;
}

void Client::Impl::on_readable() {
    uint8_t chunk[16384];
    for (;;) {
        const int received = ::recv(sock, reinterpret_cast<char*>(chunk), sizeof chunk, 0);
        if (received > 0) {
            in_buf.insert(in_buf.end(), chunk, chunk + received);
            stat_rx_bytes.fetch_add(static_cast<uint64_t>(received), std::memory_order_relaxed);
            continue;
        }
        if (received == 0) {
            fail("对端已关闭连接");
            return;
        }
        const int code = ::WSAGetLastError();
        if (code == WSAEWOULDBLOCK) break;

        fail("接收失败: " + socket_error_text(code));
        return;
    }

    parse();

    if (in_offset == in_buf.size()) {
        in_buf.clear();
        in_offset = 0;
    } else if (in_offset >= 65536) {
        in_buf.erase(in_buf.begin(), in_buf.begin() + static_cast<ptrdiff_t>(in_offset));
        in_offset = 0;
    }
}

void Client::Impl::parse() {
    for (;;) {
        const size_t available = in_buf.size() - in_offset;
        if (available < 2) return;

        const uint8_t* start = in_buf.data() + in_offset;
        size_t remaining = 0;
        size_t multiplier = 1;
        size_t header_length = 1;
        bool   complete = false;

        for (; header_length < available && header_length <= 4; ++header_length) {
            const uint8_t digit = start[header_length];
            remaining += static_cast<size_t>(digit & 0x7F) * multiplier;
            multiplier <<= 7;
            if ((digit & 0x80) == 0) {
                complete = true;
                ++header_length;
                break;
            }
        }

        if (!complete) {
            if (available > 5) fail("报文剩余长度字段非法");
            return;
        }
        if (remaining > kMaxPacketBytes) {
            fail("报文超过 32MB 上限");
            return;
        }
        if (available < header_length + remaining) return;

        on_packet(start[0], start + header_length, remaining);
        in_offset += header_length + remaining;
        if (dead) return;
    }
}

void Client::Impl::on_packet(uint8_t header, const uint8_t* body, size_t length) {
    stat_rx_packets.fetch_add(1, std::memory_order_relaxed);
    pings_outstanding = 0;

    switch (header >> 4) {
        case 2: {  // CONNACK
            if (length < 2) return;
            const uint8_t code = body[1];
            if (code == 0) {
                state.store(State::Connected, std::memory_order_release);
                last_tx = now_ms();
                emit("已连接到 broker");
            } else {
                fail(std::string("连接被拒绝: ") + connack_reason(code));
            }
            return;
        }
        case 3: {  // PUBLISH
            if (length < 2) return;
            const uint8_t flags = header & 0x0F;
            const uint8_t qos = static_cast<uint8_t>((flags >> 1) & 0x03);
            const bool retain = (flags & 0x01) != 0;
            const bool duplicate = (flags & 0x08) != 0;

            const size_t topic_length = read_u16(body);
            if (2 + topic_length > length) return;

            size_t offset = 2 + topic_length;
            uint16_t packet_id = 0;
            if (qos > 0) {
                if (offset + 2 > length) return;
                packet_id = read_u16(body + offset);
                offset += 2;
            }

            Message message;
            message.topic.assign(reinterpret_cast<const char*>(body + 2), topic_length);
            message.payload.assign(reinterpret_cast<const char*>(body + offset), length - offset);
            message.qos = qos;
            message.retain = retain;
            message.duplicate = duplicate;
            message.packet_id = packet_id;

            {
                SrwGuard guard(rx_lock);
                rx_queue.push_back(std::move(message));
            }
            stat_rx_messages.fetch_add(1, std::memory_order_relaxed);

            if (qos == 1) {
                std::vector<uint8_t> ack;
                put_u16(ack, packet_id);
                out_buf.insert(out_buf.end(), {0x40, 0x02});
                out_buf.insert(out_buf.end(), ack.begin(), ack.end());
                flush();
            } else if (qos == 2) {
                std::vector<uint8_t> ack;
                put_u16(ack, packet_id);
                out_buf.insert(out_buf.end(), {0x50, 0x02});
                out_buf.insert(out_buf.end(), ack.begin(), ack.end());
                flush();
            }
            return;
        }
        case 5:    // PUBREC -> PUBREL
        case 6: {  // PUBREL -> PUBCOMP
            if (length < 2) return;
            const uint8_t response = (header >> 4) == 5 ? 0x62 : 0x70;
            const uint16_t packet_id = read_u16(body);
            out_buf.push_back(response);
            out_buf.push_back(0x02);
            put_u16(out_buf, packet_id);
            flush();
            return;
        }
        case 9:  // SUBACK
            if (length >= 3) {
                const uint8_t granted = body[2];
                if (granted == 0x80) emit("订阅被拒绝");
                else emit("订阅已确认 (QoS " + std::to_string(granted) + ")");
            }
            return;
        case 11:  // UNSUBACK
            emit("已取消订阅");
            return;
        case 13:  // PINGRESP
            return;
        case 14:  // DISCONNECT
            fail("broker 主动断开连接");
            return;
        default:
            return;
    }
}

void Client::Impl::on_timer() {
    const uint64_t now = now_ms();

    if (state.load(std::memory_order_acquire) == State::Connecting) {
        if (now - connect_start > kConnectTimeoutMs) fail("连接超时");
        return;
    }
    if (!connected()) return;

    if (now - last_tx >= static_cast<uint64_t>(keep_alive) * 1000u) {
        if (pings_outstanding >= 2) {
            fail("心跳超时");
            return;
        }
        out_buf.push_back(0xC0);   // PINGREQ
        out_buf.push_back(0x00);
        stat_tx_packets.fetch_add(1, std::memory_order_relaxed);
        ++pings_outstanding;
        last_tx = now;
        flush();
    }
}

void Client::Impl::pull_tx() {
    std::deque<std::vector<uint8_t>> pending;
    {
        SrwGuard guard(tx_lock);
        pending.swap(tx_queue);
    }
    for (const auto& packet : pending) out_buf.insert(out_buf.end(), packet.begin(), packet.end());
}

bool Client::Impl::send_connect(const Options& options) {
    std::vector<uint8_t> body;
    body.reserve(64 + options.client_id.size() + options.username.size() + options.password.size());

    put_string(body, "MQTT");
    body.push_back(0x04);  // 3.1.1

    uint8_t flags = 0;
    if (options.clean_session) flags |= 0x02;
    if (!options.username.empty()) flags |= 0x80;
    if (!options.password.empty() && !options.username.empty()) flags |= 0x40;
    body.push_back(flags);
    put_u16(body, keep_alive);

    put_string(body, options.client_id);
    if (!options.username.empty()) put_string(body, options.username);
    if (!options.password.empty() && !options.username.empty()) put_string(body, options.password);

    auto packet = make_packet(0x10, std::move(body));
    out_buf.insert(out_buf.end(), packet.begin(), packet.end());
    stat_tx_packets.fetch_add(1, std::memory_order_relaxed);
    return flush();
}

bool Client::Impl::publish(std::string_view topic, std::string_view payload, uint8_t qos, bool retain) {
    if (!connected()) return false;

    std::vector<uint8_t> body;
    body.reserve(topic.size() + payload.size() + 8);
    put_string(body, topic);

    uint8_t header = 0x30;
    if (retain) header |= 0x01;
    if (qos > 0) {
        header |= static_cast<uint8_t>(qos << 1);
        put_u16(body, next_packet_id.fetch_add(1, std::memory_order_relaxed));
    }

    body.insert(body.end(), payload.begin(), payload.end());
    enqueue(make_packet(header, std::move(body)));
    return true;
}

bool Client::Impl::subscribe(std::string_view filter, uint8_t qos) {
    if (!connected()) return false;

    std::vector<uint8_t> body;
    put_u16(body, next_packet_id.fetch_add(1, std::memory_order_relaxed));
    put_string(body, filter);
    body.push_back(qos > 2 ? 2 : qos);

    enqueue(make_packet(0x82, std::move(body)));
    return true;
}

bool Client::Impl::unsubscribe(std::string_view filter) {
    if (!connected()) return false;

    std::vector<uint8_t> body;
    put_u16(body, next_packet_id.fetch_add(1, std::memory_order_relaxed));
    put_string(body, filter);

    enqueue(make_packet(0xA2, std::move(body)));
    return true;
}

void Client::Impl::run(Options options) {
    keep_alive = options.keep_alive ? options.keep_alive : 60;

    WSADATA wsa{};
    if (::WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        emit("网络初始化失败");
        state.store(State::Disconnected, std::memory_order_release);
        return;
    }

    std::string error;
    if (!open_socket(options, error)) {
        emit(error);
        state.store(State::Disconnected, std::memory_order_release);
        ::WSACleanup();
        return;
    }

    emit("正在连接 " + options.host + ":" + std::to_string(options.port) + " ...");
    send_connect(options);
    connect_start = now_ms();
    last_tx = connect_start;

    while (!quit.load(std::memory_order_relaxed) && !dead) {
        DWORD timeout = 500;
        if (state.load(std::memory_order_relaxed) == State::Connecting) {
            timeout = 200;
        } else if (connected()) {
            const long long idle = static_cast<long long>(now_ms() - last_tx);
            const long long remain = static_cast<long long>(keep_alive) * 1000 - idle;
            timeout = static_cast<DWORD>(std::clamp<long long>(remain, 10, 500));
        }

        HANDLE events[2] = {wake_event, net_event};
        const DWORD result = ::WSAWaitForMultipleEvents(2, events, FALSE, timeout, FALSE);

        if (result == WSA_WAIT_FAILED) {
            fail("等待网络事件失败");
            break;
        }
        if (result == WSA_WAIT_TIMEOUT) {
            on_timer();
            continue;
        }

        const DWORD index = result - WSA_WAIT_EVENT_0;
        if (index == 0) {
            ::ResetEvent(wake_event);
            pull_tx();
            flush();
        } else {
            WSANETWORKEVENTS network_events{};
            if (::WSAEnumNetworkEvents(sock, net_event, &network_events) == SOCKET_ERROR) {
                fail("读取网络事件失败");
                break;
            }
            if (network_events.lNetworkEvents & FD_READ) on_readable();
            if (network_events.lNetworkEvents & FD_WRITE) flush();
            if (network_events.lNetworkEvents & FD_CLOSE) {
                if (!dead) fail("连接已断开");
            }
        }
        if (!dead) on_timer();
    }

    // 主动断开时尽量把 DISCONNECT 发出去
    if (quit.load(std::memory_order_relaxed) && !dead && connected()) {
        out_buf.push_back(0xE0);
        out_buf.push_back(0x00);
        flush();
    }

    if (sock != INVALID_SOCKET) {
        ::WSAEventSelect(sock, nullptr, 0);
        ::closesocket(sock);
        sock = INVALID_SOCKET;
    }

    state.store(State::Disconnected, std::memory_order_release);
    emit(dead ? dead_reason : "已断开连接");

    out_buf.clear();
    out_offset = 0;
    in_buf.clear();
    in_offset = 0;
    ::WSACleanup();
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------
Client::Client() : impl_(std::make_unique<Impl>(state_)) {
    impl_->wake_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->net_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

Client::~Client() { disconnect(); }

bool Client::connect(const Options& options) {
    if (state_.load(std::memory_order_acquire) != State::Disconnected) return false;
    if (impl_->thread) {
        ::WaitForSingleObject(impl_->thread, INFINITE);
        ::CloseHandle(impl_->thread);
        impl_->thread = nullptr;
    }

    impl_->quit.store(false, std::memory_order_relaxed);
    impl_->dead = false;
    impl_->dead_reason.clear();
    impl_->pings_outstanding = 0;
    {
        SrwGuard guard(impl_->tx_lock);
        impl_->tx_queue.clear();
    }
    {
        SrwGuard guard(impl_->rx_lock);
        impl_->rx_queue.clear();
        impl_->rx_events.clear();
    }

    state_.store(State::Connecting, std::memory_order_release);
    impl_->pending_options = options;

    const uintptr_t handle = _beginthreadex(nullptr, 0, &Client::Impl::thread_entry,
                                            impl_.get(), 0, nullptr);
    if (handle == 0) {
        state_.store(State::Disconnected, std::memory_order_release);
        return false;
    }
    impl_->thread = reinterpret_cast<HANDLE>(handle);
    return true;
}

void Client::disconnect() {
    if (!impl_->thread) {
        state_.store(State::Disconnected, std::memory_order_release);
        return;
    }
    impl_->quit.store(true, std::memory_order_relaxed);
    if (impl_->wake_event) ::SetEvent(impl_->wake_event);
    ::WaitForSingleObject(impl_->thread, INFINITE);
    ::CloseHandle(impl_->thread);
    impl_->thread = nullptr;
    state_.store(State::Disconnected, std::memory_order_release);
}

bool Client::subscribe(std::string_view filter, uint8_t qos) {
    return impl_->subscribe(filter, qos);
}

bool Client::unsubscribe(std::string_view filter) {
    return impl_->unsubscribe(filter);
}

bool Client::publish(std::string_view topic, std::string_view payload, uint8_t qos, bool retain) {
    return impl_->publish(topic, payload, qos, retain);
}

void Client::drain_messages(std::vector<Message>& out) {
    SrwGuard guard(impl_->rx_lock);
    if (impl_->rx_queue.empty()) return;
    out.insert(out.end(),
               std::make_move_iterator(impl_->rx_queue.begin()),
               std::make_move_iterator(impl_->rx_queue.end()));
    impl_->rx_queue.clear();
}

void Client::drain_events(std::vector<std::string>& out) {
    SrwGuard guard(impl_->rx_lock);
    if (impl_->rx_events.empty()) return;
    out.insert(out.end(),
               std::make_move_iterator(impl_->rx_events.begin()),
               std::make_move_iterator(impl_->rx_events.end()));
    impl_->rx_events.clear();
}

Stats Client::stats() const {
    Stats result;
    result.rx_packets = impl_->stat_rx_packets.load(std::memory_order_relaxed);
    result.tx_packets = impl_->stat_tx_packets.load(std::memory_order_relaxed);
    result.rx_bytes = impl_->stat_rx_bytes.load(std::memory_order_relaxed);
    result.tx_bytes = impl_->stat_tx_bytes.load(std::memory_order_relaxed);
    result.rx_messages = impl_->stat_rx_messages.load(std::memory_order_relaxed);
    return result;
}
}
