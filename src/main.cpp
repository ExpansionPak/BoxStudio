#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <filesystem>
namespace fs = std::filesystem;

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

enum SetupStage {
    STAGE_MAIN_MENU,
    STAGE_NAME_PROJECT,
    STAGE_CHOOSE_GAME,
    STAGE_CHOOSE_DECOMP,
    STAGE_SET_DIRECTORY,
    STAGE_COMPLETE
};

SetupStage current_stage = STAGE_MAIN_MENU;

// Variables to store the user's choices
int selected_game = -1;   // -1 = none, 0 = Super Mario 64
int selected_decomp = -1; // -1 = none, 0 = HackerSM64
char folder_path[512] = ""; 
bool is_valid_dir = false;

// Main code
int main(int, char**)
{
    // Force a console to open so you can read native assert logs
    ::AllocConsole();
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);

    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();

    HMONITOR monitor = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    float main_scale = (monitor != nullptr) ? ImGui_ImplWin32_GetDpiScaleForMonitor(monitor) : 1.0f;

    // Create application window with borderless sizing flags
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Project", nullptr };
    ::RegisterClassExW(&wc);

    HWND hwnd = ::CreateWindowW(
        wc.lpszClassName, 
        L"Project Launcher", 
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, // Removes classic title bar
        100, 100, 
        (int)(1280 * main_scale), (int)(800 * main_scale), 
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;     // Automatically scales fonts per-viewport
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleViewports; // Scales the UI windows based on monitor DPIs

    io.IniFilename = nullptr; // <--- Disables layout saving entirely

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowRounding = 8.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f; // Ensure solid background opacity
    
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Adjust window and panel borders
    style.WindowRounding    = 8.0f;  // Rounding of main windows
    style.ChildRounding     = 6.0f;  // Rounding of child windows
    style.FrameRounding     = 6.0f;  // Rounding of buttons, sliders, input boxes
    style.PopupRounding     = 6.0f;  // Rounding of context menus and tooltips
    style.ScrollbarRounding = 12.0f; // Rounding of scrollbars
    style.GrabRounding      = 6.0f;  // Rounding of the slider "grabber" handles
    style.TabRounding       = 4.0f;  // Rounding of dock tabs

    // 1. Load Fonts FIRST (before Platform/Renderer backends)
    ImFont* main_font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    if (main_font == nullptr) {
        io.Fonts->AddFontDefault();
    }

    // 2. Setup Platform/Renderer backends SECOND
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // io.Fonts->Build();
    
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Viewport handling (if we have multiple monitors, we want to make sure the main viewport is on the primary monitor where the mouse cursor starts)
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        // Force flat layout padding properties
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 20.0f)); // Generous internal margins

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::Begin("MainWorkspace", nullptr, window_flags);

        ImGui::PopStyleVar(3);

        // --- CUSTOM TITLE BAR & WINDOW CONTROLS ---
        // Align controls to the top right of the viewport
        float button_width = 40.0f;
        float title_bar_height = 32.0f;

        ImGui::SetCursorPos(ImVec2(ImGui::GetIO().DisplaySize.x - (button_width * 3) - 10.0f, 5.0f));

        // 1. Minimize Button
        if (ImGui::Button("_", ImVec2(button_width, 22.0f))) {
            HWND current_hwnd = (HWND)ImGui::GetMainViewport()->PlatformHandle;
            ::ShowWindow(current_hwnd, SW_MINIMIZE); // Fixed
        }

        ImGui::SameLine();

        // 2. Maximize / Restore Button
        if (ImGui::Button("[]", ImVec2(button_width, 22.0f))) {
            HWND current_hwnd = (HWND)ImGui::GetMainViewport()->PlatformHandle;
            WINDOWPLACEMENT wp;
            wp.length = sizeof(WINDOWPLACEMENT);
            ::GetWindowPlacement(current_hwnd, &wp);
            if (wp.showCmd == SW_MAXIMIZE)
                ::ShowWindow(current_hwnd, SW_RESTORE); // Fixed
            else
                ::ShowWindow(current_hwnd, SW_MAXIMIZE); // Fixed
        }

        ImGui::SameLine();

        // 3. Close Button (Applies custom hover styling for a clean look)
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.0f, 0.0f, 1.0f));
        if (ImGui::Button("X", ImVec2(button_width, 22.0f))) {
            ::PostQuitMessage(0); // Safely trigger application exit
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::Separator();

        // --- STAGE 0: MAIN MENU ---
        if (current_stage == STAGE_MAIN_MENU)
        {
            ImGui::Text("Hello, world!");

            if (ImGui::Button("Create Project")) {
                current_stage = STAGE_NAME_PROJECT; // Move to next stage
            }
        }

        // --- STAGE 0: NAME PROJECT (placeholder) ---
        else if (current_stage == STAGE_NAME_PROJECT)
        {
            ImGui::Text("Project naming is not implemented yet. Click next to continue.");
            ImGui::Separator();

            if (ImGui::Button("Next >>")) {
                current_stage = STAGE_CHOOSE_GAME;
            }
        }

        // --- STAGE 1: CHOOSE GAME ---
        else if (current_stage == STAGE_CHOOSE_GAME)
        {
            ImGui::Text("Which game?");
            ImGui::Separator();

            if (ImGui::Selectable("Super Mario 64", selected_game == 0)) {
                selected_game = 0;
            }
            // Future games can go here as extra Selectables

            ImGui::Spacing();
            // Only allow next if a game is selected
            ImGui::BeginDisabled(selected_game == -1);
            if (ImGui::Button("Next >>")) {
                current_stage = STAGE_CHOOSE_DECOMP;
            }
            ImGui::EndDisabled();
        }

        // --- STAGE 2: CHOOSE DECOMP BASE ---
        else if (current_stage == STAGE_CHOOSE_DECOMP)
        {
            ImGui::Text("Choose a decomp base:");
            ImGui::Separator();

            if (ImGui::Selectable("HackerSM64", selected_decomp == 0)) {
                selected_decomp = 0;
            }

            ImGui::Spacing();
            ImGui::BeginDisabled(selected_decomp == -1);
            if (ImGui::Button("Next >>")) {
                current_stage = STAGE_SET_DIRECTORY;
            }
            ImGui::EndDisabled();
        }

        // --- STAGE 3: SET DIRECTORY ---
        else if (current_stage == STAGE_SET_DIRECTORY)
        {
            ImGui::Text("Set the directory to your HackerSM64 folder:");
            ImGui::Separator();

            // 1. Input Field - Tracks if the user modified the text
            if (ImGui::InputText("Folder Path", folder_path, IM_ARRAYSIZE(folder_path)))
            {
                if (strlen(folder_path) > 0) 
                {
                    fs::path p(folder_path);
                    if (fs::exists(p) && fs::is_directory(p))
                    {
                        bool has_makefile = fs::exists(p / "Makefile");
                        bool has_src      = fs::exists(p / "src");
                        bool has_actors   = fs::exists(p / "actors");
                        bool has_config    = fs::exists(p / "include/config");
                        bool has_asm     = fs::exists(p / "asm");
                        bool has_assets   = fs::exists(p / "assets");
                        bool has_levels   = fs::exists(p / "levels");
                        bool has_textures = fs::exists(p / "textures");
                        bool has_build    = fs::exists(p / "build");
                        is_valid_dir = has_makefile && has_src && has_actors && has_config && has_asm && has_assets && has_levels && has_textures && has_build;
                    }
                    else
                    {
                        is_valid_dir = false;
                    }
                }
                else
                {
                    is_valid_dir = false;
                }
            }

            // 2. Feedback Text (Inline validation status)
            if (strlen(folder_path) > 0)
            {
                if (is_valid_dir)
                {
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Valid HackerSM64 directory structure detected.");
                }
                else
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "✗ Invalid directory. Please select a valid HackerSM64 folder.");
                }
            }
            else
            {
                ImGui::Text("Please enter a path.");
            }

            ImGui::Spacing();
            
            // 3. Action Buttons
            // Disable the finish button unless the validation checks pass successfully
            ImGui::BeginDisabled(!is_valid_dir);
            if (ImGui::Button("Finish")) {
                current_stage = STAGE_COMPLETE;
            }
            ImGui::EndDisabled();
        }

        // --- STAGE 4: SETUP COMPLETE ---
        else if (current_stage == STAGE_COMPLETE)
        {
            ImGui::Text("Setup Complete!");
            ImGui::Text("Target: Super Mario 64 (HackerSM64)");
            ImGui::Text("Path: %s", folder_path);
        }

        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // UPDATE THIS CONDITION BLOCK:
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    RECT rect;
    ::GetClientRect(hWnd, &rect);
    UINT width = rect.right - rect.left;
    UINT height = rect.bottom - rect.top;

    if (width == 0)  width = 1280;
    if (height == 0) height = 800;

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 3;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createDeviceFlags = 0;
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
    {
        char errorMsg[256];
        sprintf_s(errorMsg, "D3D11CreateDeviceAndSwapChain failed.\nHRESULT Error Code: 0x%08lX\n\nIf the code is 0x887A002D, your system is missing Windows Graphics Tools.", res);
        
        ::MessageBoxA(nullptr, errorMsg, "DirectX Initialization Fatal Error", MB_OK | MB_ICONERROR);
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0; 
        break;
    
    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ::ScreenToClient(hWnd, &pt);

        // Get the current width of the host window
        RECT rect;
        ::GetClientRect(hWnd, &rect);
        float window_width = (float)(rect.right - rect.left);

        // Define the exact width of your button cluster area (3 buttons * 40px + padding)
        float button_zone_width = 140.0f; 

        // If the mouse is in the top header area...
        if (pt.y < 40) 
        {
            // ...but it's over on the right side where the buttons live:
            if (pt.x > (window_width - button_zone_width))
            {
                return HTCLIENT; // Tell Windows: "Treat this as a normal interactive button zone, don't drag!"
            }
            
            return HTCAPTION; // Otherwise, treat it as a draggable title bar
        }
        return HTCLIENT;
    }

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); 
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}