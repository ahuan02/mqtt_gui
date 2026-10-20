#pragma once

#include <string>

class App;

namespace ui {

// 初始化字体与主题；font_file 留空表示自动扫描 fonts 目录
// 返回 false 表示中文字体缺失，已退回内置字体
bool init(float dpi_scale, const std::string& font_file = {}, float font_size = 17.0f);

// 每帧绘制整个界面
void draw(App& app);

// 退出前落盘节点图
void shutdown();

}
