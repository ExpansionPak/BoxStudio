#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>

int main(int argc, char* argv[])
{
    // -------------------------------------------------------
    // Initialize SDL2
    // SDL_INIT_VIDEO  = window + input
    // SDL_INIT_TIMER  = needed by ImGui internally
    // -------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return -1;
    }

    // Tell SDL we want an OpenGL 3.0 Core context
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // -------------------------------------------------------
    // Create the window
    // -------------------------------------------------------
    SDL_Window* window = SDL_CreateWindow(
        "BoxStudio",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window)
    {
        printf("SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    // Create OpenGL context and attach it to the window
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // vsync on

    // -------------------------------------------------------
    // Initialize Dear ImGui
    // -------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // arrow keys / tab navigation
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // panels can be docked together

    ImGui::StyleColorsDark();

    // Hook ImGui up to our SDL window and OpenGL context
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // -------------------------------------------------------
    // Main loop
    // -------------------------------------------------------
    bool running = true;

    while (running)
    {
        // --- 1. Handle OS events (close button, keyboard, mouse, etc.) ---
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event); // let ImGui see the event first

            if (event.type == SDL_QUIT)
                running = false;
        }

        // --- 2. Start a new ImGui frame ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // --- 3. Build the UI ---

        // Full-screen dockspace so panels can be docked anywhere
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Menu bar at the top
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open ROM...", "Ctrl+O")) { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) running = false;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                // Future: toggle panels on/off here
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("About BoxStudio")) { /* TODO */ }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        // A placeholder welcome panel so the window isn't empty
        ImGui::Begin("Welcome");
        ImGui::Text("BoxStudio");
        ImGui::Spacing();
        ImGui::TextDisabled("Open a SM64 ROM to get started.");
        ImGui::End();

        // --- 4. Render ---
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.10f, 0.10f, 0.10f, 1.00f); // dark background
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window); // flip front/back buffer
    }

    // -------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}