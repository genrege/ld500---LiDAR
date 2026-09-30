cmake_minimum_required(VERSION 3.16)
project(ld500 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Vendored dependencies (see third_party/README.md) -- built from source, no network fetch.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

add_executable(ld500
    ld500/ld500.cpp
    ld500/Acquisition/RadarProtocol.cpp
    ld500/Model/RadarGridModel.cpp
    ld500/Model/ObjectTracking.cpp
    ld500/Platform/PosixSerialPort.cpp
    ld500/Platform/FileSettingsStore.cpp
    ld500/Shell/ImGuiShell.cpp
    ld500/Rendering/RadarRendererImGui.cpp
    ld500/Rendering/ImGuiHudSurface.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(ld500 PRIVATE
    ld500
    ld500/Acquisition
    ld500/Model
    ld500/Rendering
    ld500/Platform
    ld500/Shell
    third_party
    third_party/imgui
    third_party/imgui/backends
)

target_link_libraries(ld500 PRIVATE glfw)

if (APPLE)
    # OpenGL is deprecated (but functional) on macOS in favor of Metal; silence the resulting
    # warnings from our direct gl*() calls used for the grid texture upload.
    target_compile_definitions(ld500 PRIVATE GL_SILENCE_DEPRECATION)

    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREVIDEO_FRAMEWORK CoreVideo REQUIRED)
    target_link_libraries(ld500 PRIVATE
        ${OPENGL_FRAMEWORK} ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREVIDEO_FRAMEWORK})
endif()
