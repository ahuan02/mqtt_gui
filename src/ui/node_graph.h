#pragma once

#include "imgui.h"

#include <functional>
#include <string>
#include <vector>

namespace ng {

enum class PinKind { Input, Output };

// 引脚数据类型：只有同类型才能相连
enum PinType {
    PinSession = 0,   // 会话（Broker 的连接）
    PinMessage,       // 消息流
    PinEvent,         // 事件流
    PinPulse,         // 脉冲（触发）
    PinTypeCount
};

struct Pin {
    PinKind     kind{PinKind::Input};
    int         type{PinSession};
    const char* label{""};
    bool        connected{false};   // 运行时刷新，实心=已连接
};

struct Node {
    int         id{0};
    int         type{0};
    std::string title;
    std::string subtitle;
    ImVec2      pos{0.0f, 0.0f};
    ImVec2      size{280.0f, 140.0f};
    ImU32       accent{0};
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
};

struct Link {
    int from_node{0};
    int from_pin{0};
    int to_node{0};
    int to_pin{0};
    int type{PinSession};
};

struct PaletteEntry {
    int         type{0};
    const char* name{""};
};

struct Rect {
    ImVec2 min{};
    ImVec2 max{};

    bool contains(const ImVec2& point) const {
        return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
    }
};

// 本帧产生的结构性请求，由业务层执行（画布不认识业务节点）
struct Requests {
    bool   changed{false};
    int    add_type{-1};
    ImVec2 add_pos{};
};

class Canvas {
public:
    static constexpr float kHeaderH = 30.0f;
    static constexpr float kPinRowH = 21.0f;
    static constexpr float kPinRadius = 5.0f;
    static constexpr float kRounding = 9.0f;
    static constexpr float kMinZoom = 0.7f;
    static constexpr float kMaxZoom = 1.5f;
    static constexpr float kGripSize = 18.0f;   // 右下角拖拽缩放热区
    static constexpr float kMinNodeW = 180.0f;
    static constexpr float kMinNodeH = 100.0f;

    // 每帧调用一次。body 回调负责节点内部控件，坐标使用屏幕绝对坐标。
    Requests draw(const char* id,
                  const ImVec2& size,
                  std::vector<Node>& nodes,
                  std::vector<Link>& links,
                  const std::vector<PaletteEntry>& palette,
                  const std::function<void(Node&, const ImVec2& body_origin, float zoom)>& body);

    float  zoom() const { return zoom_; }
    ImVec2 pan() const { return pan_; }
    void   reset_view();
    void   apply_view(const ImVec2& pan, float zoom);

    static const Link* find_input_link(const std::vector<Link>& links, int node_id, int pin_index);
    static bool        has_input_link(const std::vector<Link>& links, int node_id, int pin_index);
    static bool        has_link_of_type(const std::vector<Link>& links, int type);
    static bool        node_has_input_link(const std::vector<Link>& links, int node_id, int type);
    static int         input_link_count(const std::vector<Link>& links, int node_id);
    static ImU32       pin_color(int type);

private:
    enum class Drag { None, Pan, Node, Link, Resize };

    ImVec2 to_screen(const ImVec2& origin, const ImVec2& world) const;
    ImVec2 to_world(const ImVec2& origin, const ImVec2& screen) const;
    Rect   node_rect(const Node& node, const ImVec2& origin) const;
    ImVec2 pin_pos(const Node& node, const ImVec2& origin, PinKind kind, int index) const;

    void handle_input(const ImVec2& origin, const ImVec2& size, const ImVec2& mouse,
                      bool hovered, std::vector<Node>& nodes, std::vector<Link>& links,
                      Requests& requests);
    void draw_grid(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const;
    void draw_links(ImDrawList* draw, const ImVec2& origin, const std::vector<Node>& nodes,
                    const std::vector<Link>& links, int hovered_link) const;
    void draw_node(ImDrawList* draw, const ImVec2& origin, Node& node, bool active) const;
    void draw_popups(std::vector<Node>& nodes, const std::vector<PaletteEntry>& palette,
                     Requests& requests);

    float  zoom_{1.0f};
    ImVec2 pan_{48.0f, 40.0f};

    Drag   drag_{Drag::None};
    int    drag_node_{-1};
    ImVec2 drag_offset_{};

    int    resize_node_{-1};
    ImVec2 resize_grip_{};
    bool   hover_resize_{false};

    int          link_node_{-1};
    int          link_pin_{-1};
    PinKind      link_from_{PinKind::Output};
    int          link_type_{PinSession};

    int    hover_node_{-1};
    int    hover_input_{-1};
    int    hover_output_{-1};
    int    hover_link_{-1};
    int    hot_input_{-1};
    int    hot_output_{-1};

    int    popup_node_{-1};
    ImVec2 popup_world_{};
    bool   want_add_popup_{false};
    bool   want_node_popup_{false};
};

}  // namespace ng
