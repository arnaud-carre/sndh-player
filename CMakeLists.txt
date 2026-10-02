cmake_minimum_required(VERSION 3.20)
project(SndhArchivePlayer LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Only Windows builds are supported for now.")
endif()

# Compile the checked-out sources directly. No dependency downloads or Git commands.
set(GLFW_SOURCES
    SndhArchivePlayer/extern/glfw/src/context.c
    SndhArchivePlayer/extern/glfw/src/init.c
    SndhArchivePlayer/extern/glfw/src/input.c
    SndhArchivePlayer/extern/glfw/src/monitor.c
    SndhArchivePlayer/extern/glfw/src/platform.c
    SndhArchivePlayer/extern/glfw/src/vulkan.c
    SndhArchivePlayer/extern/glfw/src/window.c
    SndhArchivePlayer/extern/glfw/src/egl_context.c
    SndhArchivePlayer/extern/glfw/src/osmesa_context.c
    SndhArchivePlayer/extern/glfw/src/null_init.c
    SndhArchivePlayer/extern/glfw/src/null_monitor.c
    SndhArchivePlayer/extern/glfw/src/null_window.c
    SndhArchivePlayer/extern/glfw/src/null_joystick.c
    SndhArchivePlayer/extern/glfw/src/win32_module.c
    SndhArchivePlayer/extern/glfw/src/win32_time.c
    SndhArchivePlayer/extern/glfw/src/win32_thread.c
    SndhArchivePlayer/extern/glfw/src/win32_init.c
    SndhArchivePlayer/extern/glfw/src/win32_joystick.c
    SndhArchivePlayer/extern/glfw/src/win32_monitor.c
    SndhArchivePlayer/extern/glfw/src/win32_window.c
    SndhArchivePlayer/extern/glfw/src/wgl_context.c
)
set_source_files_properties(${GLFW_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "_GLFW_WIN32;_CRT_SECURE_NO_WARNINGS"
)

add_executable(SndhArchivePlayer WIN32
    SndhArchivePlayer/main.cpp
    SndhArchivePlayer/SndhArchivePlayer.cpp
    SndhArchivePlayer/SndhArchive.cpp
    SndhArchivePlayer/AsyncSndhStream.cpp
    SndhArchivePlayer/jobSystem.cpp
    SndhArchivePlayer/WavWriter.cpp

    AtariAudio/src/AtariAudioRenderer.cpp
    AtariAudio/src/AtariMachine.cpp
    AtariAudio/src/Blitter.cpp
    AtariAudio/src/Mk68901.cpp
    AtariAudio/src/SndhRenderer.cpp
    AtariAudio/src/SteDac.cpp
    AtariAudio/src/timedb.cpp
    AtariAudio/src/ym2149c.cpp
    AtariAudio/src/YmRenderer.cpp
    AtariAudio/src/external/ice_24.cpp
    AtariAudio/src/external/lzh.cpp
    AtariAudio/src/external/Musashi/m68k.cpp
    AtariAudio/src/external/Musashi/m68kcpu.cpp
    AtariAudio/src/external/Musashi/m68kops.cpp

    SndhArchivePlayer/extern/imgui/imgui.cpp
    SndhArchivePlayer/extern/imgui/imgui_demo.cpp
    SndhArchivePlayer/extern/imgui/imgui_draw.cpp
    SndhArchivePlayer/extern/imgui/imgui_tables.cpp
    SndhArchivePlayer/extern/imgui/imgui_widgets.cpp
    SndhArchivePlayer/extern/imgui/backends/imgui_impl_glfw.cpp
    SndhArchivePlayer/extern/imgui/backends/imgui_impl_opengl3.cpp

    SndhArchivePlayer/extern/zip/src/zip.c
    ${GLFW_SOURCES}
)

target_compile_features(SndhArchivePlayer PRIVATE cxx_std_17)
target_include_directories(SndhArchivePlayer PRIVATE
    SndhArchivePlayer/extern/imgui
    SndhArchivePlayer/extern/imgui/backends
    SndhArchivePlayer/extern/glfw/include
)
target_compile_definitions(SndhArchivePlayer PRIVATE UNICODE _UNICODE)
target_link_libraries(SndhArchivePlayer PRIVATE
    opengl32 user32 gdi32 shell32 imm32 winmm
)

if(MSVC)
    target_compile_options(SndhArchivePlayer PRIVATE /W3 /MP /permissive- /utf-8)
    # Use the portable main() entry point without opening a console window.
    target_link_options(SndhArchivePlayer PRIVATE /ENTRY:mainCRTStartup)
endif()
