//-----------------------------------------------------------------
//
//	SndhArchivePlayer - play large zip archive of sndh or ym files
//	Windows & macOS
//	by Arnaud Carré aka Leonard/Oxygene (@leonard_coder)
//
//-----------------------------------------------------------------
// Dear ImGui application using GLFW windowing and OpenGL 3 rendering.
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "SndhArchivePlayer.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <math.h>
#include <vector>

static SndhArchivePlayer gApp;

static void GlfwErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static float GetUiScale(GLFWwindow* window)
{
#ifdef __APPLE__
    // Cocoa window coordinates are already in logical points.
    return 1.0f;
#else
    float xScale, yScale;
    glfwGetWindowContentScale(window, &xScale, &yScale);
    return xScale > 0.0f ? xScale : 1.0f;
#endif
}

static float GetFontPixelScale(GLFWwindow* window)
{
#ifdef __APPLE__
    int windowWidth, windowHeight, framebufferWidth, framebufferHeight;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (windowWidth > 0 && framebufferWidth > 0)
        return float(framebufferWidth) / float(windowWidth);
#endif
    return 1.0f;
}

static void ApplyUiScale(float scale, float fontPixelScale, const ImGuiStyle& baseStyle)
{
    // Always start from the original style to avoid cumulative scaling.
    ImGui::GetStyle() = baseStyle;
    ImGui::GetStyle().ScaleAllSizes(scale);

    // Rasterize at the target pixel size for sharp text instead of stretching it.
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 13.0f * scale * fontPixelScale;
    io.FontDefault = io.Fonts->AddFontDefault(&fontConfig);
    // Keep the logical font size independent of Retina framebuffer density.
    io.FontGlobalScale = 1.0f / fontPixelScale;
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

int main()
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
        return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(800, 800, "SNDH & YM Archive Player v" SNDH_ARCHIVE_PLAYER_VERSION, nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    glfwSetWindowPos(window, 100, 100);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetDropCallback(window, DropFilesCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsClassic();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    const ImGuiStyle baseStyle = style;
    float uiScale = GetUiScale(window);
    float fontPixelScale = GetFontPixelScale(window);
    ApplyUiScale(uiScale, fontPixelScale, baseStyle);

    // Let ImGui install and chain the GLFW keyboard, mouse, and focus callbacks.
    const bool platformInitialized = ImGui_ImplGlfw_InitForOpenGL(window, true);
    const bool rendererInitialized = platformInitialized && ImGui_ImplOpenGL3_Init("#version 330 core");
    if (!rendererInitialized || !ImGui_ImplOpenGL3_CreateDeviceObjects())
    {
        fprintf(stderr, "Unable to initialize the ImGui OpenGL renderer.\n");
        if (rendererInitialized)
            ImGui_ImplOpenGL3_Shutdown();
        if (platformInitialized)
            ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gApp.Startup();
    bool dockingSetupDone = false;
    int exitCode = 0;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwWindowShouldClose(window))
            break;

        // Rebuild between frames when moving monitors or changing DPI settings.
        const float newUiScale = GetUiScale(window);
        const float newFontPixelScale = GetFontPixelScale(window);
        if (fabsf(newUiScale - uiScale) > 0.001f || fabsf(newFontPixelScale - fontPixelScale) > 0.001f)
        {
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            ApplyUiScale(newUiScale, newFontPixelScale, baseStyle);
            if (!ImGui_ImplOpenGL3_CreateFontsTexture())
            {
                fprintf(stderr, "Unable to recreate the scaled font texture.\n");
                exitCode = 1;
                break;
            }
            uiScale = newUiScale;
            fontPixelScale = newFontPixelScale;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoWindowMenuButton);
        if ( !dockingSetupDone )
        {
            ImGui::DockBuilderRemoveNode(dockspace_id); // Clear out existing layout
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode); // Add empty node
            ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
            ImGuiID dock_main_id   = dockspace_id; // This variable will track the document node, however we are not using it here as we aren't docking anything into it.
            ImGuiID dock_id_up     = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up,    0.25f, nullptr, &dock_main_id);
            ImGuiID dock_id_up_left;
            ImGuiID dock_id_up_right;
            ImGui::DockBuilderSplitNode(dock_id_up, ImGuiDir_Left,  0.5f, &dock_id_up_left, &dock_id_up_right);
            ImGuiID dock_id_middle;
            ImGuiID dock_id_down;
            ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up,    0.75f, &dock_id_middle, &dock_id_down);

            // dock all windows to each pannel
            ImGui::DockBuilderDockWindow(kWndSongInfo, dock_id_up_left);
            ImGui::DockBuilderDockWindow(kWndAudioOut, dock_id_up_right);
            ImGui::DockBuilderDockWindow(kWndSndhArchive, dock_id_middle);
            ImGui::DockBuilderDockWindow(kWndFileViewer, dock_id_middle);
            ImGui::DockBuilderDockWindow(kWndEmulation, dock_id_down);
            ImGui::DockBuilderFinish(dockspace_id);

            dockingSetupDone = true;
        }

        gApp.UpdateImGui();
        ImGui::Render();

        // Keep playback advancement running while throttling minimized frames.
        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) || framebufferWidth == 0 || framebufferHeight == 0)
        {
            glfwWaitEventsTimeout(0.032);
            continue;
        }
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        const ImVec4 clearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClearColor(clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    gApp.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return exitCode;
}
