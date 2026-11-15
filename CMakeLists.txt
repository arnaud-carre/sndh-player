cmake_minimum_required(VERSION 3.20)
project(SndhArchivePlayer LANGUAGES C CXX)

# Single-configuration generators default to an optimized build.
# Explicit build types and multi-configuration generators keep their settings.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR "Only Windows and macOS builds are supported.")
endif()

# Compile the checked-out sources directly. No dependency downloads or Git commands.
set(GLFW_SOURCES
    src/extern/glfw/src/context.c
    src/extern/glfw/src/init.c
    src/extern/glfw/src/input.c
    src/extern/glfw/src/monitor.c
    src/extern/glfw/src/platform.c
    src/extern/glfw/src/vulkan.c
    src/extern/glfw/src/window.c
    src/extern/glfw/src/egl_context.c
    src/extern/glfw/src/osmesa_context.c
    src/extern/glfw/src/null_init.c
    src/extern/glfw/src/null_monitor.c
    src/extern/glfw/src/null_window.c
    src/extern/glfw/src/null_joystick.c
)

if(WIN32)
    list(APPEND GLFW_SOURCES
        src/extern/glfw/src/win32_module.c
        src/extern/glfw/src/win32_time.c
        src/extern/glfw/src/win32_thread.c
        src/extern/glfw/src/win32_init.c
        src/extern/glfw/src/win32_joystick.c
        src/extern/glfw/src/win32_monitor.c
        src/extern/glfw/src/win32_window.c
        src/extern/glfw/src/wgl_context.c
    )
    set_source_files_properties(${GLFW_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS "_GLFW_WIN32;_CRT_SECURE_NO_WARNINGS"
    )
elseif(APPLE)
    enable_language(OBJC)
    list(APPEND GLFW_SOURCES
        src/extern/glfw/src/cocoa_time.c
        src/extern/glfw/src/posix_thread.c
        src/extern/glfw/src/posix_module.c
        src/extern/glfw/src/cocoa_init.m
        src/extern/glfw/src/cocoa_joystick.m
        src/extern/glfw/src/cocoa_monitor.m
        src/extern/glfw/src/cocoa_window.m
        src/extern/glfw/src/nsgl_context.m
    )
    set_source_files_properties(${GLFW_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS "_GLFW_COCOA"
    )
endif()

add_executable(SndhArchivePlayer
    src/main.cpp
    src/SndhArchivePlayer.cpp
    src/AppData.cpp
    src/AppIcon.cpp
    src/SndhArchive.cpp
    src/AudioStream.cpp
    src/AudioStream.h
    src/AsyncSndhStream.cpp
    src/jobSystem.cpp
    src/WavWriter.cpp

    src/AtariAudio/src/AtariAudioRenderer.cpp
    src/AtariAudio/src/AtariMachine.cpp
    src/AtariAudio/src/Blitter.cpp
    src/AtariAudio/src/Mk68901.cpp
    src/AtariAudio/src/SndhRenderer.cpp
    src/AtariAudio/src/SteDac.cpp
    src/AtariAudio/src/timedb.cpp
    src/AtariAudio/src/ym2149c.cpp
    src/AtariAudio/src/YmRenderer.cpp
    src/AtariAudio/src/external/ice_24.cpp
    src/AtariAudio/src/external/lzh.cpp
    src/AtariAudio/src/external/Musashi/m68k.cpp
    src/AtariAudio/src/external/Musashi/m68kcpu.cpp
    src/AtariAudio/src/external/Musashi/m68kops.cpp

    src/CuiGlfw.cpp
    src/extern/zip/src/zip.c
    ${GLFW_SOURCES}
)

# Keep the existing GLFW window and use only CUI's core and OpenGL renderer.
set(CUI_WITH_APP OFF CACHE BOOL "Use the player's GLFW integration" FORCE)
set(CUI_BUILD_TESTS OFF CACHE BOOL "Build CUI tests" FORCE)
set(CUI_BUILD_EXAMPLES OFF CACHE BOOL "Build CUI examples" FORCE)
add_subdirectory(src/extern/cui EXCLUDE_FROM_ALL)
target_link_libraries(SndhArchivePlayer PRIVATE cui::cui)
target_compile_features(SndhArchivePlayer PRIVATE cxx_std_17)
target_include_directories(SndhArchivePlayer PRIVATE
    src/extern/glfw/include
)
if(WIN32)
    enable_language(RC)
    target_sources(SndhArchivePlayer PRIVATE src/resources/app.rc)
    target_include_directories(SndhArchivePlayer PRIVATE src/resources)
    set_target_properties(SndhArchivePlayer PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(SndhArchivePlayer PRIVATE UNICODE _UNICODE)
    target_link_libraries(SndhArchivePlayer PRIVATE
        opengl32 user32 gdi32 shell32 imm32 winmm
    )
elseif(APPLE)
    target_sources(SndhArchivePlayer PRIVATE
        src/resources/app.icns
        src/resources/app-about.png)
    set_source_files_properties(
        src/resources/app.icns
        src/resources/app-about.png PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(SndhArchivePlayer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_ICON_FILE app.icns)
    target_link_libraries(SndhArchivePlayer PRIVATE
        "-framework Cocoa" "-framework IOKit"
        "-framework CoreFoundation" "-framework OpenGL" "-framework AudioToolbox"
    )
endif()

if(MSVC)
    target_compile_options(SndhArchivePlayer PRIVATE /W3 /MP /permissive- /utf-8)
    # Use the portable main() entry point without opening a console window.
    target_link_options(SndhArchivePlayer PRIVATE /ENTRY:mainCRTStartup)
endif()
