#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "BoxStudioApp.h"
#include "decomps/sm64/hackersm64/HackerSM64.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cmath>
#include <regex>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

namespace fs = std::filesystem;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0;
static UINT g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static ID3D11SamplerState* g_wrapSamplerState = nullptr;
static ID3D11SamplerState* g_clampSamplerState = nullptr;


// BoxStudioWin32.cpp intentionally composes the current editor from focused
// implementation fragments. This keeps the large prototype build-stable while
// giving contributors clear homes for future extraction into normal .cpp/.h units.
#include "../../app/BoxStudioState.inc.cpp"
#include "../../core/BoxStudioUtilities.inc.cpp"
#include "Win32Dialogs.inc.cpp"
#include "../../project/ProjectFiles.inc.cpp"
#include "../../renderer/LevelRenderData.inc.cpp"
#include "../../renderer/dx11/ViewportRendererDX11.inc.cpp"
#include "../../editor/LevelEditor.inc.cpp"
#include "../../ui/BoxStudioUi.inc.cpp"
#include "../../app/BoxStudioCommands.inc.cpp"
#include "Win32AppHost.inc.cpp"
