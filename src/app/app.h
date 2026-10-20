#pragma once

#include "core/file_io.h"
#include "core/message_store.h"
#include "core/mqtt_client.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct Subscription {
    std::string filter;
    int         qos{0};
};

struct EventLine {
    std::string clock;
    std::string text;
    bool        error{false};
};

class App {
public:
    App();
    ~App();

    void init();
    void shutdown();
    void tick();

    // UI 触发
    void connect_now();
    void disconnect_now();
    void subscribe_topic(const std::string& filter, int qos);
    void unsubscribe_topic(const std::string& filter);
    void publish_message(const std::string& topic, const std::string& payload, int qos, bool retain);
    void clear_messages();
    void sync_subscriptions(const std::vector<Subscription>& wanted);

    mqtt::Client  client;
    MessageStore  store;

    // 连接配置
    std::string host{"127.0.0.1"};
    int         port{1883};
    std::string client_id;
    std::string username;
    std::string password;
    int         keep_alive{60};
    bool        clean_session{true};
    bool        auto_reconnect{true};

    std::vector<Subscription> subscriptions;
    std::deque<EventLine>     event_log;
    static constexpr size_t   kEventLogCap = 200;

    // 直接绑定到节点内嵌输入框的缓冲
    char edit_host[128]{};
    char edit_port[8]{};
    char edit_client_id[128]{};
    char edit_user[128]{};
    char edit_pass[128]{};
    char edit_pub_topic[256]{};
    char edit_pub_payload[4096]{};
    int  edit_pub_qos{0};
    bool edit_pub_retain{false};
    char filter_text[128]{};

    // 字体：留空表示自动扫描 fonts 目录（ui.ttf 优先）
    std::string font_file;
    float       font_size{17.0f};

    // 窗口几何：-1 表示首次启动，由程序居中
    int  window_x{-1};
    int  window_y{-1};
    int  window_w{1440};
    int  window_h{900};
    bool window_maximized{false};

    // UI 状态
    uint64_t    selected_seq{0};
    bool        auto_scroll{true};
    int         direction_filter{0};   // 0 全部 / 1 接收 / 2 发送
    std::string hint;
    bool        hint_error{false};
    double      hint_time{0.0};
    uint64_t    last_store_version{0};

    void set_hint(std::string text, bool error);

private:
    mqtt::Options build_options() const;
    void          make_entry(mqtt::Message&& message, bool outbound);
    void          load_config();
    void          save_config();

    std::vector<mqtt::Message> incoming_;
    std::vector<std::string>   events_;
    mqtt::State                last_state_{mqtt::State::Disconnected};
    double                     reconnect_at_{0.0};
    bool                       want_connected_{false};
    bool                       config_dirty_{false};
    uint64_t                   next_seq_{1};
};
