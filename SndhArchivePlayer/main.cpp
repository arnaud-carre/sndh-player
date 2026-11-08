//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#include "SndhArchivePlayer.h"
#include "CuiGlfw.h"
#include <cui/cui_render.h>
#include <stdio.h>
#include <vector>
#include <algorithm>

static SndhArchivePlayer gApp;

static void GlfwErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static void DropFilesCallback(GLFWwindow*, int count, const char** paths)
{
    if (count == 0)
        return;

#ifdef _WIN32
    // GLFW supplies UTF-8; the existing Windows file loader uses ANSI paths.
    const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, paths[0], -1, nullptr, 0);
    if (wideLength == 0)
        return;
    std::vector<wchar_t> widePath(wideLength);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, paths[0], -1, widePath.data(), wideLength))
        return;
    const int pathLength = WideCharToMultiByte(CP_ACP, 0, widePath.data(), -1, nullptr, 0, nullptr, nullptr);
    if (pathLength == 0)
        return;
    std::vector<char> path(pathLength);
    if (WideCharToMultiByte(CP_ACP, 0, widePath.data(), -1, path.data(), pathLength, nullptr, nullptr))
        gApp.DropFile(path.data());
#else
    gApp.DropFile(paths[0]);
#endif
}

static void FitStartupWindow(GLFWwindow* window)
{
    int x, y, width, height;
    glfwGetWindowPos(window, &x, &y);
    glfwGetWindowSize(window, &width, &height);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    long long largestOverlap = 0;
    for (int i = 0; i < count; ++i)
    {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        const int overlapWidth = (std::max)(0, (std::min)(x + width, mx + mw) - (std::max)(x, mx));
        const int overlapHeight = (std::max)(0, (std::min)(y + height, my + mh) - (std::max)(y, my));
        const long long overlap = static_cast<long long>(overlapWidth) * overlapHeight;
        if (overlap > largestOverlap)
        {
            largestOverlap = overlap;
            monitor = monitors[i];
        }
    }
    if (!monitor) return;
    int workX, workY, workWidth, workHeight;
    glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);
    int left, top, right, bottom;
    glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
    const int availableWidth = (std::max)(1, workWidth - left - right);
    const int availableHeight = (std::max)(1, workHeight - top - bottom);
    width = (std::min)(width, availableWidth);
    height = (std::min)(height, availableHeight);
    glfwSetWindowSizeLimits(window, (std::min)(720, availableWidth), (std::min)(640, availableHeight),
        GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(window, width, height);
    glfwSetWindowPos(window, workX + left + (availableWidth - width) / 2,
        workY + top + (availableHeight - height) / 2);
}

int main()
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1060, 900, "SNDH & YM Archive Player v" SNDH_ARCHIVE_PLAYER_VERSION, nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    FitStartupWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    cui_gl_renderer* renderer = cui_gl_create([](const char* name) -> void* { return reinterpret_cast<void*>(glfwGetProcAddress(name)); });
    if (!renderer)
    {
        fprintf(stderr, "Unable to initialize the CUI OpenGL renderer.\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    cui_ctx_desc desc = {};
    desc.renderer = cui_gl_iface(renderer);
    desc.theme = CUI_THEME_LIGHT;
    desc.accent = CUI_ACCENT_GREEN;
    cui_ctx* ui = cui_create(&desc);
    if (!ui)
    {
        cui_gl_destroy(renderer);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    {
        CuiGlfw platform(window, ui);
        glfwSetDropCallback(window, DropFilesCallback);
        gApp.Startup(ui);
        glfwShowWindow(window);
        double previousTime = glfwGetTime();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            if (glfwWindowShouldClose(window)) break;

            // Playback and job completion advance independently of visible panels.
            gApp.Update();
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || width == 0 || height == 0)
            {
                glfwWaitEventsTimeout(0.032);
                previousTime = glfwGetTime();
                continue;
            }
            platform.UpdateViewport();
            const double now = glfwGetTime();
            cui_begin_frame(ui, float(now - previousTime));
            previousTime = now;
            gApp.DrawGui(ui);
            cui_end_frame(ui);
            cui_gl_render(renderer, cui_get_draw_data(ui), true);
            glfwSwapBuffers(window);
            // Oscilloscopes need regular frames during playback; sleep when idle.
            if (!gApp.NeedsContinuousFrames() && !cui_needs_redraw(ui))
            {
                const float wakeup = cui_next_wakeup(ui);
                glfwWaitEventsTimeout(wakeup > 0 && wakeup < 0.1f ? wakeup : 0.1);
            }
        }
        gApp.Shutdown(ui);
    }
    cui_destroy(ui);
    cui_gl_destroy(renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
