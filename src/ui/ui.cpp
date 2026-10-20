#include "ui/ui.h"

#include "app/app.h"
#include "core/file_io.h"
#include "ui/node_graph.h"
#include "ui/palette.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ui {
namespace {

float g_scale = 1.0f;

// ---------------------------------------------------------------- 配色
// 全部取自 ui/palette.h，这里只做短别名
constexpr ImU32 kAccent  = pal::Accent;
constexpr ImU32 kSuccess = pal::Success;
constexpr ImU32 kWarning = pal::Warning;
constexpr ImU32 kDanger  = pal::Danger;
constexpr ImU32 kPurple  = pal::Mauve;
constexpr ImU32 kCyan    = pal::Sky;
constexpr ImU32 kMuted   = pal::TextSub;

ImVec4 to_vec(ImU32 color, float alpha = 1.0f) {
    return ImVec4(((color >> IM_COL32_R_SHIFT) & 0xFFu) / 255.0f,
                  ((color >> IM_COL32_G_SHIFT) & 0xFFu) / 255.0f,
                  ((color >> IM_COL32_B_SHIFT) & 0xFFu) / 255.0f, alpha);
}

// ---------------------------------------------------------------- 主题
void apply_style(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = to_vec(pal::Text);
    c[ImGuiCol_TextDisabled] = to_vec(pal::TextMuted);
    c[ImGuiCol_WindowBg] = to_vec(pal::Canvas);
    c[ImGuiCol_ChildBg] = to_vec(pal::Canvas);
    c[ImGuiCol_PopupBg] = to_vec(pal::Surface);
    c[ImGuiCol_Border] = to_vec(IM_COL32(0x23, 0x2B, 0x38, 255));
    c[ImGuiCol_BorderShadow] = to_vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_FrameBg] = to_vec(IM_COL32(0x1A, 0x21, 0x2C, 255));
    c[ImGuiCol_FrameBgHovered] = to_vec(IM_COL32(0x23, 0x2C, 0x3A, 255));
    c[ImGuiCol_FrameBgActive] = to_vec(IM_COL32(0x2A, 0x35, 0x47, 255));
    c[ImGuiCol_TitleBg] = to_vec(IM_COL32(0x12, 0x16, 0x1D, 255));
    c[ImGuiCol_TitleBgActive] = to_vec(IM_COL32(0x15, 0x1A, 0x22, 255));
    c[ImGuiCol_MenuBarBg] = to_vec(IM_COL32(0x15, 0x1A, 0x22, 255));
    c[ImGuiCol_ScrollbarBg] = to_vec(IM_COL32(0x0F, 0x13, 0x19, 255));
    c[ImGuiCol_ScrollbarGrab] = to_vec(IM_COL32(0x2C, 0x36, 0x46, 255));
    c[ImGuiCol_ScrollbarGrabHovered] = to_vec(IM_COL32(0x3A, 0x46, 0x5A, 255));
    c[ImGuiCol_ScrollbarGrabActive] = to_vec(IM_COL32(0x4A, 0x58, 0x72, 255));
    c[ImGuiCol_CheckMark] = to_vec(kAccent);
    c[ImGuiCol_SliderGrab] = to_vec(IM_COL32(0x3F, 0x71, 0xC8, 255));
    c[ImGuiCol_SliderGrabActive] = to_vec(kAccent);
    c[ImGuiCol_Button] = to_vec(IM_COL32(0x21, 0x2A, 0x38, 255));
    c[ImGuiCol_ButtonHovered] = to_vec(IM_COL32(0x2C, 0x37, 0x49, 255));
    c[ImGuiCol_ButtonActive] = to_vec(IM_COL32(0x36, 0x45, 0x5C, 255));
    c[ImGuiCol_Header] = to_vec(IM_COL32(0x22, 0x31, 0x49, 255));
    c[ImGuiCol_HeaderHovered] = to_vec(IM_COL32(0x2B, 0x3F, 0x5F, 255));
    c[ImGuiCol_HeaderActive] = to_vec(IM_COL32(0x35, 0x50, 0x7A, 255));
    c[ImGuiCol_Separator] = to_vec(IM_COL32(0x23, 0x2B, 0x38, 255));
    c[ImGuiCol_ResizeGrip] = to_vec(IM_COL32(0x2C, 0x36, 0x46, 128));
    c[ImGuiCol_TableHeaderBg] = to_vec(IM_COL32(0x17, 0x1D, 0x27, 255));
    c[ImGuiCol_TableBorderStrong] = to_vec(IM_COL32(0x23, 0x2B, 0x38, 255));
    c[ImGuiCol_TableBorderLight] = to_vec(IM_COL32(0x1C, 0x23, 0x2E, 255));
    c[ImGuiCol_TableRowBg] = to_vec(IM_COL32(0, 0, 0, 0));
    c[ImGuiCol_TableRowBgAlt] = to_vec(IM_COL32(0xFF, 0xFF, 0xFF, 6));
    c[ImGuiCol_TextSelectedBg] = to_vec(pal::Selection);
    c[ImGuiCol_NavCursor] = to_vec(kAccent);

    style.ScaleAllSizes(scale);
}

bool file_exists(const std::string& path) {
    const DWORD attributes = ::GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string lower_ascii(std::string text) {
    for (char& ch : text) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return text;
}

bool is_font_file(const std::string& name) {
    const std::string lower = lower_ascii(name);
    const auto ends_with = [&](const char* suffix) {
        const size_t len = std::strlen(suffix);
        return lower.size() > len && lower.compare(lower.size() - len, len, suffix) == 0;
    };
    return ends_with(".ttf") || ends_with(".otf") || ends_with(".ttc");
}

// 字体目录里按约定挑文件：ui.ttf/ui.otf 优先，否则取字典序第一个字体
std::string pick_font_in(const std::string& dir) {
    WIN32_FIND_DATAA data{};
    const std::string pattern = dir + "\\*";
    HANDLE handle = ::FindFirstFileA(pattern.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) return {};

    std::string preferred;
    std::string fallback;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::string name = data.cFileName;
        if (!is_font_file(name)) continue;

        const std::string lower = lower_ascii(name);
        if (lower == "ui.ttf" || lower == "ui.otf" || lower == "ui.ttc") {
            preferred = dir + "\\" + name;
            break;
        }
        if (fallback.empty() || name < fallback) fallback = name;
    } while (::FindNextFileA(handle, &data));
    ::FindClose(handle);

    if (!preferred.empty()) return preferred;
    return fallback.empty() ? std::string() : dir + "\\" + fallback;
}

// 找字体：先用 ini 里指定的文件，再扫描 fonts 目录，最后退回内嵌楷体
std::string resolve_font_path(const std::string& requested) {
    const std::string base = executable_directory();
    const std::string dirs[] = {
        base + "\\fonts",
        base + "\\res\\fonts",
        base + "\\..\\res\\fonts",
    };

    if (!requested.empty()) {
        for (const auto& dir : dirs) {
            const std::string candidate = dir + "\\" + requested;
            if (file_exists(candidate)) return candidate;
        }
    }

    for (const auto& dir : dirs) {
        const std::string found = pick_font_in(dir);
        if (!found.empty()) return found;
    }
    return {};
}

// ---------------------------------------------------------------- 节点定义
enum NodeType { kBroker = 0, kSubscribe, kTable, kPublish, kPulse, kLog, kNodeTypeCount };

constexpr const char* kGraphFile = "mqtt_gui.graph";

struct SubNode {
    int  node_id{0};
    char topic[256]{};
    int  qos{0};
};

struct PubNode {
    int  node_id{0};
    char topic[256]{};
    char payload[2048]{};
    int  qos{0};
    bool retain{false};
};

struct GraphState {
    std::vector<ng::Node> nodes;
    std::vector<ng::Link> links;
    std::vector<SubNode>  subs;
    std::vector<PubNode>  pubs;
    ng::Canvas            canvas;
    int                   next_id{1};
    bool                  dirty{false};
    bool                  loaded{false};
    bool                  pulse_fired{false};
    size_t                log_seen{0};
    double                last_connect_attempt{-100.0};
};

GraphState g;
double g_flash_time = 0.0;

ng::Node* find_node(int id) {
    for (auto& node : g.nodes)
        if (node.id == id) return &node;
    return nullptr;
}

SubNode* find_sub(int id) {
    for (auto& sub : g.subs)
        if (sub.node_id == id) return &sub;
    return nullptr;
}

PubNode* find_pub(int id) {
    for (auto& pub : g.pubs)
        if (pub.node_id == id) return &pub;
    return nullptr;
}

ImU32 accent_of(int type) {
    switch (type) {
        case kBroker:    return pal::Accent;    // 蓝
        case kSubscribe: return pal::Success;   // 绿
        case kTable:     return pal::Mauve;     // 紫
        case kPublish:   return pal::Sky;       // 青
        case kPulse:     return pal::Peach;     // 橙
        case kLog:       return pal::Lavender;  // 淡紫
        default:         return pal::TextSub;
    }
}

ng::Node make_node(int type) {
    ng::Node node;
    node.type = type;
    node.accent = accent_of(type);

    switch (type) {
        case kBroker:
            node.title = "BROKER";
            node.subtitle = "未连接";
            node.pos = ImVec2(40.0f, 60.0f);
            node.size = ImVec2(302.0f, 288.0f);
            node.outputs.push_back({ng::PinKind::Output, ng::PinSession, "会话"});
            node.outputs.push_back({ng::PinKind::Output, ng::PinEvent, "事件"});
            break;
        case kSubscribe:
            node.title = "订阅";
            node.pos = ImVec2(392.0f, 60.0f);
            node.size = ImVec2(264.0f, 132.0f);
            node.inputs.push_back({ng::PinKind::Input, ng::PinSession, "会话"});
            node.outputs.push_back({ng::PinKind::Output, ng::PinMessage, "消息"});
            break;
        case kTable:
            node.title = "消息面板";
            node.pos = ImVec2(704.0f, 24.0f);
            node.size = ImVec2(640.0f, 440.0f);
            node.inputs.push_back({ng::PinKind::Input, ng::PinMessage, "消息"});
            break;
        case kPublish:
            node.title = "发布";
            node.pos = ImVec2(392.0f, 258.0f);
            node.size = ImVec2(300.0f, 240.0f);
            node.inputs.push_back({ng::PinKind::Input, ng::PinSession, "会话"});
            node.inputs.push_back({ng::PinKind::Input, ng::PinPulse, "触发"});
            break;
        case kPulse:
            node.title = "脉冲";
            node.pos = ImVec2(60.0f, 380.0f);
            node.size = ImVec2(228.0f, 150.0f);
            node.outputs.push_back({ng::PinKind::Output, ng::PinPulse, "脉冲"});
            break;
        case kLog:
            node.title = "事件日志";
            node.pos = ImVec2(60.0f, 548.0f);
            node.size = ImVec2(320.0f, 300.0f);
            node.inputs.push_back({ng::PinKind::Input, ng::PinEvent, "事件"});
            break;
        default:
            break;
    }
    return node;
}

bool has_node_of_type(int type) {
    for (const auto& node : g.nodes)
        if (node.type == type) return true;
    return false;
}

int add_node(App& app, int type, const ImVec2& pos, int forced_id = 0) {
    if (type < 0 || type >= kNodeTypeCount) return 0;
    if (type == kBroker && has_node_of_type(kBroker)) return 0;

    ng::Node node = make_node(type);
    node.id = forced_id > 0 ? forced_id : g.next_id++;
    if (forced_id > 0) g.next_id = std::max(g.next_id, forced_id + 1);
    node.pos = pos;
    g.nodes.push_back(node);

    if (type == kSubscribe) {
        SubNode sub;
        sub.node_id = node.id;
        std::snprintf(sub.topic, sizeof sub.topic, "test/#");
        g.subs.push_back(sub);
    } else if (type == kPublish) {
        PubNode pub;
        pub.node_id = node.id;
        std::snprintf(pub.topic, sizeof pub.topic, "%s", app.edit_pub_topic);
        std::snprintf(pub.payload, sizeof pub.payload, "%s", app.edit_pub_payload);
        pub.qos = app.edit_pub_qos;
        pub.retain = app.edit_pub_retain;
        g.pubs.push_back(pub);
    }
    return node.id;
}

std::vector<ng::PaletteEntry> palette_entries() {
    std::vector<ng::PaletteEntry> entries = {
        {kSubscribe, "订阅节点"},
        {kTable, "消息面板节点"},
        {kPublish, "发布节点"},
        {kPulse, "脉冲节点"},
        {kLog, "事件日志节点"},
    };
    if (!has_node_of_type(kBroker)) entries.insert(entries.begin(), {kBroker, "Broker 节点"});
    return entries;
}

// ---------------------------------------------------------------- 节点图持久化
std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t pos = text.find(separator, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

void build_default_graph(App& app) {
    g.nodes.clear();
    g.links.clear();
    g.subs.clear();
    g.pubs.clear();
    g.next_id = 1;

    const int broker = add_node(app, kBroker, ImVec2(40.0f, 60.0f));
    const int subscriber = add_node(app, kSubscribe, ImVec2(392.0f, 60.0f));
    const int table = add_node(app, kTable, ImVec2(704.0f, 24.0f));
    add_node(app, kPublish, ImVec2(392.0f, 258.0f));
    add_node(app, kPulse, ImVec2(60.0f, 380.0f));
    const int log = add_node(app, kLog, ImVec2(60.0f, 548.0f));

    // 默认只接数据流，会话连线留给用户拖（拖动即连接 broker）
    g.links.push_back({subscriber, 0, table, 0, ng::PinMessage});
    g.links.push_back({broker, 1, log, 0, ng::PinEvent});
}

bool valid_link(const ng::Link& link) {
    const ng::Node* from = find_node(link.from_node);
    const ng::Node* to = find_node(link.to_node);
    if (!from || !to) return false;
    if (link.from_pin < 0 || link.from_pin >= static_cast<int>(from->outputs.size())) return false;
    if (link.to_pin < 0 || link.to_pin >= static_cast<int>(to->inputs.size())) return false;
    return from->outputs[static_cast<size_t>(link.from_pin)].type ==
           to->inputs[static_cast<size_t>(link.to_pin)].type;
}

void load_graph(App& app) {
    std::string content;
    if (!read_whole_file(app_path(kGraphFile), content)) {
        build_default_graph(app);
        g.loaded = true;
        return;
    }

    g.nodes.clear();
    g.links.clear();
    g.subs.clear();
    g.pubs.clear();
    g.next_id = 1;

    struct RawNode { int id; int type; float x; float y; float w; float h; };
    std::vector<RawNode> raw_nodes;

    size_t start = 0;
    try {
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

        if (key == "view") {
            const auto parts = split(value, ',');
            if (parts.size() == 3) {
                g.canvas.apply_view(ImVec2(std::stof(parts[0]), std::stof(parts[1])), std::stof(parts[2]));
            }
        } else if (key == "node") {
            const auto parts = split(value, ',');
            if (parts.size() >= 6) {
                raw_nodes.push_back({std::stoi(parts[0]), std::stoi(parts[1]),
                                     std::stof(parts[2]), std::stof(parts[3]),
                                     std::stof(parts[4]), std::stof(parts[5])});
            } else if (parts.size() == 4) {
                raw_nodes.push_back({std::stoi(parts[0]), std::stoi(parts[1]),
                                     std::stof(parts[2]), std::stof(parts[3]), 0.0f, 0.0f});
            }
        } else if (key == "sub") {
            const auto parts = split(value, ',');
            if (parts.size() >= 3) {
                SubNode sub;
                sub.node_id = std::stoi(parts[0]);
                sub.qos = std::clamp(std::stoi(parts[1]), 0, 2);
                std::snprintf(sub.topic, sizeof sub.topic, "%s", unescape_field(parts[2]).c_str());
                g.subs.push_back(sub);
            }
        } else if (key == "pub") {
            const auto parts = split(value, ',');
            if (parts.size() >= 5) {
                PubNode pub;
                pub.node_id = std::stoi(parts[0]);
                pub.qos = std::clamp(std::stoi(parts[1]), 0, 2);
                pub.retain = parts[2] == "1";
                std::snprintf(pub.topic, sizeof pub.topic, "%s", unescape_field(parts[3]).c_str());
                std::snprintf(pub.payload, sizeof pub.payload, "%s", unescape_field(parts[4]).c_str());
                g.pubs.push_back(pub);
            }
        } else if (key == "link") {
            const auto parts = split(value, ',');
            if (parts.size() >= 5) {
                ng::Link link;
                link.from_node = std::stoi(parts[0]);
                link.from_pin = std::stoi(parts[1]);
                link.to_node = std::stoi(parts[2]);
                link.to_pin = std::stoi(parts[3]);
                link.type = std::stoi(parts[4]);
                g.links.push_back(link);
            }
        }
    }
    } catch (...) {
        // 图文件损坏：丢弃解析结果，走默认布局
        g.nodes.clear();
        g.links.clear();
        g.subs.clear();
        g.pubs.clear();
        raw_nodes.clear();
    }

    g.nodes.reserve(raw_nodes.size());
    for (const auto& raw : raw_nodes) {
        if (raw.type < 0 || raw.type >= kNodeTypeCount) continue;
        if (raw.type == kBroker && has_node_of_type(kBroker)) continue;
        const int id = add_node(app, raw.type, ImVec2(raw.x, raw.y), raw.id);
        if (id > 0 && raw.w > 0.0f && raw.h > 0.0f) {
            if (ng::Node* node = find_node(id)) node->size = ImVec2(raw.w, raw.h);
        }
    }

    g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                 [](const ng::Link& link) { return !valid_link(link); }),
                  g.links.end());
    // 同一节点只保留一份业务数据，防止旧图文件里的重复行越积越多
    g.subs.erase(std::unique(g.subs.begin(), g.subs.end(),
                             [](const SubNode& a, const SubNode& b) { return a.node_id == b.node_id; }),
                 g.subs.end());
    g.pubs.erase(std::unique(g.pubs.begin(), g.pubs.end(),
                             [](const PubNode& a, const PubNode& b) { return a.node_id == b.node_id; }),
                 g.pubs.end());
    g.subs.erase(std::remove_if(g.subs.begin(), g.subs.end(),
                               [](const SubNode& sub) { return find_node(sub.node_id) == nullptr; }),
                 g.subs.end());
    g.pubs.erase(std::remove_if(g.pubs.begin(), g.pubs.end(),
                               [](const PubNode& pub) { return find_node(pub.node_id) == nullptr; }),
                 g.pubs.end());

    if (g.nodes.empty()) build_default_graph(app);
    g.loaded = true;
}

void save_graph() {
    std::string content = "# mqtt_gui 节点图\n";

    char buffer[256];
    const ImVec2 pan = g.canvas.pan();
    std::snprintf(buffer, sizeof buffer, "view=%.1f,%.1f,%.3f\n", pan.x, pan.y, g.canvas.zoom());
    content += buffer;

    for (const auto& node : g.nodes) {
        std::snprintf(buffer, sizeof buffer, "node=%d,%d,%.0f,%.0f,%.0f,%.0f\n",
                      node.id, node.type, node.pos.x, node.pos.y, node.size.x, node.size.y);
        content += buffer;
    }
    for (const auto& sub : g.subs) {
        content += "sub=" + std::to_string(sub.node_id) + "," + std::to_string(sub.qos) + "," +
                   escape_field(sub.topic) + "\n";
    }
    for (const auto& pub : g.pubs) {
        content += "pub=" + std::to_string(pub.node_id) + "," + std::to_string(pub.qos) + "," +
                   (pub.retain ? "1" : "0") + "," + escape_field(pub.topic) + "," +
                   escape_field(pub.payload) + "\n";
    }
    for (const auto& link : g.links) {
        content += "link=" + std::to_string(link.from_node) + "," + std::to_string(link.from_pin) + "," +
                   std::to_string(link.to_node) + "," + std::to_string(link.to_pin) + "," +
                   std::to_string(link.type) + "\n";
    }

    write_whole_file(app_path(kGraphFile), content);
}

void ensure_graph(App& app) {
    if (g.loaded) return;
    load_graph(app);
}

// ---------------------------------------------------------------- 节点内容
// 节点内容区可用高度：节点总高减去标题、引脚行与内边距
float body_avail(const ng::Node& node, float zoom) {
    const int rows = static_cast<int>(std::max(node.inputs.size(), node.outputs.size()));
    return node.size.y * zoom -
           (ng::Canvas::kHeaderH + ng::Canvas::kPinRowH * static_cast<float>(rows) + 12.0f) * zoom;
}

void draw_broker_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    const bool session = ng::Canvas::has_link_of_type(g.links, ng::PinSession);

    ImGui::SetNextItemWidth(170.0f * zoom);
    ImGui::InputTextWithHint("##host", "broker 地址", app.edit_host, sizeof app.edit_host);
    ImGui::SameLine(0.0f, 6.0f * zoom);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##port", "端口", app.edit_port, sizeof app.edit_port,
                             ImGuiInputTextFlags_CharsDecimal);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##clientid", "client id", app.edit_client_id, sizeof app.edit_client_id);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##username", "用户名（可选）", app.edit_user, sizeof app.edit_user);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##password", "密码（可选）", app.edit_pass, sizeof app.edit_pass,
                             ImGuiInputTextFlags_Password);

    ImGui::SetNextItemWidth(62.0f * zoom);
    if (ImGui::InputInt("##keepalive", &app.keep_alive, 0, 0)) {
        app.keep_alive = std::clamp(app.keep_alive, 5, 3600);
    }
    ImGui::SameLine(0.0f, 8.0f * zoom);
    ImGui::Checkbox("清理会话", &app.clean_session);
    ImGui::SameLine(0.0f, 8.0f * zoom);
    ImGui::Checkbox("自动重连", &app.auto_reconnect);

    ImGui::TextColored(session ? to_vec(kSuccess) : to_vec(kMuted),
                       session ? "会话已连线" : "拖「会话」到订阅/发布即可连线");

    (void)node;
    (void)origin;
}

void draw_subscribe_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    SubNode* sub = find_sub(node.id);
    if (!sub) return;

    const bool linked = ng::Canvas::has_input_link(g.links, node.id, 0);

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##topic", "订阅过滤器，如 sensors/#", sub->topic, sizeof sub->topic);

    ImGui::SetNextItemWidth(84.0f * zoom);
    ImGui::Combo("##qos", &sub->qos, "QoS 0\0QoS 1\0QoS 2\0");
    ImGui::SameLine(0.0f, 8.0f * zoom);
    ImGui::TextColored(linked ? to_vec(kSuccess) : to_vec(kMuted),
                       linked ? "已生效" : "未接线");

    (void)app;
    (void)origin;
}

void draw_message_table(App& app, float zoom, float height) {
    const auto& view = app.store.view(app.filter_text, app.direction_filter);
    if (view.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f * zoom));
        ImGui::TextDisabled("暂无消息，等待 broker 推送");
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("##messages", 5, flags, ImVec2(0.0f, height))) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("时间", ImGuiTableColumnFlags_WidthFixed, 86.0f * zoom);
    ImGui::TableSetupColumn("方向", ImGuiTableColumnFlags_WidthFixed, 44.0f * zoom);
    ImGui::TableSetupColumn("主题", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableSetupColumn("长度", ImGuiTableColumnFlags_WidthFixed, 52.0f * zoom);
    ImGui::TableSetupColumn("负载", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view.size()), ImGui::GetTextLineHeightWithSpacing());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const MessageEntry& entry = app.store.at(view[static_cast<size_t>(row)]);
            ImGui::PushID(static_cast<const void*>(&entry));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const bool selected = (app.selected_seq == entry.seq);
            if (ImGui::Selectable(entry.clock.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                app.selected_seq = selected ? 0 : entry.seq;

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(to_vec(entry.outbound ? kSuccess : kAccent),
                               "%s", entry.outbound ? "发送" : "接收");

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(entry.topic.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%u", static_cast<unsigned>(entry.size));

            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("%s", entry.preview.c_str());

            ImGui::PopID();
        }
    }
    clipper.End();

    if (app.auto_scroll && app.store.version() != app.last_store_version)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndTable();
}

void draw_table_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    const bool linked = ng::Canvas::has_input_link(g.links, node.id, 0);

    ImGui::SetNextItemWidth(168.0f * zoom);
    ImGui::InputTextWithHint("##filter", "过滤主题 / 内容", app.filter_text, sizeof app.filter_text);
    ImGui::SameLine(0.0f, 6.0f * zoom);
    ImGui::SetNextItemWidth(76.0f * zoom);
    ImGui::Combo("##direction", &app.direction_filter, "全部\0接收\0发送\0");
    ImGui::SameLine(0.0f, 10.0f * zoom);
    ImGui::Checkbox("跟随最新", &app.auto_scroll);
    ImGui::SameLine(0.0f, 12.0f * zoom);
    ImGui::TextDisabled("%zu / %zu 条", app.store.size(), MessageStore::kCapacity);

    const float consumed = ImGui::GetCursorScreenPos().y - origin.y;
    const float detail_height = (app.selected_seq != 0) ? 96.0f * zoom : 0.0f;
    const float table_height = body_avail(node, zoom) - consumed - detail_height - 6.0f * zoom;

    if (!linked) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f * zoom));
        ImGui::TextDisabled("把「消息」引脚拖进来，这里才会显示实时消息");
        return;
    }

    draw_message_table(app, zoom, table_height > 60.0f * zoom ? table_height : 60.0f * zoom);
    app.last_store_version = app.store.version();

    if (app.selected_seq != 0) {
        const MessageEntry* entry = app.store.find(app.selected_seq);
        if (!entry) {
            app.selected_seq = 0;
            return;
        }

        ImGui::BeginChild("##detail", ImVec2(0.0f, detail_height), ImGuiChildFlags_Borders);
        ImGui::PushStyleColor(ImGuiCol_Text, to_vec(pal::TextPin));
        ImGui::TextUnformatted(entry->topic.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 10.0f * zoom);
        ImGui::TextDisabled("%s · QoS %u · %u 字节%s", entry->outbound ? "发送" : "接收",
                            static_cast<unsigned>(entry->qos), static_cast<unsigned>(entry->size),
                            entry->truncated ? " · 已截断" : "");

        const float copy_width = 52.0f * zoom;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - copy_width);
        if (ImGui::SmallButton("复制")) ImGui::SetClipboardText(entry->payload.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, to_vec(pal::TextValue));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(entry->payload.data(), entry->payload.data() + entry->payload.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }
}

void draw_publish_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    PubNode* pub = find_pub(node.id);
    if (!pub) return;

    const bool session = ng::Canvas::has_input_link(g.links, node.id, 0);
    const bool trigger = ng::Canvas::has_input_link(g.links, node.id, 1);
    const bool ready = session && trigger;

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ptopic", "发布主题", pub->topic, sizeof pub->topic);

    const float payload_height =
        std::max(52.0f * zoom, body_avail(node, zoom) - 82.0f * zoom);
    ImGui::InputTextMultiline("##ppayload", pub->payload, sizeof pub->payload,
                              ImVec2(-FLT_MIN, payload_height), ImGuiInputTextFlags_AllowTabInput);

    ImGui::SetNextItemWidth(84.0f * zoom);
    ImGui::Combo("##pqos", &pub->qos, "QoS 0\0QoS 1\0QoS 2\0");
    ImGui::SameLine(0.0f, 8.0f * zoom);
    ImGui::Checkbox("保留", &pub->retain);
    ImGui::SameLine(0.0f, 10.0f * zoom);
    ImGui::TextColored(ready ? to_vec(kSuccess) : to_vec(kMuted),
                       ready ? "就绪，等脉冲" : "需会话 + 触发");

    (void)app;
    (void)origin;
}

void draw_pulse_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    const float diameter = 58.0f * zoom;
    const ImVec2 pad_pos(origin.x + 4.0f * zoom, origin.y);

    ImGui::SetCursorScreenPos(pad_pos);
    ImGui::InvisibleButton("##pulse", ImVec2(diameter, diameter));
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = ImGui::IsItemClicked();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(pad_pos.x + diameter * 0.5f, pad_pos.y + diameter * 0.5f);
    const ImU32 color = IM_COL32(255, 156, 92, 255);

    const double now = ImGui::GetTime();
    const float since_flash = static_cast<float>(now - g_flash_time);
    draw->AddCircleFilled(center, diameter * 0.5f, (color & 0x00FFFFFFu) | (26u << IM_COL32_A_SHIFT), 28);
    draw->AddCircle(center, diameter * 0.48f, (color & 0x00FFFFFFu) | (90u << IM_COL32_A_SHIFT), 28,
                    1.4f * zoom);
    draw->AddCircleFilled(center, diameter * (pressed ? 0.30f : 0.34f),
                          hovered ? color : (color & 0x00FFFFFFu) | (210u << IM_COL32_A_SHIFT), 28);

    if (since_flash < 0.45f) {
        const float ring = diameter * (0.36f + since_flash * 0.5f);
        const int alpha = static_cast<int>(200.0f * (1.0f - since_flash / 0.45f));
        draw->AddCircle(center, ring, (color & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT),
                        32, 2.0f * zoom);
    }

    const char* caption = "点击";
    const ImVec2 caption_size = ImGui::CalcTextSize(caption);
    draw->AddText(ImVec2(center.x - caption_size.x * 0.5f, center.y - caption_size.y * 0.5f),
                  IM_COL32(24, 18, 10, 255), caption);

    ImGui::SetCursorScreenPos(ImVec2(pad_pos.x + diameter + 14.0f * zoom, origin.y + 12.0f * zoom));
    ImGui::TextDisabled("点一下发一次脉冲");
    ImGui::SetCursorScreenPos(ImVec2(pad_pos.x + diameter + 14.0f * zoom, origin.y + 34.0f * zoom));
    ImGui::TextDisabled("连到发布节点的「触发」");

    if (pressed) {
        g.pulse_fired = true;
        g_flash_time = now;
    }

    (void)node;
    (void)app;
}

void draw_log_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    const bool linked = ng::Canvas::has_input_link(g.links, node.id, 0);
    const float consumed = ImGui::GetCursorScreenPos().y - origin.y;
    const float height = body_avail(node, zoom) - consumed - 4.0f * zoom;

    ImGui::BeginChild("##log", ImVec2(0.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (app.event_log.empty()) {
        ImGui::TextDisabled(linked ? "暂无事件" : "把「事件」引脚拖进来");
    } else {
        for (const auto& line : app.event_log) {
            ImGui::TextDisabled("%s", line.clock.c_str());
            ImGui::SameLine(0.0f, 6.0f * zoom);
            ImGui::TextColored(line.error ? to_vec(kDanger) : to_vec(pal::TextSub),
                               "%s", line.text.c_str());
        }
        if (app.event_log.size() != g.log_seen) {
            ImGui::SetScrollHereY(1.0f);
            g.log_seen = app.event_log.size();
        }
    }
    ImGui::EndChild();
}

void draw_body(App& app, ng::Node& node, const ImVec2& origin, float zoom) {
    // 节点与内嵌控件统一底色 #181825，靠描边区分层次
    ImGui::PushStyleColor(ImGuiCol_FrameBg, to_vec(IM_COL32(24, 24, 37, 255)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, to_vec(IM_COL32(30, 30, 46, 255)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, to_vec(IM_COL32(38, 38, 58, 255)));
    ImGui::PushStyleColor(ImGuiCol_Border, to_vec(IM_COL32(49, 50, 68, 255)));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, to_vec(IM_COL32(24, 24, 37, 255)));
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, to_vec(IM_COL32(30, 30, 46, 255)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

    switch (node.type) {
        case kBroker:    draw_broker_body(app, node, origin, zoom); break;
        case kSubscribe: draw_subscribe_body(app, node, origin, zoom); break;
        case kTable:     draw_table_body(app, node, origin, zoom); break;
        case kPublish:   draw_publish_body(app, node, origin, zoom); break;
        case kPulse:     draw_pulse_body(app, node, origin, zoom); break;
        case kLog:       draw_log_body(app, node, origin, zoom); break;
        default: break;
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);
}

// ---------------------------------------------------------------- 连线 -> 功能
void sync_business(App& app) {
    // 清理已被删除的节点留下的业务数据
    g.subs.erase(std::remove_if(g.subs.begin(), g.subs.end(),
                                [](const SubNode& sub) { return find_node(sub.node_id) == nullptr; }),
                 g.subs.end());
    g.pubs.erase(std::remove_if(g.pubs.begin(), g.pubs.end(),
                                [](const PubNode& pub) { return find_node(pub.node_id) == nullptr; }),
                 g.pubs.end());

    int broker_id = 0;
    for (const auto& node : g.nodes) {
        if (node.type == kBroker) {
            broker_id = node.id;
            break;
        }
    }

    const mqtt::State state = app.client.state();
    const char* state_text = "未连接";
    if (state == mqtt::State::Connected) state_text = "已连接";
    else if (state == mqtt::State::Connecting) state_text = "连接中";

    bool session_linked = false;
    for (const auto& link : g.links) {
        if (link.type != ng::PinSession) continue;
        if (broker_id != 0 && link.from_node != broker_id) continue;
        session_linked = true;
        break;
    }

    for (auto& node : g.nodes) {
        if (node.type == kBroker) node.subtitle = state_text;
    }

    // 连线即连接：有会话连线就保持连接，断开全部连线即断开
    const double now = ImGui::GetTime();
    if (session_linked && state == mqtt::State::Disconnected) {
        if (now - g.last_connect_attempt > 2.5) {
            g.last_connect_attempt = now;
            app.connect_now();
        }
    } else if (!session_linked && state != mqtt::State::Disconnected) {
        app.disconnect_now();
    }

    std::vector<Subscription> wanted;
    for (auto& node : g.nodes) {
        if (node.type != kSubscribe) continue;
        SubNode* sub = find_sub(node.id);
        const bool linked = ng::Canvas::has_input_link(g.links, node.id, 0);
        node.subtitle = linked ? "已订阅" : "未接线";
        if (!sub || sub->topic[0] == '\0' || !linked) continue;
        wanted.push_back({sub->topic, sub->qos});
    }
    // 正在输入时不重订，避免每敲一个字符就发一次 SUBSCRIBE
    if (!ImGui::GetIO().WantTextInput) app.sync_subscriptions(wanted);

    if (g.pulse_fired) {
        for (const auto& node : g.nodes) {
            if (node.type != kPublish) continue;
            const PubNode* pub = find_pub(node.id);
            if (!pub) continue;
            if (!ng::Canvas::has_input_link(g.links, node.id, 0)) continue;
            if (!ng::Canvas::has_input_link(g.links, node.id, 1)) continue;
            app.publish_message(pub->topic, pub->payload, pub->qos, pub->retain);
        }
    }
}

// ---------------------------------------------------------------- 状态条
void draw_status_bar(App& app, float zoom) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, to_vec(IM_COL32(0x1E, 0x1E, 0x2E, 255)));
    ImGui::BeginChild("##status", ImVec2(0.0f, 30.0f * zoom), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(12.0f * zoom, 5.0f * zoom));

    const mqtt::State state = app.client.state();
    ImU32 color = kDanger;
    const char* label = "未连接";
    if (state == mqtt::State::Connected) {
        color = kSuccess;
        label = "已连接";
    } else if (state == mqtt::State::Connecting) {
        color = kWarning;
        label = "连接中";
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 center(cursor.x + 5.0f * zoom, cursor.y + ImGui::GetTextLineHeight() * 0.5f);
    draw->AddCircleFilled(center, 4.0f * zoom, color, 16);
    draw->AddCircle(center, 7.5f * zoom, (color & 0x00FFFFFFu) | (55u << IM_COL32_A_SHIFT), 18, 1.4f * zoom);

    ImGui::Dummy(ImVec2(16.0f * zoom, 0.0f));
    ImGui::SameLine();
    ImGui::TextColored(to_vec(color), "%s", label);
    ImGui::SameLine(0.0f, 14.0f * zoom);
    ImGui::TextDisabled("%s : %d", app.host.c_str(), app.port);

    const mqtt::Stats stats = app.client.stats();
    char right[192];
    std::snprintf(right, sizeof right, "收 %llu  发 %llu  报文 %llu / %llu  缓存 %zu / %zu",
                  static_cast<unsigned long long>(stats.rx_messages),
                  static_cast<unsigned long long>(stats.tx_packets),
                  static_cast<unsigned long long>(stats.rx_packets),
                  static_cast<unsigned long long>(stats.tx_packets), app.store.size(),
                  MessageStore::kCapacity);
    const float right_width = ImGui::CalcTextSize(right).x;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float right_x = ImGui::GetCursorPosX() + avail.x - right_width - 14.0f * zoom;

    if (!app.hint.empty()) {
        const float hint_x = ImGui::GetCursorPosX() + 18.0f * zoom;
        if (hint_x + ImGui::CalcTextSize(app.hint.c_str()).x < right_x - 12.0f * zoom) {
            ImGui::SameLine(hint_x);
            ImGui::TextColored(app.hint_error ? to_vec(kDanger) : to_vec(kMuted), "%s", app.hint.c_str());
        }
    }

    ImGui::SameLine(right_x);
    ImGui::TextDisabled("%s", right);

    ImGui::EndChild();
}

}  // namespace

// ---------------------------------------------------------------- 对外接口

bool init(float dpi_scale, const std::string& font_file, float font_size) {
    g_scale = dpi_scale > 0.5f ? dpi_scale : 1.0f;
    apply_style(g_scale);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const float base_size = (font_size >= 12.0f && font_size <= 28.0f) ? font_size : 17.0f;
    const std::string font_path = resolve_font_path(font_file);
    if (!font_path.empty()) {
        ImFontConfig config;
        config.PixelSnapH = true;
        config.OversampleH = 2;
        // 1.92+ 后端支持动态字形图集，中文按需光栅化，无需预置字形范围
        if (io.Fonts->AddFontFromFileTTF(font_path.c_str(), base_size * g_scale, &config) != nullptr)
            return true;
    }

    io.Fonts->AddFontDefault();
    return false;
}

void draw(App& app) {
    ensure_graph(app);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar();
    if (visible) {
        draw_status_bar(app, g_scale);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        g.pulse_fired = false;

        const ng::Requests requests = g.canvas.draw(
            "##canvas", avail, g.nodes, g.links, palette_entries(),
            [&app](ng::Node& node, const ImVec2& origin, float zoom) {
                draw_body(app, node, origin, zoom);
            });

        if (requests.changed) g.dirty = true;
        if (requests.add_type >= 0) {
            add_node(app, requests.add_type, requests.add_pos);
            g.dirty = true;
        }

        sync_business(app);

        if (g.dirty && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            save_graph();
            g.dirty = false;
        }
    }
    ImGui::End();
}

void shutdown() {
    if (g.loaded) save_graph();
}

}  // namespace ui
