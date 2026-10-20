#include "ui/node_graph.h"
#include "ui/palette.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ng {
namespace {

constexpr ImU32 kBody     = pal::NodeBody;
constexpr ImU32 kHeader   = pal::NodeBody;
constexpr ImU32 kBorder   = pal::Line;
constexpr ImU32 kShadow   = pal::Shadow;
constexpr ImU32 kGridDot  = pal::GridDot;
constexpr ImU32 kTextPin  = pal::TextPin;   // 已连接引脚
constexpr ImU32 kPinIdle  = pal::TextSub;   // 未连接引脚
constexpr ImU32 kTextDim  = pal::TextMuted; // 副标题、空态提示
constexpr ImU32 kHole     = pal::Hole;

ImU32 with_alpha(ImU32 color, int alpha) {
    return (color & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

Node* find_node(std::vector<Node>& nodes, int id) {
    for (auto& node : nodes)
        if (node.id == id) return &node;
    return nullptr;
}

const Node* find_node(const std::vector<Node>& nodes, int id) {
    for (const auto& node : nodes)
        if (node.id == id) return &node;
    return nullptr;
}

float distance_to_segment(const ImVec2& p, const ImVec2& a, const ImVec2& b) {
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const ImVec2 ap(p.x - a.x, p.y - a.y);
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = 0.0f;
    if (len2 > 0.0001f) t = std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f);
    const float dx = p.x - (a.x + ab.x * t);
    const float dy = p.y - (a.y + ab.y * t);
    return std::sqrt(dx * dx + dy * dy);
}

ImVec2 bezier_point(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
    const float u = 1.0f - t;
    const float w0 = u * u * u;
    const float w1 = 3.0f * u * u * t;
    const float w2 = 3.0f * u * t * t;
    const float w3 = t * t * t;
    return ImVec2(p0.x * w0 + p1.x * w1 + p2.x * w2 + p3.x * w3,
                  p0.y * w0 + p1.y * w1 + p2.y * w2 + p3.y * w3);
}

void curve_controls(const ImVec2& a, const ImVec2& b, ImVec2& c1, ImVec2& c2) {
    const float dx = std::max(56.0f, std::fabs(b.x - a.x) * 0.55f);
    c1 = ImVec2(a.x + dx, a.y);
    c2 = ImVec2(b.x - dx, b.y);
}

}  // namespace

// ---------------------------------------------------------------- 查询接口

ImU32 Canvas::pin_color(int type) {
    switch (type) {
        case PinSession: return pal::Accent;   // 蓝
        case PinMessage: return pal::Sky;      // 青
        case PinEvent:   return pal::Mauve;    // 紫
        case PinPulse:   return pal::Peach;    // 橙
        default:         return pal::TextSub;
    }
}

const Link* Canvas::find_input_link(const std::vector<Link>& links, int node_id, int pin_index) {
    for (const auto& link : links)
        if (link.to_node == node_id && link.to_pin == pin_index) return &link;
    return nullptr;
}

bool Canvas::has_input_link(const std::vector<Link>& links, int node_id, int pin_index) {
    return find_input_link(links, node_id, pin_index) != nullptr;
}

bool Canvas::has_link_of_type(const std::vector<Link>& links, int type) {
    for (const auto& link : links)
        if (link.type == type) return true;
    return false;
}

bool Canvas::node_has_input_link(const std::vector<Link>& links, int node_id, int type) {
    for (const auto& link : links)
        if (link.to_node == node_id && link.type == type) return true;
    return false;
}

int Canvas::input_link_count(const std::vector<Link>& links, int node_id) {
    int count = 0;
    for (const auto& link : links)
        if (link.to_node == node_id) ++count;
    return count;
}

// ---------------------------------------------------------------- 坐标

ImVec2 Canvas::to_screen(const ImVec2& origin, const ImVec2& world) const {
    return ImVec2(origin.x + pan_.x + world.x * zoom_, origin.y + pan_.y + world.y * zoom_);
}

ImVec2 Canvas::to_world(const ImVec2& origin, const ImVec2& screen) const {
    return ImVec2((screen.x - origin.x - pan_.x) / zoom_, (screen.y - origin.y - pan_.y) / zoom_);
}

Rect Canvas::node_rect(const Node& node, const ImVec2& origin) const {
    Rect rect;
    rect.min = to_screen(origin, node.pos);
    rect.max = to_screen(origin, ImVec2(node.pos.x + node.size.x, node.pos.y + node.size.y));
    return rect;
}

ImVec2 Canvas::pin_pos(const Node& node, const ImVec2& origin, PinKind kind, int index) const {
    const float x = (kind == PinKind::Input) ? node.pos.x : node.pos.x + node.size.x;
    const float y = node.pos.y + kHeaderH + kPinRowH * (static_cast<float>(index) + 0.5f);
    return to_screen(origin, ImVec2(x, y));
}

void Canvas::reset_view() {
    pan_ = ImVec2(48.0f, 40.0f);
    zoom_ = 1.0f;
}

void Canvas::apply_view(const ImVec2& pan, float zoom) {
    pan_ = pan;
    zoom_ = std::clamp(zoom, kMinZoom, kMaxZoom);
}

// ---------------------------------------------------------------- 主绘制

Requests Canvas::draw(const char* id,
                      const ImVec2& size,
                      std::vector<Node>& nodes,
                      std::vector<Link>& links,
                      const std::vector<PaletteEntry>& palette,
                      const std::function<void(Node&, const ImVec2&, float)>& body) {
    Requests requests;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 46, 255));
    ImGui::BeginChild(id, size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * zoom_);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * zoom_, 4.0f * zoom_));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f * zoom_, 6.0f * zoom_));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * zoom_);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f * zoom_);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.0f * zoom_);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // 滚轮缩放：保持光标下的世界坐标不动；光标在节点内时让位给节点里的滚动区域
    bool over_node = false;
    for (const auto& node : nodes) {
        if (node_rect(node, origin).contains(mouse)) {
            over_node = true;
            break;
        }
    }

    const float wheel = ImGui::GetIO().MouseWheel;
    if (hovered && wheel != 0.0f && drag_ == Drag::None && !over_node) {
        const ImVec2 anchor = to_world(origin, mouse);
        const float target = std::clamp(zoom_ * (wheel > 0.0f ? 1.08f : 1.0f / 1.08f), kMinZoom, kMaxZoom);
        if (target != zoom_) {
            zoom_ = target;
            pan_.x = mouse.x - origin.x - anchor.x * zoom_;
            pan_.y = mouse.y - origin.y - anchor.y * zoom_;
            ImGui::PopFont();
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * zoom_);
        }
    }

    // ---- 命中测试（后绘制的节点在上层）
    hover_node_ = -1;
    hover_input_ = -1;
    hover_output_ = -1;
    hover_link_ = -1;
    hover_resize_ = false;

    const float pin_reach = (kPinRadius + 5.0f) * zoom_;
    for (int index = static_cast<int>(nodes.size()) - 1; index >= 0; --index) {
        if (hover_node_ >= 0) break;
        Node& node = nodes[static_cast<size_t>(index)];
        const Rect rect = node_rect(node, origin);
        if (!rect.contains(mouse)) continue;

        hover_node_ = node.id;

        // 右下角是缩放热区，命中后不再判定引脚
        if (mouse.x >= rect.max.x - kGripSize * zoom_ && mouse.y >= rect.max.y - kGripSize * zoom_) {
            hover_resize_ = true;
            continue;
        }

        float best = pin_reach;
        for (int i = 0; i < static_cast<int>(node.inputs.size()); ++i) {
            const ImVec2 p = pin_pos(node, origin, PinKind::Input, i);
            const float dx = p.x - mouse.x;
            const float dy = p.y - mouse.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= best) {
                best = dist;
                hover_input_ = i;
            }
        }
        best = pin_reach;
        for (int i = 0; i < static_cast<int>(node.outputs.size()); ++i) {
            const ImVec2 p = pin_pos(node, origin, PinKind::Output, i);
            const float dx = p.x - mouse.x;
            const float dy = p.y - mouse.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist <= best) {
                best = dist;
                hover_output_ = i;
            }
        }
    }

    if (hover_node_ < 0 && drag_ == Drag::None) {
        const float threshold = 7.0f * zoom_;
        for (int i = 0; i < static_cast<int>(links.size()); ++i) {
            const Link& link = links[static_cast<size_t>(i)];
            const Node* from = find_node(nodes, link.from_node);
            const Node* to = find_node(nodes, link.to_node);
            if (!from || !to) continue;

            const ImVec2 p0 = pin_pos(*from, origin, PinKind::Output, link.from_pin);
            const ImVec2 p1 = pin_pos(*to, origin, PinKind::Input, link.to_pin);
            ImVec2 c1, c2;
            curve_controls(p0, p1, c1, c2);

            float best = threshold;
            ImVec2 previous = p0;
            for (int step = 1; step <= 18; ++step) {
                const ImVec2 point = bezier_point(p0, c1, c2, p1, static_cast<float>(step) / 18.0f);
                const float dist = distance_to_segment(mouse, previous, point);
                if (dist < best) {
                    best = dist;
                    hover_link_ = i;
                }
                previous = point;
            }
        }
    }

    handle_input(origin, canvas_size, mouse, hovered, nodes, links, requests);

    if (hover_resize_ || drag_ == Drag::Resize) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);

    // ---- 背景与连线
    draw_grid(draw_list, origin, canvas_size);
    draw_links(draw_list, origin, nodes, links, hover_link_);

    if (drag_ == Drag::Link) {
        const Node* source = find_node(nodes, link_node_);
        if (source) {
            const ImVec2 p0 = pin_pos(*source, origin, link_from_, link_pin_);
            ImVec2 c1, c2;
            curve_controls(p0, mouse, c1, c2);
            const ImU32 color = pin_color(link_type_);
            draw_list->AddBezierCubic(p0, c1, c2, mouse, with_alpha(color, 45), 6.0f * zoom_);
            draw_list->AddBezierCubic(p0, c1, c2, mouse, color, 2.0f * zoom_);
            draw_list->AddCircleFilled(mouse, 3.5f * zoom_, color, 12);
        }
    }

    // ---- 节点（拖拽中的节点最后绘制，保证在最上层）
    std::vector<int> order;
    order.reserve(nodes.size());
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) order.push_back(i);
    if (drag_ == Drag::Node) {
        const auto it = std::find_if(order.begin(), order.end(), [&](int index) {
            return nodes[static_cast<size_t>(index)].id == drag_node_;
        });
        if (it != order.end()) {
            const int value = *it;
            order.erase(it);
            order.push_back(value);
        }
    }

    for (const int index : order) {
        Node& node = nodes[static_cast<size_t>(index)];
        const bool active = (drag_ == Drag::Node && node.id == drag_node_) || node.id == hover_node_;
        draw_node(draw_list, origin, node, active);

        // 节点内容放进透明子窗口：换行后光标回到节点体起点，且自动裁剪
        const Rect rect = node_rect(node, origin);
        const int rows = static_cast<int>(std::max(node.inputs.size(), node.outputs.size()));
        const ImVec2 body_origin(rect.min.x + 12.0f * zoom_,
                                 rect.min.y + (kHeaderH + kPinRowH * static_cast<float>(rows) + 6.0f) * zoom_);
        const ImVec2 body_size(node.size.x * zoom_ - 24.0f * zoom_,
                               node.size.y * zoom_ - (kHeaderH + kPinRowH * static_cast<float>(rows) + 12.0f) * zoom_);

        ImGui::PushID(node.id);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::SetCursorScreenPos(body_origin);
        ImGui::BeginChild("##body", body_size, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoMove);
        ImGui::PopStyleVar();
        body(node, ImGui::GetCursorScreenPos(), zoom_);
        ImGui::EndChild();
        ImGui::PopID();
    }

    // 空白处提示
    if (nodes.empty()) {
        const ImVec2 center(origin.x + canvas_size.x * 0.5f, origin.y + canvas_size.y * 0.5f);
        const char* hint = "右键空白处添加节点";
        const ImVec2 text_size = ImGui::CalcTextSize(hint);
        draw_list->AddText(ImVec2(center.x - text_size.x * 0.5f, center.y), kTextDim, hint);
    }

    draw_popups(nodes, palette, requests);

    ImGui::PopFont();
    ImGui::PopStyleVar(5);
    ImGui::EndChild();
    return requests;
}

// ---------------------------------------------------------------- 交互

void Canvas::handle_input(const ImVec2& origin,
                          const ImVec2& size,
                          const ImVec2& mouse,
                          bool hovered,
                          std::vector<Node>& nodes,
                          std::vector<Link>& links,
                          Requests& requests) {
    (void)size;
    const ImVec2 world_mouse = to_world(origin, mouse);

    const auto remove_links_to = [&](int node_id, int pin_index) {
        links.erase(std::remove_if(links.begin(), links.end(),
                                   [&](const Link& link) {
                                       return link.to_node == node_id && link.to_pin == pin_index;
                                   }),
                    links.end());
    };

    if (drag_ == Drag::None && hovered) {
        const ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hover_resize_ && hover_node_ >= 0) {
                if (const Node* node = find_node(nodes, hover_node_)) {
                    drag_ = Drag::Resize;
                    resize_node_ = node->id;
                    resize_grip_ = ImVec2(world_mouse.x - node->pos.x - node->size.x,
                                          world_mouse.y - node->pos.y - node->size.y);
                }
            } else if (hover_node_ >= 0 && hover_output_ >= 0) {
                const Node* node = find_node(nodes, hover_node_);
                if (node) {
                    link_node_ = node->id;
                    link_pin_ = hover_output_;
                    link_from_ = PinKind::Output;
                    link_type_ = node->outputs[static_cast<size_t>(hover_output_)].type;
                    drag_ = Drag::Link;
                }
            } else if (hover_node_ >= 0 && hover_input_ >= 0) {
                Node* node = find_node(nodes, hover_node_);
                if (node) {
                    if (const Link* existing = find_input_link(links, node->id, hover_input_)) {
                        link_node_ = existing->from_node;
                        link_pin_ = existing->from_pin;
                        link_type_ = existing->type;
                        link_from_ = PinKind::Output;
                        remove_links_to(node->id, hover_input_);
                        requests.changed = true;
                    } else {
                        link_node_ = node->id;
                        link_pin_ = hover_input_;
                        link_type_ = node->inputs[static_cast<size_t>(hover_input_)].type;
                        link_from_ = PinKind::Input;
                    }
                    drag_ = Drag::Link;
                }
            } else if (hover_node_ >= 0) {
                Node* node = find_node(nodes, hover_node_);
                if (node) {
                    const Rect rect = node_rect(*node, origin);
                    if (mouse.y <= rect.min.y + kHeaderH * zoom_) {
                        drag_ = Drag::Node;
                        drag_node_ = node->id;
                        drag_offset_ = ImVec2(world_mouse.x - node->pos.x, world_mouse.y - node->pos.y);
                    }
                }
            } else {
                drag_ = Drag::Pan;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) drag_ = Drag::Pan;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            popup_world_ = world_mouse;
            if (hover_link_ >= 0) {
                links.erase(links.begin() + hover_link_);
                requests.changed = true;
            } else if (hover_node_ >= 0) {
                popup_node_ = hover_node_;
                want_node_popup_ = true;
            } else {
                want_add_popup_ = true;
            }
        }
        (void)io;
    }

    if (drag_ == Drag::Node) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            drag_ = Drag::None;
            drag_node_ = -1;
        } else if (Node* node = find_node(nodes, drag_node_)) {
            node->pos.x = std::round(world_mouse.x - drag_offset_.x);
            node->pos.y = std::round(world_mouse.y - drag_offset_.y);
            requests.changed = true;
        }
    } else if (drag_ == Drag::Resize) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            drag_ = Drag::None;
            resize_node_ = -1;
        } else if (Node* node = find_node(nodes, resize_node_)) {
            node->size.x = std::max(kMinNodeW, std::round(world_mouse.x - resize_grip_.x - node->pos.x));
            node->size.y = std::max(kMinNodeH, std::round(world_mouse.y - resize_grip_.y - node->pos.y));
            requests.changed = true;
        }
    } else if (drag_ == Drag::Pan) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            pan_.x += delta.x;
            pan_.y += delta.y;
        } else {
            drag_ = Drag::None;
        }
    } else if (drag_ == Drag::Link) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const bool valid_target = hover_node_ >= 0 && hover_node_ != link_node_ &&
                                      ((link_from_ == PinKind::Output && hover_input_ >= 0) ||
                                       (link_from_ == PinKind::Input && hover_output_ >= 0));
            if (valid_target) {
                Node* target = find_node(nodes, hover_node_);
                if (target) {
                    const int to_node = target->id;
                    const int to_pin = (link_from_ == PinKind::Output) ? hover_input_ : hover_output_;
                    const PinKind to_kind = (link_from_ == PinKind::Output) ? PinKind::Input : PinKind::Output;
                    const int type = (to_kind == PinKind::Input)
                                         ? target->inputs[static_cast<size_t>(to_pin)].type
                                         : target->outputs[static_cast<size_t>(to_pin)].type;

                    if (type == link_type_) {
                        Link link;
                        link.type = link_type_;
                        if (link_from_ == PinKind::Output) {
                            link.from_node = link_node_;
                            link.from_pin = link_pin_;
                            link.to_node = to_node;
                            link.to_pin = to_pin;
                            remove_links_to(to_node, to_pin);
                        } else {
                            link.from_node = to_node;
                            link.from_pin = to_pin;
                            link.to_node = link_node_;
                            link.to_pin = link_pin_;
                            remove_links_to(link_node_, link_pin_);
                        }
                        const bool duplicate =
                            std::any_of(links.begin(), links.end(), [&](const Link& item) {
                                return item.from_node == link.from_node && item.from_pin == link.from_pin &&
                                       item.to_node == link.to_node && item.to_pin == link.to_pin;
                            });
                        if (!duplicate) {
                            links.push_back(link);
                            requests.changed = true;
                        }
                    }
                }
            }
            drag_ = Drag::None;
        }
    }
}

// ---------------------------------------------------------------- 绘制细节

void Canvas::draw_grid(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
    const float step = 30.0f * zoom_;
    if (step < 8.0f) return;

    float start_x = std::fmod(pan_.x, step);
    float start_y = std::fmod(pan_.y, step);
    if (start_x < 0.0f) start_x += step;
    if (start_y < 0.0f) start_y += step;

    const float max_x = origin.x + size.x;
    const float max_y = origin.y + size.y;
    const float radius = std::max(1.0f, 1.1f * zoom_);

    for (float y = origin.y + start_y; y < max_y; y += step) {
        for (float x = origin.x + start_x; x < max_x; x += step)
            draw->AddCircleFilled(ImVec2(x, y), radius, kGridDot, 4);
    }
}

void Canvas::draw_links(ImDrawList* draw, const ImVec2& origin, const std::vector<Node>& nodes,
                        const std::vector<Link>& links, int hovered_link) const {
    const float time = static_cast<float>(ImGui::GetTime());

    for (int i = 0; i < static_cast<int>(links.size()); ++i) {
        const Link& link = links[static_cast<size_t>(i)];
        const Node* from = find_node(nodes, link.from_node);
        const Node* to = find_node(nodes, link.to_node);
        if (!from || !to) continue;

        const ImVec2 p0 = pin_pos(*from, origin, PinKind::Output, link.from_pin);
        const ImVec2 p1 = pin_pos(*to, origin, PinKind::Input, link.to_pin);
        ImVec2 c1, c2;
        curve_controls(p0, p1, c1, c2);

        const ImU32 color = pin_color(link.type);
        const bool hot = (i == hovered_link);
        draw->AddBezierCubic(p0, c1, c2, p1, with_alpha(color, hot ? 60 : 30), (hot ? 8.0f : 6.0f) * zoom_);
        draw->AddBezierCubic(p0, c1, c2, p1, with_alpha(color, hot ? 255 : 190), (hot ? 2.6f : 1.8f) * zoom_);

        // 流动光点：从输出端流向输入端，提示数据方向
        const float phase = std::fmod(time * 0.28f + static_cast<float>(i) * 0.17f, 1.0f);
        const ImVec2 spark = bezier_point(p0, c1, c2, p1, phase);
        draw->AddCircleFilled(spark, 2.6f * zoom_, with_alpha(color, 230), 10);
    }
}

void Canvas::draw_node(ImDrawList* draw, const ImVec2& origin, Node& node, bool active) const {
    const Rect rect = node_rect(node, origin);
    const float rounding = kRounding * zoom_;
    const ImVec2 head_max(rect.max.x, rect.min.y + kHeaderH * zoom_);

    draw->AddRectFilled(ImVec2(rect.min.x + 2.0f * zoom_, rect.min.y + 7.0f * zoom_),
                        ImVec2(rect.max.x + 3.0f * zoom_, rect.max.y + 8.0f * zoom_), kShadow, rounding);
    draw->AddRectFilled(rect.min, rect.max, kBody, rounding);
    draw->AddRectFilled(rect.min, head_max, kHeader, rounding, ImDrawFlags_RoundCornersTop);
    draw->AddLine(ImVec2(rect.min.x, head_max.y), ImVec2(rect.max.x, head_max.y),
                  with_alpha(node.accent, 70), 1.0f);
    draw->AddRect(rect.min, rect.max, active ? with_alpha(node.accent, 200) : kBorder, rounding,
                  active ? 1.8f * zoom_ : 1.0f * zoom_);
    draw->AddRectFilled(ImVec2(rect.min.x + zoom_, rect.min.y + 9.0f * zoom_),
                        ImVec2(rect.min.x + 3.5f * zoom_, head_max.y - 9.0f * zoom_), node.accent, 1.5f * zoom_);

    draw->PushClipRect(rect.min, rect.max, true);

    const float text_y = rect.min.y + (kHeaderH * zoom_ - ImGui::GetTextLineHeight()) * 0.5f;
    draw->AddText(ImVec2(rect.min.x + 12.0f * zoom_, text_y), node.accent, node.title.c_str());
    if (!node.subtitle.empty()) {
        const float width = ImGui::CalcTextSize(node.subtitle.c_str()).x;
        draw->AddText(ImVec2(rect.max.x - width - 12.0f * zoom_, text_y), kTextDim, node.subtitle.c_str());
    }

    const auto draw_pins = [&](std::vector<Pin>& pins, PinKind kind, int hovered_pin) {
        for (int i = 0; i < static_cast<int>(pins.size()); ++i) {
            Pin& pin = pins[static_cast<size_t>(i)];
            const ImVec2 center = pin_pos(node, origin, kind, i);
            const ImU32 color = pin_color(pin.type);

            draw->AddCircleFilled(center, kPinRadius * zoom_, pin.connected ? color : kHole, 16);
            draw->AddCircle(center, kPinRadius * zoom_, with_alpha(color, pin.connected ? 255 : 140), 16,
                            1.6f * zoom_);

            if (drag_ == Drag::Link && link_type_ == pin.type && i == hovered_pin && node.id == hover_node_)
                draw->AddCircle(center, (kPinRadius + 6.0f) * zoom_, with_alpha(color, 120), 20, 2.0f * zoom_);

            const ImVec2 label_size = ImGui::CalcTextSize(pin.label);
            const float label_x = (kind == PinKind::Input) ? center.x + 12.0f * zoom_
                                                           : center.x - label_size.x - 12.0f * zoom_;
            draw->AddText(ImVec2(label_x, center.y - label_size.y * 0.5f),
                          pin.connected ? kTextPin : kPinIdle, pin.label);
        }
    };

    draw_pins(node.inputs, PinKind::Input, hover_input_);
    draw_pins(node.outputs, PinKind::Output, hover_output_);

    // 右下角缩放把手
    const bool grip_hot = hover_resize_ && node.id == hover_node_;
    const ImU32 grip_color = grip_hot ? with_alpha(node.accent, 235) : IM_COL32(255, 255, 255, 40);
    for (int i = 0; i < 3; ++i) {
        const float offset = (5.0f + static_cast<float>(i) * 4.5f) * zoom_;
        const ImVec2 corner(rect.max.x - 5.0f * zoom_, rect.max.y - 5.0f * zoom_);
        draw->AddLine(ImVec2(corner.x - offset, corner.y), ImVec2(corner.x, corner.y - offset), grip_color,
                      1.3f * zoom_);
    }

    draw->PopClipRect();
}

void Canvas::draw_popups(std::vector<Node>& nodes, const std::vector<PaletteEntry>& palette,
                         Requests& requests) {
    if (want_add_popup_) {
        ImGui::OpenPopup("##ng_add");
        want_add_popup_ = false;
    }
    if (want_node_popup_) {
        ImGui::OpenPopup("##ng_node");
        want_node_popup_ = false;
    }

    if (ImGui::BeginPopup("##ng_add")) {
        if (ImGui::BeginMenu("添加节点")) {
            for (const auto& entry : palette) {
                if (ImGui::MenuItem(entry.name)) {
                    requests.add_type = entry.type;
                    requests.add_pos = popup_world_;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("整理视图")) reset_view();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("##ng_node")) {
        if (ImGui::MenuItem("删除节点")) {
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                       [&](const Node& node) { return node.id == popup_node_; }),
                        nodes.end());
            requests.changed = true;
        }
        ImGui::EndPopup();
    }
}

}  // namespace ng
