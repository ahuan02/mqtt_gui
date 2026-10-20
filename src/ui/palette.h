#pragma once

#include "imgui.h"

// 全局取色板：UI 层与节点画布共用，避免颜色散落各处
// 基调 Catppuccin Mocha（配 #1e1e2e 窗体）
namespace pal {

// ---- 文字
constexpr ImU32 Text      = IM_COL32(0xCD, 0xD6, 0xF4, 255);  // 正文
constexpr ImU32 TextSub   = IM_COL32(0xA6, 0xAD, 0xC8, 255);  // 次要说明
constexpr ImU32 TextMuted = IM_COL32(0x6C, 0x70, 0x86, 255);  // 占位、禁用
constexpr ImU32 TextPin   = IM_COL32(0xBA, 0xC2, 0xDE, 255);  // 引脚标签
constexpr ImU32 TextValue = IM_COL32(0xDD, 0xE4, 0xFF, 255);  // 负载、主题等值

// ---- 状态色
constexpr ImU32 Accent  = IM_COL32(0x89, 0xB4, 0xFA, 255);    // 蓝
constexpr ImU32 Success = IM_COL32(0xA6, 0xE3, 0xA1, 255);    // 绿
constexpr ImU32 Warning = IM_COL32(0xF9, 0xE2, 0xAF, 255);    // 黄
constexpr ImU32 Danger  = IM_COL32(0xF3, 0x8B, 0xA8, 255);    // 红
constexpr ImU32 Mauve   = IM_COL32(0xCB, 0xA6, 0xF7, 255);    // 紫
constexpr ImU32 Sky     = IM_COL32(0x89, 0xDC, 0xEB, 255);    // 青
constexpr ImU32 Peach   = IM_COL32(0xFA, 0xB3, 0x87, 255);    // 橙
constexpr ImU32 Lavender = IM_COL32(0xB4, 0xBE, 0xFE, 255);   // 淡紫

// ---- 表面
constexpr ImU32 Canvas      = IM_COL32(0x1E, 0x1E, 0x2E, 255);  // 窗体/画布
constexpr ImU32 Surface     = IM_COL32(0x23, 0x23, 0x36, 255);  // 弹层
constexpr ImU32 NodeBody    = IM_COL32(0x18, 0x18, 0x25, 255);  // 节点与内嵌控件
constexpr ImU32 NodeHover   = IM_COL32(0x1E, 0x1E, 0x2E, 255);
constexpr ImU32 NodeActive  = IM_COL32(0x26, 0x26, 0x3A, 255);
constexpr ImU32 Line        = IM_COL32(0x31, 0x32, 0x44, 255);  // 描边
constexpr ImU32 Hole        = IM_COL32(0x11, 0x11, 0x1B, 255);  // 未连接引脚
constexpr ImU32 GridDot     = IM_COL32(255, 255, 255, 14);
constexpr ImU32 Shadow      = IM_COL32(0, 0, 0, 90);
constexpr ImU32 Selection   = IM_COL32(0x3F, 0x71, 0xC8, 115);

}  // namespace pal
