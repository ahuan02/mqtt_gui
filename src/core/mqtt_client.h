#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mqtt {

enum class State : int {
    Disconnected = 0,
    Connecting,
    Connected
};

struct Options {
    std::string host{"127.0.0.1"};
    uint16_t    port{1883};
    std::string client_id;
    std::string username;
    std::string password;
    uint16_t    keep_alive{60};
    bool        clean_session{true};
};

struct Message {
    std::string topic;
    std::string payload;
    uint32_t    packet_id{0};
    uint8_t     qos{0};
    bool        retain{false};
    bool        duplicate{false};
};

struct Stats {
    uint64_t rx_packets{0};
    uint64_t tx_packets{0};
    uint64_t rx_bytes{0};
    uint64_t tx_bytes{0};
    uint64_t rx_messages{0};
};

// 精简 MQTT 3.1.1 客户端：单个后台线程 + 非阻塞 socket + 事件驱动。
// 协议覆盖 CONNECT / CONNACK / PUBLISH(QoS 0..2) / ACK 族 / SUBSCRIBE /
// UNSUBSCRIBE / PING / DISCONNECT，足够调试面板使用。
class Client {
public:
    Client();
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    bool connect(const Options& options);   // 异步，立即返回
    void disconnect();                      // 阻塞等待线程退出

    State state() const noexcept { return state_.load(std::memory_order_acquire); }
    bool connected() const noexcept { return state() == State::Connected; }
    bool busy() const noexcept { return state() == State::Connecting; }

    bool subscribe(std::string_view filter, uint8_t qos);
    bool unsubscribe(std::string_view filter);
    bool publish(std::string_view topic, std::string_view payload, uint8_t qos, bool retain);

    // 由 UI 线程调用：把后台收到的消息/事件批量取走（加锁一次，开销恒定）
    void drain_messages(std::vector<Message>& out);
    void drain_events(std::vector<std::string>& out);

    Stats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<State>    state_{State::Disconnected};
};
}
