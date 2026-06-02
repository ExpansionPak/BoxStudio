cmake_minimum_required(VERSION 3.16)
project(BoxStudio LANGUAGES C CXX)

# ==============================================================================
# CONFIGURATION NOTES:
# - Target: Win32 API + DirectX 11.
# - Windows Fallback: Expects SDL2 at C:/SDL2 or via SDL2_DIR environment variable.
# ==============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define executable and source files (Swapped SDL2/OpenGL for Win32/DX11)
add_executable(BoxStudio 
    src/main.cpp
    third_party/glad/src/gl.c
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
)

# Global include directories
target_include_directories(BoxStudio PRIVATE 
    include
    third_party/glad/include
    third_party/imgui
    third_party/imgui/backends
)

# Link native Windows DirectX 11 library
target_link_libraries(BoxStudio PRIVATE d3d11 d3dcompiler)

# Cross-platform SDL2 dependency resolution (Kept for your other cross-platform systems)
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
    if(DEFINED ENV{SDL2_DIR})
        set(SDL2_PATH "$ENV{SDL2_DIR}")
    else()
        set(SDL2_PATH "C:/SDL2") 
    endif()

    if(EXISTS "${SDL2_PATH}")
        target_include_directories(BoxStudio PRIVATE "${SDL2_PATH}/include")
        target_link_directories(BoxStudio PRIVATE "${SDL2_PATH}/lib/x64" "${SDL2_PATH}/lib")
        target_link_libraries(BoxStudio PRIVATE SDL2 SDL2main)
        set(SDL2_FOUND TRUE)
    endif()
endif()

if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 could not be found! Check configuration notes at top of CMakeLists.txt.")
elseif(TARGET SDL2::SDL2)
    target_link_libraries(BoxStudio PRIVATE SDL2::SDL2)
endif()

# Windows runtime DLL handling
if(WIN32 AND EXISTS "${SDL2_PATH}/lib/x64/SDL2.dll")
    add_custom_command(TARGET BoxStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_PATH}/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:BoxStudio>
    )
endif()
