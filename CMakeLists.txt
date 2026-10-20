cmake_minimum_required(VERSION 3.20)
project(mqtt_gui LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# 体积控制：函数/数据分段 + 链接期回收未引用段 + LTO
# ---------------------------------------------------------------------------
if (NOT MSVC)
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()
# 注意：本机 MinGW 未附带 LTO 链接插件，故不启用 -flto；
# -O3 + --gc-sections + -s 已能把体积压到位。

# ---------------------------------------------------------------------------
# GLFW：只编译 Win32 平台源码，示例/测试/文档/安装全部关闭
# ---------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11      OFF CACHE BOOL "" FORCE)
add_subdirectory(res/glfw EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Dear ImGui：只编译核心 4 个 + 用到的那 2 个后端（不进 demo/examples/misc）
# ---------------------------------------------------------------------------
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res/imgui")

add_library(imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui SYSTEM PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_compile_definitions(imgui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------
# 主程序
# ---------------------------------------------------------------------------
# 窗口/任务栏/exe 图标：编译成资源段嵌进 exe（GLFW 按资源名 GLFW_ICON 加载）
set(ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/res/icon/icon.ico")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/res/icon/app.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc" @ONLY)
set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc" PROPERTIES
    OBJECT_DEPENDS "${ICON_FILE}")

add_executable(mqtt_gui
    src/main.cpp
    src/core/file_io.cpp
    src/core/mqtt_client.cpp
    src/app/app.cpp
    src/ui/node_graph.cpp
    src/ui/ui.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
)
target_include_directories(mqtt_gui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mqtt_gui PRIVATE imgui)

if (WIN32)
    target_link_libraries(mqtt_gui PRIVATE opengl32 ws2_32 dwmapi)
endif()

if (MSVC)
    target_compile_options(mqtt_gui PRIVATE /utf-8 /W4)
else()
    target_compile_options(mqtt_gui PRIVATE -Wall -Wextra -Wno-unused-parameter)
    # 静态链入 libstdc++/libgcc + 去符号表 => 单文件 exe
    # （不使用 -static：本机 MinGW 的 libpthread.a 缺 UCRT 符号，
    #   代码里已改用 Win32 线程/锁，不再依赖 libwinpthread）
    target_link_options(mqtt_gui PRIVATE -static-libgcc -static-libstdc++ -s)
endif()

option(MQTT_GUI_CONSOLE "保留控制台窗口（调试用）" OFF)
if (NOT MQTT_GUI_CONSOLE)
    set_target_properties(mqtt_gui PROPERTIES WIN32_EXECUTABLE ON)
endif()

# 运行期只需要：exe + 中文字体 + 一个 49KB 的 winpthread（libstdc++ 静态库的异常处理依赖它）
get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(MINGW_WINPTHREAD_DLL "${MINGW_BIN_DIR}/libwinpthread-1.dll")
if (NOT EXISTS "${MINGW_WINPTHREAD_DLL}")
    set(MINGW_WINPTHREAD_DLL "")
endif()

# 整个字体目录一起复制：换字体只要往 res/fonts 丢文件，无需改 CMake
add_custom_command(TARGET mqtt_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mqtt_gui>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts"
            "$<TARGET_FILE_DIR:mqtt_gui>/fonts"
    COMMENT "Copying fonts to output directory"
)

if (MINGW_WINPTHREAD_DLL)
    add_custom_command(TARGET mqtt_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MINGW_WINPTHREAD_DLL}"
                "$<TARGET_FILE_DIR:mqtt_gui>/libwinpthread-1.dll"
        COMMENT "Copying winpthread runtime"
    )
endif()

# ---------------------------------------------------------------------------
# 发布包：只包含运行必需的文件
# ---------------------------------------------------------------------------
install(TARGETS mqtt_gui RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/" DESTINATION fonts)
if (MINGW_WINPTHREAD_DLL)
    install(FILES "${MINGW_WINPTHREAD_DLL}" DESTINATION .)
endif()
