#include "app/app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr const char* kIniFile = "mqtt_gui.ini";

double now_seconds() {
    static const ULONGLONG origin = ::GetTickCount64();
    return static_cast<double>(::GetTickCount64() - origin) / 1000.0;
}

std::string clock_now() {
    SYSTEMTIME local{};
    ::GetLocalTime(&local);

    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d.%03d",
                  local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
    return buffer;
}

std::string make_client_id() {
    static unsigned counter = 0;
    const unsigned long long seed = ::GetTickCount64() + (++counter) * 7919ull;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "mqtt-gui-%04X",
                  static_cast<unsigned>(seed ^ (seed >> 16)) & 0xFFFFu);
    return buffer;
}

bool looks_like_error(const std::string& text) {
    static const char* kWords[] = {"失败", "拒绝", "超时", "断开", "无法", "错误", "上限"};
    for (const char* word : kWords) {
        if (text.find(word) != std::string::npos) return true;
    }
    return false;
}

void copy_into(char* target, size_t size, const std::string& source) {
    std::snprintf(target, size, "%s", source.c_str());
}

// 把输入框内容写回配置（输入框是唯一可编辑来源）
void sync_from_editors(App& app) {
    app.host = app.edit_host;
    const int port = std::atoi(app.edit_port);
    app.port = (port > 0 && port <= 65535) ? port : 1883;
    app.client_id = app.edit_client_id;
    app.username = app.edit_user;
    app.password = app.edit_pass;
}

}  // namespace

App::App() {
    client_id = make_client_id();
}

App::~App() {
    shutdown();
}

void App::init() {
    load_config();

    copy_into(edit_host, sizeof edit_host, host);
    std::snprintf(edit_port, sizeof edit_port, "%d", port);
    copy_into(edit_client_id, sizeof edit_client_id, client_id);
    copy_into(edit_user, sizeof edit_user, username);
    copy_into(edit_pass, sizeof edit_pass, password);
    if (edit_pub_topic[0] == '\0') std::snprintf(edit_pub_topic, sizeof edit_pub_topic, "test/topic");
}

void App::shutdown() {
    client.disconnect();
    sync_from_editors(*this);
    // 退出时总是落盘：窗口位置、过滤条件这类状态平时不标脏
    save_config();
}

mqtt::Options App::build_options() const {
    mqtt::Options options;
    options.host = host;
    options.port = static_cast<uint16_t>(std::clamp(port, 1, 65535));
    options.client_id = client_id.empty() ? make_client_id() : client_id;
    options.username = username;
    options.password = password;
    options.keep_alive = static_cast<uint16_t>(std::clamp(keep_alive, 5, 3600));
    options.clean_session = clean_session;
    return options;
}

void App::set_hint(std::string text, bool error) {
    hint = std::move(text);
    hint_error = error;
    hint_time = now_seconds();
}

void App::connect_now() {
    sync_from_editors(*this);
    config_dirty_ = true;

    want_connected_ = true;
    reconnect_at_ = 0.0;
    if (!client.connect(build_options())) {
        set_hint("当前状态无法发起连接", true);
    } else {
        set_hint("正在连接 " + host + ":" + std::to_string(port), false);
    }
}

void App::disconnect_now() {
    want_connected_ = false;
    client.disconnect();
    set_hint("已断开连接", false);
}

void App::subscribe_topic(const std::string& filter, int qos) {
    if (filter.empty()) return;

    const auto existing = std::find_if(subscriptions.begin(), subscriptions.end(),
                                       [&](const Subscription& sub) { return sub.filter == filter; });
    if (existing == subscriptions.end()) {
        subscriptions.push_back({filter, std::clamp(qos, 0, 2)});
    } else {
        existing->qos = std::clamp(qos, 0, 2);
    }
    config_dirty_ = true;

    if (client.connected()) {
        client.subscribe(filter, static_cast<uint8_t>(std::clamp(qos, 0, 2)));
        set_hint("已发送订阅: " + filter, false);
    } else {
        set_hint("已加入订阅列表，连接后自动订阅", false);
    }
}

void App::unsubscribe_topic(const std::string& filter) {
    subscriptions.erase(std::remove_if(subscriptions.begin(), subscriptions.end(),
                                       [&](const Subscription& sub) { return sub.filter == filter; }),
                        subscriptions.end());
    config_dirty_ = true;

    if (client.connected()) client.unsubscribe(filter);
}

// 节点图是订阅的唯一来源：这里做增量同步，避免每次都全量重订
void App::sync_subscriptions(const std::vector<Subscription>& wanted) {
    for (auto it = subscriptions.begin(); it != subscriptions.end();) {
        const bool still_wanted =
            std::any_of(wanted.begin(), wanted.end(),
                        [&](const Subscription& sub) { return sub.filter == it->filter; });
        if (still_wanted) {
            ++it;
            continue;
        }
        if (client.connected()) client.unsubscribe(it->filter);
        it = subscriptions.erase(it);
        config_dirty_ = true;
    }

    for (const auto& sub : wanted) {
        if (sub.filter.empty()) continue;
        const auto found = std::find_if(subscriptions.begin(), subscriptions.end(),
                                        [&](const Subscription& item) { return item.filter == sub.filter; });
        if (found == subscriptions.end()) {
            subscriptions.push_back(sub);
            if (client.connected()) client.subscribe(sub.filter, static_cast<uint8_t>(sub.qos));
            config_dirty_ = true;
        } else if (found->qos != sub.qos) {
            found->qos = sub.qos;
            if (client.connected()) client.subscribe(sub.filter, static_cast<uint8_t>(sub.qos));
            config_dirty_ = true;
        }
    }
}

void App::publish_message(const std::string& topic, const std::string& payload, int qos, bool retain) {
    if (topic.empty()) {
        set_hint("主题不能为空", true);
        return;
    }
    if (!client.connected()) {
        set_hint("尚未连接 broker，无法发布", true);
        return;
    }

    if (!client.publish(topic, payload, static_cast<uint8_t>(std::clamp(qos, 0, 2)), retain)) {
        set_hint("发布失败", true);
        return;
    }

    mqtt::Message message;
    message.topic = topic;
    message.payload = payload;
    message.qos = static_cast<uint8_t>(std::clamp(qos, 0, 2));
    message.retain = retain;
    make_entry(std::move(message), true);
}

void App::clear_messages() {
    store.clear();
    selected_seq = 0;
}

void App::make_entry(mqtt::Message&& message, bool outbound) {
    MessageEntry entry;
    entry.seq = next_seq_++;
    entry.clock = clock_now();
    entry.topic = std::move(message.topic);
    entry.size = static_cast<uint32_t>(message.payload.size());
    entry.qos = message.qos;
    entry.retain = message.retain;
    entry.packet_id = static_cast<uint16_t>(message.packet_id);
    entry.outbound = outbound;

    if (message.payload.size() > MessageStore::kMaxPayload) {
        message.payload.resize(MessageStore::kMaxPayload);
        entry.truncated = true;
    }

    entry.preview = MessageStore::make_preview(message.payload);
    entry.payload = MessageStore::sanitize(message.payload);
    store.push(std::move(entry));
}

void App::tick() {
    incoming_.clear();
    client.drain_messages(incoming_);
    for (auto& message : incoming_) make_entry(std::move(message), false);

    events_.clear();
    client.drain_events(events_);
    for (const auto& text : events_) {
        const bool error = looks_like_error(text);
        set_hint(text, error);
        event_log.push_back({clock_now(), text, error});
        if (event_log.size() > kEventLogCap) event_log.pop_front();
    }

    const mqtt::State state = client.state();
    if (state != last_state_) {
        if (state == mqtt::State::Connected) {
            for (const auto& sub : subscriptions) {
                client.subscribe(sub.filter, static_cast<uint8_t>(std::clamp(sub.qos, 0, 2)));
            }
        } else if (state == mqtt::State::Disconnected) {
            reconnect_at_ = now_seconds() + 3.0;
        }
        last_state_ = state;
    }

    if (want_connected_ && auto_reconnect && state == mqtt::State::Disconnected &&
        now_seconds() >= reconnect_at_ && reconnect_at_ > 0.0) {
        if (!client.connect(build_options())) reconnect_at_ = now_seconds() + 3.0;
    }
}

void App::load_config() {
    std::string content;
    if (!read_whole_file(app_path(kIniFile), content)) return;

    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();

        std::string line = content.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);

        if (key == "host") host = value;
        else if (key == "port") port = std::atoi(value.c_str());
        else if (key == "client_id") client_id = value;
        else if (key == "username") username = value;
        else if (key == "keep_alive") keep_alive = std::atoi(value.c_str());
        else if (key == "clean_session") clean_session = value != "0";
        else if (key == "auto_reconnect") auto_reconnect = value != "0";
        else if (key == "password") password = unescape_field(value);
        else if (key == "pub_qos") edit_pub_qos = std::atoi(value.c_str());
        else if (key == "pub_retain") edit_pub_retain = value != "0";
        else if (key == "pub_topic") {
            std::snprintf(edit_pub_topic, sizeof edit_pub_topic, "%s", unescape_field(value).c_str());
        } else if (key == "pub_payload") {
            std::snprintf(edit_pub_payload, sizeof edit_pub_payload, "%s", unescape_field(value).c_str());
        } else if (key == "filter") {
            std::snprintf(filter_text, sizeof filter_text, "%s", unescape_field(value).c_str());
        } else if (key == "direction") {
            direction_filter = std::clamp(std::atoi(value.c_str()), 0, 2);
        } else if (key == "auto_scroll") {
            auto_scroll = value != "0";
        } else if (key == "font") {
            font_file = value;
        } else if (key == "font_size") {
            font_size = std::clamp(static_cast<float>(std::atof(value.c_str())), 12.0f, 28.0f);
        } else if (key == "window") {
            int maximized = 0;
            if (std::sscanf(value.c_str(), "%d,%d,%d,%d,%d", &window_x, &window_y, &window_w,
                            &window_h, &maximized) == 5) {
                window_maximized = maximized != 0;
                if (window_w < 640 || window_h < 480) {
                    window_w = 1440;
                    window_h = 900;
                    window_x = -1;
                    window_y = -1;
                }
            }
        }
    }
}

void App::save_config() {
    std::string content;
    content += "# MQTT 调试面板配置\n";
    content += "host=" + host + "\n";
    content += "port=" + std::to_string(port) + "\n";
    content += "client_id=" + client_id + "\n";
    content += "username=" + username + "\n";
    content += "password=" + escape_field(password) + "\n";
    content += "keep_alive=" + std::to_string(keep_alive) + "\n";
    content += std::string("clean_session=") + (clean_session ? "1" : "0") + "\n";
    content += std::string("auto_reconnect=") + (auto_reconnect ? "1" : "0") + "\n";
    content += "pub_topic=" + escape_field(edit_pub_topic) + "\n";
    content += "pub_payload=" + escape_field(edit_pub_payload) + "\n";
    content += "pub_qos=" + std::to_string(edit_pub_qos) + "\n";
    content += std::string("pub_retain=") + (edit_pub_retain ? "1" : "0") + "\n";
    content += "font=" + font_file + "\n";
    content += "font_size=" + std::to_string(static_cast<int>(font_size + 0.5f)) + "\n";
    content += "filter=" + escape_field(filter_text) + "\n";
    content += "direction=" + std::to_string(direction_filter) + "\n";
    content += std::string("auto_scroll=") + (auto_scroll ? "1" : "0") + "\n";
    content += "window=" + std::to_string(window_x) + "," + std::to_string(window_y) + "," +
               std::to_string(window_w) + "," + std::to_string(window_h) + "," +
               (window_maximized ? "1" : "0") + "\n";

    write_whole_file(app_path(kIniFile), content);
}
