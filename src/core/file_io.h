#pragma once

#include <string>

// exe 所在目录（字体、配置、节点图都以它为基准）
std::string executable_directory();

// 以 exe 目录为基准拼路径
std::string app_path(const char* filename);

bool read_whole_file(const std::string& path, std::string& content);
bool write_whole_file(const std::string& path, const std::string& content);

// 单行文本字段转义（\n 与 \\），用于配置/节点图落盘
std::string escape_field(const std::string& value);
std::string unescape_field(const std::string& value);
