cmake_minimum_required(VERSION 3.10)
project(BoxStudio)

set(CMAKE_CXX_STANDARD 17)

# Setup directories
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(GLAD_DIR ${CMAKE_SOURCE_DIR}/third_party/glad)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Find System Packages
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED) # Better than hardcoding opengl32

# Verify files exist, then add executable
add_executable(BoxStudio
    ${CMAKE_SOURCE_DIR}/src/main.cpp  # Added absolute source dir macro

    # Glad
    ${GLAD_DIR}/src/gl.c

    # ImGui core
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp

    # ImGui backends (SDL2 + OpenGL)
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(BoxStudio PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${GLAD_DIR}/include
    ${INCLUDE_DIR}
)

target_link_libraries(BoxStudio PRIVATE 
    SDL2::SDL2 
    OpenGL::GL
)
