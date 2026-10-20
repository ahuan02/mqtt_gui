#include "app/app.h"
#include "ui/ui.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>
#include <cstdio>

namespace {

void on_glfw_error(int code, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, description ? description : "");
}

void center_window(GLFWwindow* window) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;

    int area_x = 0, area_y = 0, area_w = 0, area_h = 0;
    glfwGetMonitorWorkarea(monitor, &area_x, &area_y, &area_w, &area_h);

    int width = 0, height = 0;
    glfwGetWindowSize(window, &width, &height);
    glfwSetWindowPos(window, area_x + (area_w - width) / 2, area_y + (area_h - height) / 2);
}

void enable_dark_title_bar(GLFWwindow* window) {
#ifdef _WIN32
    HWND handle = glfwGetWin32Window(window);
    if (!handle) return;

    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(handle, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof dark);

    // 标题栏与窗体同色 #1e1e2e，边框略暗收边
    const COLORREF caption = RGB(30, 30, 46);
    ::DwmSetWindowAttribute(handle, 34 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    const COLORREF border = RGB(24, 24, 37);
    ::DwmSetWindowAttribute(handle, 35 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
#else
    (void)window;
#endif
}

}  // namespace

int main(int, char**) {
    glfwSetErrorCallback(on_glfw_error);
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);

    // 配置要在建窗口之前读：恢复上次的尺寸与位置
    App app;
    app.init();

    GLFWwindow* window = glfwCreateWindow(app.window_w, app.window_h, "MQTT 调试面板", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwSetWindowSizeLimits(window, 1060, 660, GLFW_DONT_CARE, GLFW_DONT_CARE);
    if (app.window_x >= 0 && app.window_y >= 0) {
        glfwSetWindowPos(window, app.window_x, app.window_y);
    } else {
        center_window(window);
    }
    if (app.window_maximized) glfwMaximizeWindow(window);
    enable_dark_title_bar(window);

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    float scale_x = 1.0f, scale_y = 1.0f;
    glfwGetWindowContentScale(window, &scale_x, &scale_y);
    const float dpi_scale = std::clamp(std::max(scale_x, scale_y), 1.0f, 2.0f);

    ui::init(dpi_scale, app.font_file, app.font_size);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
#ifdef _WIN32
            ::Sleep(30);
#endif
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        app.tick();
        ui::draw(app);

        ImGui::Render();

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(30.0f / 255.0f, 30.0f / 255.0f, 46.0f / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 先取回窗口几何再关：最大化时保留最大化前的宽高
    const bool maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
    app.window_maximized = maximized;
    if (!maximized) {
        glfwGetWindowSize(window, &app.window_w, &app.window_h);
        glfwGetWindowPos(window, &app.window_x, &app.window_y);
    }

    ui::shutdown();
    app.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
