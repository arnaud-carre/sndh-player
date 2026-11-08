cmake_minimum_required(VERSION 3.14)

project(cui
    VERSION 1.1.0
    DESCRIPTION "Modern native UI components for C"
    LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CUI_IS_TOP_LEVEL ON)
else()
    set(CUI_IS_TOP_LEVEL OFF)
endif()

option(CUI_BUILD_EXAMPLES "Build the examples" ${CUI_IS_TOP_LEVEL})
option(CUI_BUILD_TESTS "Build the tests" ${CUI_IS_TOP_LEVEL})
option(CUI_TEXT_SHAPING "Shape static Arabic text with OpenType" OFF)
option(CUI_BUILD_SHARED "Build a shared library" OFF)
option(CUI_WITH_APP "Include the app layer (windows, events)" ON)
option(CUI_DEFAULT_FONTS "Embed Inter, Geist and Lucide" ON)
option(CUI_STB_IMAGE "Image loading with stb_image" ON)
option(CUI_IMAGE_ALL_FORMATS "Also decode BMP, GIF and TGA (PNG and JPEG are always on)" OFF)
set(CUI_PLATFORM "auto" CACHE STRING "Window backend: auto, win32, cocoa, x11, sdl2, glfw")
set_property(CACHE CUI_PLATFORM PROPERTY STRINGS auto win32 cocoa x11 sdl2 glfw)

if(NOT CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

set(CUI_CORE_SOURCES
    src/cui_core.c
    src/cui_theme.c
    src/cui_draw.c
    src/cui_text.c
    src/cui_layout.c
    src/cui_widgets.c
    src/cui_controls.c
    src/cui_textedit.c
    src/cui_overlay.c
    src/cui_data.c
    src/cui_charts.c
    src/cui_extras.c
    src/cui_actions.c
    src/cui_pickers.c
    src/cui_disclosure.c
    src/cui_grid.c
    src/cui_treetable.c
    src/cui_code.c
    src/cui_markdown.c
    src/cui_editor.c
    src/cui_diff.c
    src/cui_tools.c
    src/cui_hex.c
    src/cui_social.c
    src/cui_media.c
    src/cui_planning.c
    src/cui_chrome.c
    src/cui_dock.c
    src/cui_viz.c
    src/cui_motion.c
    src/cui_ai.c
    src/cui_image.c
    src/cui_nodes.c
    src/cui_flame.c
    src/cui_curves.c
    src/cui_timeline.c
    src/cui_dials.c
    src/cui_month.c
    src/cui_tour.c
    src/cui_photo.c
    src/cui_files.c
    src/cui_terminal.c
    src/cui_emoji.c
    src/render/cui_render_sw.c
    src/render/cui_render_gl.c)

if(CUI_DEFAULT_FONTS)
    list(APPEND CUI_CORE_SOURCES src/cui_font_data.c src/cui_inflate.c)
endif()

# Pick the window backend
set(CUI_BACKEND "")
if(CUI_WITH_APP)
    if(CUI_PLATFORM STREQUAL "auto")
        if(WIN32)
            set(CUI_BACKEND win32)
        elseif(APPLE)
            set(CUI_BACKEND cocoa)
        else()
            set(CUI_BACKEND x11)
        endif()
    else()
        set(CUI_BACKEND ${CUI_PLATFORM})
    endif()
    if(CUI_BACKEND STREQUAL "cocoa")
        enable_language(OBJC)
    endif()
    list(APPEND CUI_CORE_SOURCES src/platform/cui_app.c src/platform/cui_dialogs.c)
    if(CUI_BACKEND STREQUAL "win32")
        list(APPEND CUI_CORE_SOURCES src/platform/cui_app_win32.c)
    elseif(CUI_BACKEND STREQUAL "cocoa")
        list(APPEND CUI_CORE_SOURCES src/platform/cui_app_cocoa.m)
    elseif(CUI_BACKEND STREQUAL "x11")
        list(APPEND CUI_CORE_SOURCES src/platform/cui_app_x11.c)
    elseif(CUI_BACKEND STREQUAL "sdl2")
        list(APPEND CUI_CORE_SOURCES src/platform/cui_app_sdl2.c)
    elseif(CUI_BACKEND STREQUAL "glfw")
        list(APPEND CUI_CORE_SOURCES src/platform/cui_app_glfw.c)
    else()
        message(FATAL_ERROR "Unknown CUI_PLATFORM '${CUI_BACKEND}'")
    endif()
endif()

if(CUI_BUILD_SHARED)
    add_library(cui SHARED ${CUI_CORE_SOURCES})
    target_compile_definitions(cui PUBLIC CUI_SHARED PRIVATE CUI_BUILDING)
    set_target_properties(cui PROPERTIES C_VISIBILITY_PRESET hidden)
else()
    add_library(cui STATIC ${CUI_CORE_SOURCES})
endif()
add_library(cui::cui ALIAS cui)

target_include_directories(cui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

if(CUI_TEXT_SHAPING)
    target_sources(cui PRIVATE src/cui_bidi.c src/cui_shape.c)
    target_compile_definitions(cui PRIVATE CUI_TEXT_SHAPING)
endif()

if(NOT CUI_DEFAULT_FONTS)
    target_compile_definitions(cui PRIVATE CUI_NO_DEFAULT_FONTS)
endif()
if(NOT CUI_STB_IMAGE)
    target_compile_definitions(cui PRIVATE CUI_NO_STB_IMAGE)
elseif(CUI_IMAGE_ALL_FORMATS)
    target_compile_definitions(cui PRIVATE CUI_IMAGE_ALL_FORMATS)
endif()

# One section per function and object, so linkers can drop the parts an app
# never calls (-Wl,--gc-sections, -dead_strip, /OPT:REF).
if(MSVC)
    target_compile_options(cui PRIVATE /Gy /Gw)
else()
    target_compile_options(cui PRIVATE -ffunction-sections -fdata-sections)
endif()

if(MSVC)
    target_compile_options(cui PRIVATE /W4 /wd4100 /wd4201 /wd4204 /wd4221 /wd4244 /wd4267 /wd4305 /wd4996 /utf-8)
    target_compile_definitions(cui PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(cui PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# System libraries
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(cui PRIVATE Threads::Threads)
endif()
if(NOT WIN32 AND NOT MSVC)
    target_link_libraries(cui PUBLIC m)
endif()

if(CUI_BACKEND STREQUAL "win32")
    target_link_libraries(cui PUBLIC user32 gdi32 opengl32 shell32 imm32 dwmapi comdlg32 ole32)
elseif(CUI_BACKEND STREQUAL "cocoa")
    target_link_libraries(cui PUBLIC "-framework Cocoa" "-framework QuartzCore" "-framework OpenGL")
    target_compile_definitions(cui PRIVATE GL_SILENCE_DEPRECATION)
    set_source_files_properties(src/platform/cui_app_cocoa.m PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(CUI_BACKEND STREQUAL "x11")
    find_package(X11 REQUIRED)
    target_include_directories(cui PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(cui PUBLIC ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
elseif(CUI_BACKEND STREQUAL "sdl2")
    find_package(SDL2 REQUIRED)
    target_link_libraries(cui PUBLIC SDL2::SDL2)
elseif(CUI_BACKEND STREQUAL "glfw")
    find_package(glfw3 REQUIRED)
    target_link_libraries(cui PUBLIC glfw)
endif()

set_target_properties(cui PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

if(CUI_BUILD_EXAMPLES AND CUI_WITH_APP)
    add_subdirectory(examples)
endif()

if(CUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
include(GNUInstallDirs)
install(TARGETS cui EXPORT cuiTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/cui DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT cuiTargets NAMESPACE cui:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cui)
include(CMakePackageConfigHelpers)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cuiConfig.cmake
    "include(CMakeFindDependencyMacro)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/cuiTargets.cmake\")\n")
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/cuiConfigVersion.cmake COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cuiConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/cuiConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cui)

# pkg-config
set(CUI_PC_LIBS "-lcui")
if(CUI_BACKEND STREQUAL "x11")
    set(CUI_PC_LIBS "${CUI_PC_LIBS} -lX11 -ldl -lpthread -lm")
elseif(CUI_BACKEND STREQUAL "cocoa")
    set(CUI_PC_LIBS "${CUI_PC_LIBS} -framework Cocoa -framework QuartzCore -framework OpenGL")
elseif(CUI_BACKEND STREQUAL "win32")
    set(CUI_PC_LIBS "${CUI_PC_LIBS} -luser32 -lgdi32 -lopengl32 -lshell32 -limm32 -ldwmapi")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/cui.pc
"prefix=\${pcfiledir}/../..
libdir=\${prefix}/${CMAKE_INSTALL_LIBDIR}
includedir=\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}

Name: cui
Description: ${PROJECT_DESCRIPTION}
Version: ${PROJECT_VERSION}
Cflags: -I\${includedir}
Libs: -L\${libdir} ${CUI_PC_LIBS}
")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cui.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
