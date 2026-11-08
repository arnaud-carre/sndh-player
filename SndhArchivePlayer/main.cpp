//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
#include "SndhArchivePlayer.h"
#include "CuiGlfw.h"
#include "AppData.h"
#include <cui/cui_render.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
#include <sstream>
#ifdef _WIN32
#include <shellapi.h>
#endif

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

static void FitStartupWindow(GLFWwindow* window, GLFWwindow* beside = nullptr, int minWidth = 720, int minHeight = 640)
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
    glfwSetWindowSizeLimits(window, (std::min)(minWidth, availableWidth), (std::min)(minHeight, availableHeight),
        GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(window, width, height);
    int targetX = workX + left + (availableWidth - width) / 2;
    int targetY = workY + top + (availableHeight - height) / 2;
    if (beside)
    {
        int bx, by, bw, bh, bl, bt, br, bb;
        glfwGetWindowPos(beside, &bx, &by);
        glfwGetWindowSize(beside, &bw, &bh);
        glfwGetWindowFrameSize(beside, &bl, &bt, &br, &bb);
        targetX = bx + bw + br + left + 8;
        if (targetX + width + right > workX + workWidth)
            targetX = bx - bl - right - width - 8;
        targetX = (std::max)(workX + left, (std::min)(targetX, workX + left + availableWidth - width));
        targetY = (std::max)(workY + top, (std::min)(by, workY + top + availableHeight - height));
    }
    glfwSetWindowPos(window, targetX, targetY);
}

class DataViewerWindow
{
public:
    ~DataViewerWindow() { Close(); }

    void Render(GLFWwindow* mainWindow, cui_ctx* mainUi)
    {
        if (m_window && glfwWindowShouldClose(m_window)) gApp.CloseDataViewer();
        if (!gApp.IsDataViewerOpen())
        {
            Close();
            return;
        }
        if (!m_window && !Open(mainWindow, mainUi))
        {
            gApp.CloseDataViewer();
            return;
        }
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) || width == 0 || height == 0) return;
        glfwMakeContextCurrent(m_window);
        m_platform->UpdateViewport();
        cui_set_theme(m_ui, cui_get_theme_mode(mainUi));
        const double now = glfwGetTime();
        cui_begin_frame(m_ui, float(now - m_previousTime));
        m_previousTime = now;
        gApp.DrawDataGui(m_ui);
        cui_end_frame(m_ui);
        cui_gl_render(m_renderer, cui_get_draw_data(m_ui), true);
        glfwSwapBuffers(m_window);
        glfwMakeContextCurrent(mainWindow);
    }

private:
    bool Open(GLFWwindow* mainWindow, cui_ctx* mainUi)
    {
        m_window = glfwCreateWindow(760, 600, "File Viewer - SNDH & YM Archive Player", nullptr, nullptr);
        if (!m_window) return false;
        int x, y;
        glfwGetWindowPos(mainWindow, &x, &y);
        glfwSetWindowPos(m_window, x, y);
        FitStartupWindow(m_window, mainWindow, 360, 240);
        glfwMakeContextCurrent(m_window);
        glfwSwapInterval(0);
        m_renderer = cui_gl_create([](const char* name) -> void* { return reinterpret_cast<void*>(glfwGetProcAddress(name)); });
        if (m_renderer)
        {
            cui_ctx_desc desc = {};
            desc.renderer = cui_gl_iface(m_renderer);
            desc.theme = cui_get_theme_mode(mainUi);
            desc.accent = CUI_ACCENT_GREEN;
            m_ui = cui_create(&desc);
        }
        if (!m_ui)
        {
            Close();
            glfwMakeContextCurrent(mainWindow);
            return false;
        }
        m_platform = std::make_unique<CuiGlfw>(m_window, m_ui);
        m_previousTime = glfwGetTime();
        glfwShowWindow(m_window);
        glfwMakeContextCurrent(mainWindow);
        return true;
    }

    void Close()
    {
        if (!m_window) return;
        GLFWwindow* previous = glfwGetCurrentContext();
        glfwMakeContextCurrent(m_window);
        m_platform.reset();
        if (m_ui) cui_destroy(m_ui);
        if (m_renderer) cui_gl_destroy(m_renderer);
        glfwDestroyWindow(m_window);
        glfwMakeContextCurrent(previous == m_window ? nullptr : previous);
        m_window = nullptr;
        m_ui = nullptr;
        m_renderer = nullptr;
    }

    GLFWwindow* m_window = nullptr;
    cui_ctx* m_ui = nullptr;
    cui_gl_renderer* m_renderer = nullptr;
    std::unique_ptr<CuiGlfw> m_platform;
    double m_previousTime = 0;
};

static std::string StartupFileArgument(int argc, char** argv)
{
#ifdef _WIN32
    // Read UTF-16 arguments directly, then use the same native encoding as
    // file drops and the existing Windows file/archive loaders.
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::string path;
    if (args && count > 1)
    {
        const int length = WideCharToMultiByte(CP_ACP, 0, args[1], -1, nullptr, 0, nullptr, nullptr);
        if (length > 0)
        {
            std::vector<char> buffer(length);
            if (WideCharToMultiByte(CP_ACP, 0, args[1], -1, buffer.data(), length, nullptr, nullptr))
                path = buffer.data();
        }
    }
    if (args) LocalFree(args);
    return path;
#else
    // Finder may supply its legacy process serial number when launching a bundle.
    for (int i = 1; i < argc; ++i)
        if (strncmp(argv[i], "-psn_", 5) != 0) return argv[i];
    return {};
#endif
}

int main(int argc, char** argv)
{
    const std::string startupFile = StartupFileArgument(argc, argv);
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
    char savedSize[64];
    int normalWidth, normalHeight;
    if (LoadAppData("WindowSize", savedSize, sizeof(savedSize)) &&
        (std::istringstream(savedSize) >> normalWidth >> normalHeight) &&
        normalWidth > 0 && normalHeight > 0 && normalWidth <= 32768 && normalHeight <= 32768)
        glfwSetWindowSize(window, normalWidth, normalHeight);
    FitStartupWindow(window);
    glfwGetWindowSize(window, &normalWidth, &normalHeight);
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
        DataViewerWindow dataViewer;
        glfwSetDropCallback(window, DropFilesCallback);
        gApp.Startup(ui, startupFile.c_str());
        glfwShowWindow(window);
        double previousTime = glfwGetTime();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            // Keep the restored size when the window is maximized or minimized.
            if (!glfwGetWindowAttrib(window, GLFW_MAXIMIZED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED))
            {
                int width, height;
                glfwGetWindowSize(window, &width, &height);
                if (width > 0 && height > 0)
                {
                    normalWidth = width;
                    normalHeight = height;
                }
            }
            if (glfwWindowShouldClose(window)) break;

            // Playback and job completion advance independently of visible panels.
            gApp.Update();
            dataViewer.Render(window, ui);
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
        snprintf(savedSize, sizeof(savedSize), "%d %d", normalWidth, normalHeight);
        SaveAppData("WindowSize", savedSize);
        gApp.Shutdown(ui);
    }
    cui_destroy(ui);
    cui_gl_destroy(renderer);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
