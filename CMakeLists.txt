cmake_minimum_required(VERSION 3.16)

project(BoxStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BOXSTUDIO_BUILD_GUI "Build the full BoxStudio editor UI" ${WIN32})

set(BOXSTUDIO_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

file(GLOB_RECURSE BOXSTUDIO_DECOMP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/decomps/*.cpp"
)

if(BOXSTUDIO_BUILD_GUI AND NOT WIN32)
    message(FATAL_ERROR "The GUI editor currently targets Win32/DX11 only. Configure with -DBOXSTUDIO_BUILD_GUI=OFF on this platform.")
endif()

if(BOXSTUDIO_BUILD_GUI)
    add_executable(BoxStudio WIN32
        src/main.cpp
        ${BOXSTUDIO_DECOMP_SOURCES}
        src/platform/win32/BoxStudioWin32.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )

    target_include_directories(BoxStudio PRIVATE
        src
        "${BOXSTUDIO_IMGUI_DIR}"
        "${BOXSTUDIO_IMGUI_DIR}/backends"
    )

    target_link_libraries(BoxStudio PRIVATE d3d11 d3dcompiler)

    if(MSVC)
        target_compile_options(BoxStudio PRIVATE /W4 /permissive-)
    else()
        target_compile_options(BoxStudio PRIVATE -Wall -Wextra -Wpedantic)
    endif()
else()
    add_executable(BoxStudio
        src/main.cpp
        ${BOXSTUDIO_DECOMP_SOURCES}
        src/platform/portable/BoxStudioPortable.cpp
    )

    target_include_directories(BoxStudio PRIVATE src)

    if(MSVC)
        target_compile_options(BoxStudio PRIVATE /W4 /permissive-)
    else()
        target_compile_options(BoxStudio PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

enable_testing()
add_test(NAME BoxStudioVersion COMMAND BoxStudio --version)
