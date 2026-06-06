#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

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

struct ViewportRenderer {
    ID3D11Texture2D* color = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11Buffer* cameraBuffer = nullptr;
    ID3D11Buffer* drawBuffer = nullptr;
    ID3D11BlendState* blendState = nullptr;
    ID3D11DepthStencilState* depthWriteState = nullptr;
    ID3D11DepthStencilState* depthReadState = nullptr;
    ID3D11RasterizerState* solidRasterizer = nullptr;
    ID3D11RasterizerState* wireRasterizer = nullptr;
    ID3D11ShaderResourceView* whiteSrv = nullptr;
    int width = 0;
    int height = 0;
    int vertexCapacity = 0;
};

static ViewportRenderer g_viewportRenderer;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct LevelObject {
    std::string name;
    std::string model;
    std::string behParam = "0x00000000";
    std::string behavior;
    Vec3 position;
    Vec3 rotation;
    float scale = 1.0f;
    bool fromScript = false;
};

struct MeshTriangle {
    int a = 0;
    int b = 0;
    int c = 0;
    ImVec2 uva = ImVec2(0.0f, 0.0f);
    ImVec2 uvb = ImVec2(0.0f, 0.0f);
    ImVec2 uvc = ImVec2(0.0f, 0.0f);
    std::string surface;
    std::string texture;
    std::string textureFormat;
    std::string textureSize;
    int textureWidth = 32;
    int textureHeight = 32;
    bool textureClamp = false;
    bool textureClampS = false;
    bool textureClampT = false;
    bool textureMirrorS = false;
    bool textureMirrorT = false;
    float textureScaleS = 1.0f;
    float textureScaleT = 1.0f;
    bool textureGen = false;
    ImU32 color = IM_COL32(110, 150, 120, 220);
};

static const char* kEditorWaterTexture = "__BOXSTUDIO_WATER__";

struct LevelMesh {
    std::vector<Vec3> vertices;
    std::vector<MeshTriangle> triangles;
    std::vector<fs::path> sources;
    bool renderMesh = false;
};

struct TextureAsset {
    std::string name;
    fs::path path;
};

struct GpuTexture {
    std::string name;
    fs::path source;
    int width = 0;
    int height = 0;
    ID3D11ShaderResourceView* srv = nullptr;
    std::string error;
};

struct ViewportVertex {
    float x, y, z;
    float u, v;
    float r, g, b, a;
};

struct ViewportMeshBatch {
    std::vector<ViewportVertex> vertices;
    std::string textureKey;
    bool textured = false;
    bool clamp = false;
    bool water = false;
};

struct ViewportCameraConstants {
    float camera[4];
    float right[4];
    float up[4];
    float forward[4];
    float viewport[4];
};

struct ViewportDrawConstants {
    float useTexture = 0.0f;
    float alphaTest = 0.0f;
    float alphaScale = 1.0f;
    float pad = 0.0f;
};

struct LevelInfo {
    std::string name;
    fs::path path;
    fs::path scriptPath;
};

struct Project {
    std::string name;
    std::string game = "Super Mario 64";
    std::string decompBase = "HackerSM64";
    fs::path root;
    fs::path sourcePath;
    fs::path manifestPath;
};

enum class Screen {
    Home,
    CreateProject,
    Editor
};

enum class EditorMode {
    LevelMenu,
    EditingLevel
};

enum class LevelViewMode {
    Wireframe,
    GeometryOnly,
    TextureMode
};

struct CopyJob {
    std::thread worker;
    std::atomic<bool> running = false;
    std::atomic<bool> finished = false;
    std::atomic<bool> success = false;
    std::atomic<int> copied = 0;
    std::string error;
    Project project;
    std::mutex mutex;
};

struct LevelLoadJob {
    std::thread worker;
    std::atomic<bool> running = false;
    std::atomic<bool> finished = false;
    std::atomic<bool> success = false;
    int levelIndex = -1;
    std::string levelName;
    std::vector<LevelObject> objects;
    LevelMesh mesh;
    std::vector<TextureAsset> textures;
    std::string error;
    std::mutex mutex;
};

struct AppState {
    Screen screen = Screen::Home;
    Project project;
    std::vector<LevelInfo> levels;
    std::vector<LevelObject> objects;
    LevelMesh mesh;
    std::vector<TextureAsset> textures;
    std::unordered_map<std::string, GpuTexture> textureCache;
    std::vector<ViewportMeshBatch> viewportBatches;
    LevelViewMode cachedViewMode = LevelViewMode::GeometryOnly;
    bool viewportBatchesDirty = true;
    int textureTrianglesDrawn = 0;
    int textureTrianglesMissing = 0;
    int selectedLevel = -1;
    int selectedObject = -1;
    int selectedTexture = -1;
    bool projectLoaded = false;
    EditorMode editorMode = EditorMode::LevelMenu;
    LevelViewMode viewMode = LevelViewMode::GeometryOnly;
    bool rotateGizmo = false;
    int activeGizmoAxis = -1;
    bool levelDirty = false;
    bool pendingExit = false;
    bool pendingLevelMenu = false;

    char projectName[128] = "My SM64 Hack";
    char projectsRoot[512] = "";
    char hackerSm64Path[512] = "";
    char openProjectPath[512] = "";
    char newLevelName[96] = "boxstudio_test";
    std::string status = "Ready.";
    std::string validation = "";
    bool baseLooksValid = false;
    bool baseLooksBuilt = false;
    CopyJob copyJob;
    LevelLoadJob levelLoadJob;

    float cameraYaw = 0.72f;
    float cameraPitch = 0.62f;
    float cameraDistance = 1250.0f;
    bool flyCamera = false;
    Vec3 cameraTarget = { 0.0f, 0.0f, 0.0f };
    Vec3 cameraPosition = { 900.0f, 700.0f, 900.0f };
    ImVec2 cameraPan = ImVec2(0.0f, 0.0f);
};

static AppState* g_appState = nullptr;

static std::string ToUtf8(const fs::path& path)
{
    return path.u8string();
}

static std::string ToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, size - 1)), '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), size, nullptr, nullptr);
    return out;
}

static std::string EscapeJson(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

static std::string Slurp(const fs::path& path);
static std::vector<std::string> SplitArgs(const std::string& args);

struct MacroCall {
    size_t start = 0;
    size_t end = 0;
    std::string name;
    std::string args;
};

static std::vector<MacroCall> FindObjectMacros(const std::string& text);

static std::string SanitizeFolderName(std::string value)
{
    for (char& c : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ';
        if (!ok) c = '_';
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value.empty() ? "Untitled BoxStudio Project" : value;
}

static std::string ToLevelEnumName(const std::string& levelName)
{
    std::string value = "LEVEL_";
    for (char c : levelName) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            value += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        } else {
            value += '_';
        }
    }
    return value;
}

static std::string SanitizeLevelName(std::string value)
{
    for (char& c : value) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            c = '_';
        }
    }
    while (value.find("__") != std::string::npos) {
        value = std::regex_replace(value, std::regex("__+"), "_");
    }
    while (!value.empty() && value.front() == '_') value.erase(value.begin());
    while (!value.empty() && value.back() == '_') value.pop_back();
    return value.empty() ? "boxstudio_level" : value;
}

static fs::path DefaultProjectsRoot()
{
    char* profile = nullptr;
    size_t len = 0;
    if (_dupenv_s(&profile, &len, "USERPROFILE") == 0 && profile != nullptr) {
        fs::path root = fs::path(profile) / "Documents" / "BoxStudio" / "Projects";
        free(profile);
        return root;
    }
    return fs::current_path() / "Projects";
}

static bool Has(const fs::path& root, const char* child)
{
    std::error_code ec;
    return fs::exists(root / child, ec);
}

static bool PickFolder(fs::path& outPath)
{
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) return false;

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr) && item != nullptr) {
            PWSTR path = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr) && path != nullptr) {
                outPath = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return SUCCEEDED(hr) && !outPath.empty();
}

static bool PickTextureFile(fs::path& outPath)
{
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) return false;

    COMDLG_FILTERSPEC filters[] = {
        { L"Texture files", L"*.png;*.bmp;*.tga;*.jpg;*.jpeg;*.rgba16;*.ci4;*.ci8;*.inc.c" },
        { L"All files", L"*.*" }
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr) && item != nullptr) {
            PWSTR path = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr) && path != nullptr) {
                outPath = fs::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return SUCCEEDED(hr) && !outPath.empty();
}

static bool ValidateHackerSm64(const fs::path& root, std::string& reason, bool& built)
{
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        reason = "Folder does not exist.";
        built = false;
        return false;
    }

    std::vector<std::string> missing;
    for (const char* required : { "Makefile", "src", "actors", "assets", "levels", "textures", "include/config" }) {
        if (!Has(root, required)) missing.emplace_back(required);
    }

    built = Has(root, "build");
    if (!missing.empty()) {
        std::ostringstream ss;
        ss << "Missing ";
        for (size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << missing[i];
        }
        reason = ss.str();
        return false;
    }

    reason = built ? "Valid built HackerSM64 workspace detected." : "Valid HackerSM64 workspace detected. Build folder not found yet.";
    return true;
}

static bool WriteManifest(const Project& project, std::string& error)
{
    std::error_code ec;
    fs::create_directories(project.root / ".boxstudio", ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    std::ofstream out(project.manifestPath, std::ios::binary);
    if (!out) {
        error = "Could not write project manifest.";
        return false;
    }

    out << "{\n";
    out << "  \"format\": \"boxstudio.project.v1\",\n";
    out << "  \"name\": \"" << EscapeJson(project.name) << "\",\n";
    out << "  \"game\": \"" << EscapeJson(project.game) << "\",\n";
    out << "  \"decompBase\": \"" << EscapeJson(project.decompBase) << "\",\n";
    out << "  \"projectRoot\": \"" << EscapeJson(ToUtf8(project.root)) << "\",\n";
    out << "  \"sourcePath\": \"" << EscapeJson(ToUtf8(project.sourcePath)) << "\",\n";
    out << "  \"workspacePolicy\": \"copied-decomp-tree-with-boxstudio-metadata\"\n";
    out << "}\n";
    return true;
}

static bool WriteLevelScene(const Project& project, const LevelInfo& level, const std::vector<LevelObject>& objects, std::string& error)
{
    std::error_code ec;
    const fs::path sceneDir = project.root / ".boxstudio" / "levels";
    fs::create_directories(sceneDir, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    const fs::path scenePath = sceneDir / (level.name + ".scene.json");
    std::ofstream out(scenePath, std::ios::binary);
    if (!out) {
        error = "Could not write scene file.";
        return false;
    }

    out << "{\n";
    out << "  \"format\": \"boxstudio.level-scene.v1\",\n";
    out << "  \"level\": \"" << EscapeJson(level.name) << "\",\n";
    out << "  \"objects\": [\n";
    for (size_t i = 0; i < objects.size(); ++i) {
        const LevelObject& object = objects[i];
        out << "    {\n";
        out << "      \"name\": \"" << EscapeJson(object.name) << "\",\n";
        out << "      \"model\": \"" << EscapeJson(object.model) << "\",\n";
        out << "      \"behParam\": \"" << EscapeJson(object.behParam) << "\",\n";
        out << "      \"behavior\": \"" << EscapeJson(object.behavior) << "\",\n";
        out << "      \"position\": [" << object.position.x << ", " << object.position.y << ", " << object.position.z << "],\n";
        out << "      \"rotation\": [" << object.rotation.x << ", " << object.rotation.y << ", " << object.rotation.z << "],\n";
        out << "      \"scale\": " << object.scale << ",\n";
        out << "      \"fromScript\": " << (object.fromScript ? "true" : "false") << "\n";
        out << "    }" << (i + 1 == objects.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";
    return true;
}

static std::string FormatLevelObject(const LevelObject& object)
{
    std::string behParam = object.behParam;
    if (object.model == "MODEL_KOOPA_WITH_SHELL" && object.behavior == "bhvKoopa" && behParam == "0x00000000") {
        behParam = "0x00010000";
    }

    std::ostringstream out;
    out << "        OBJECT(" << object.model << ", "
        << static_cast<int>(object.position.x) << ", "
        << static_cast<int>(object.position.y) << ", "
        << static_cast<int>(object.position.z) << ", "
        << static_cast<int>(object.rotation.x) << ", "
        << static_cast<int>(object.rotation.y) << ", "
        << static_cast<int>(object.rotation.z) << ", "
        << behParam << ", "
        << object.behavior << "),";
    return out.str();
}

static std::string ModelLoadCommandFor(const std::string& model)
{
    static const std::unordered_map<std::string, std::string> known = {
        { "MODEL_GOOMBA", "JUMP_LINK(script_func_global_1)," },
        { "MODEL_KOOPA_WITH_SHELL", "JUMP_LINK(script_func_global_15)," },
        { "MODEL_YELLOW_COIN", "" },
        { "MODEL_STAR", "" },
        { "MODEL_BITS_WARP_PIPE", "" },
        { "MODEL_THI_WARP_PIPE", "" }
    };
    auto it = known.find(model);
    return it == known.end() ? std::string{} : it->second;
}

static std::string ModelSegmentRequirement(const std::string& model)
{
    static const std::unordered_map<std::string, std::string> requirements = {
        { "MODEL_GOOMBA", "_common0_" },
        { "MODEL_KOOPA_WITH_SHELL", "_group14_" },
        { "MODEL_BITS_WARP_PIPE", "_common1_" },
        { "MODEL_THI_WARP_PIPE", "_common1_" }
    };
    auto it = requirements.find(model);
    return it == requirements.end() ? std::string{} : it->second;
}

static std::string ModelSupportProblem(const std::string& script, const LevelObject& object)
{
    if (object.model.empty() || object.behavior.empty()) {
        return "object is missing a model or behavior.";
    }

    const std::string required = ModelSegmentRequirement(object.model);
    if (required.empty() || script.find(required) != std::string::npos) return {};

    if (object.model == "MODEL_KOOPA_WITH_SHELL") {
        return "Koopa needs actor group14 loaded in segment 0x06/0x0D.";
    }
    if (object.model == "MODEL_GOOMBA") {
        return "Goomba needs common0 loaded in segment 0x08/0x0F.";
    }
    return object.model + " needs " + required + " loaded by this level.";
}

static bool LevelScriptSupportsModel(const LevelInfo& level, const std::string& model)
{
    const std::string required = ModelSegmentRequirement(model);
    if (required.empty()) return true;
    const std::string script = Slurp(level.scriptPath);
    return script.find(required) != std::string::npos;
}

static void InjectBoxStudioModelLoads(std::string& script, const std::vector<LevelObject>& objects)
{
    const std::string beginText = "    /* BOXSTUDIO_MODEL_LOADS_BEGIN */";
    const std::string endText = "    /* BOXSTUDIO_MODEL_LOADS_END */";
    size_t begin = script.find(beginText);
    if (begin != std::string::npos) {
        size_t end = script.find(endText, begin);
        if (end != std::string::npos) {
            size_t endLine = script.find('\n', end);
            script.erase(begin, (endLine == std::string::npos ? script.size() : endLine + 1) - begin);
        }
    }

    std::vector<std::string> commands;
    for (const LevelObject& object : objects) {
        if (object.fromScript) continue;
        if (!ModelSupportProblem(script, object).empty()) continue;
        const std::string command = ModelLoadCommandFor(object.model);
        if (!command.empty() && script.find(command) == std::string::npos &&
            std::find(commands.begin(), commands.end(), command) == commands.end()) {
            commands.push_back(command);
        }
    }
    if (commands.empty()) return;

    std::string block = beginText + "\n";
    for (const std::string& command : commands) {
        block += "    " + command + "\n";
    }
    block += endText + "\n";

    size_t insert = script.find("MARIO(");
    if (insert != std::string::npos) {
        insert = script.find('\n', insert);
        if (insert != std::string::npos) {
            size_t afterModelSetup = insert + 1;
            size_t scan = afterModelSetup;
            while (true) {
                const size_t lineEnd = script.find('\n', scan);
                const size_t lineLimit = lineEnd == std::string::npos ? script.size() : lineEnd;
                const std::string line = script.substr(scan, lineLimit - scan);
                if (line.find("JUMP_LINK(script_func_global_") == std::string::npos &&
                    line.find("LOAD_MODEL_FROM_GEO(") == std::string::npos &&
                    line.find("LOAD_MODEL_FROM_DL(") == std::string::npos) {
                    break;
                }
                afterModelSetup = lineEnd == std::string::npos ? script.size() : lineEnd + 1;
                if (lineEnd == std::string::npos) break;
                scan = afterModelSetup;
            }
            script.insert(afterModelSetup, block);
            return;
        }
    }
    insert = script.find("ALLOC_LEVEL_POOL()");
    if (insert != std::string::npos) {
        insert = script.find('\n', insert);
        if (insert != std::string::npos) {
            script.insert(insert + 1, block);
            return;
        }
    }
    insert = script.find("AREA(");
    if (insert != std::string::npos) script.insert(insert, block);
}

static size_t FindAreaCommandInsertPoint(const std::string& script)
{
    const size_t areaStart = script.find("AREA(");
    if (areaStart == std::string::npos) return std::string::npos;

    const size_t areaEnd = script.find("END_AREA()", areaStart);
    if (areaEnd == std::string::npos) return std::string::npos;

    size_t insert = areaEnd;
    const char* markers[] = {
        "\n        TERRAIN(",
        "\n        MACRO_OBJECTS(",
        "\n        SET_BACKGROUND_MUSIC(",
        "\n        TERRAIN_TYPE("
    };
    for (const char* marker : markers) {
        const size_t markerPos = script.find(marker, areaStart);
        if (markerPos != std::string::npos && markerPos < areaEnd && markerPos < insert) {
            insert = markerPos + 1;
        }
    }
    return insert;
}

static std::string RepairLegacyEightArgObjects(const std::string& script)
{
    std::string repaired;
    size_t cursor = 0;
    for (const MacroCall& call : FindObjectMacros(script)) {
        if (call.name != "OBJECT") continue;
        const std::vector<std::string> args = SplitArgs(call.args);
        if (args.size() != 8 || call.start < cursor) continue;

        repaired.append(script, cursor, call.start - cursor);
        const bool lastIsBehavior = args[7].find("bhv") == 0;
        repaired += "OBJECT(";
        if (lastIsBehavior) {
            for (size_t i = 0; i < 7; ++i) {
                if (i > 0) repaired += ", ";
                repaired += args[i];
            }
            repaired += ", 0x00000000, ";
            repaired += args[7];
        } else {
            for (size_t i = 0; i < args.size(); ++i) {
                if (i > 0) repaired += ", ";
                repaired += args[i];
            }
            repaired += ", bhvStaticObject";
        }
        repaired += ")";
        cursor = call.end;
    }
    if (cursor == 0) return script;
    repaired.append(script, cursor, std::string::npos);
    return repaired;
}

static void EnsureHackerSm64ScriptIncludes(std::string& script, const std::string& levelName)
{
    if (script.find("#include \"level_commands.h\"") != std::string::npos) return;

    std::string includes;
    includes += "#include <ultra64.h>\n";
    includes += "#include \"sm64.h\"\n";
    includes += "#include \"behavior_data.h\"\n";
    includes += "#include \"model_ids.h\"\n";
    includes += "#include \"seq_ids.h\"\n";
    includes += "#include \"segment_symbols.h\"\n";
    includes += "#include \"level_commands.h\"\n\n";
    includes += "#include \"game/level_update.h\"\n\n";
    includes += "#include \"levels/scripts.h\"\n\n";
    includes += "#include \"actors/common1.h\"\n\n";
    includes += "#include \"make_const_nonconst.h\"\n";
    includes += "#include \"levels/" + levelName + "/header.h\"\n";

    std::regex includePattern(R"((?:#include[^\n]*\n)+\s*)");
    std::smatch match;
    if (std::regex_search(script, match, includePattern) && match.position() == 0) {
        script.replace(0, static_cast<size_t>(match.length()), includes + "\n");
    } else {
        script.insert(0, includes + "\n");
    }
}

static void RepairGeneratedLevelEnumReferences(std::string& script, const std::string& levelName)
{
    const std::string legacy = "LEVEL_" + levelName;
    const std::string fixed = ToLevelEnumName(levelName);
    size_t pos = 0;
    while ((pos = script.find(legacy, pos)) != std::string::npos) {
        script.replace(pos, legacy.size(), fixed);
        pos += fixed.size();
    }
}

static bool WriteObjectsToLevelScript(const LevelInfo& level, const std::vector<LevelObject>& objects, std::string& error, std::string* warning = nullptr)
{
    std::string script = Slurp(level.scriptPath);
    if (script.empty()) {
        error = "Level script is empty or missing.";
        return false;
    }
    EnsureHackerSm64ScriptIncludes(script, level.name);
    RepairGeneratedLevelEnumReferences(script, level.name);
    script = RepairLegacyEightArgObjects(script);
    InjectBoxStudioModelLoads(script, objects);

    const char* beginMarker = "        /* BOXSTUDIO_OBJECTS_BEGIN */\n";
    const char* endMarker = "        /* BOXSTUDIO_OBJECTS_END */\n";
    const std::string beginText = "        /* BOXSTUDIO_OBJECTS_BEGIN */";
    const std::string endText = "        /* BOXSTUDIO_OBJECTS_END */";

    size_t begin = script.find(beginText);
    if (begin != std::string::npos) {
        size_t end = script.find(endText, begin);
        if (end != std::string::npos) {
            size_t endLine = script.find('\n', end);
            script.erase(begin, (endLine == std::string::npos ? script.size() : endLine + 1) - begin);
        }
    }

    std::string block = beginMarker;
    int written = 0;
    std::vector<std::string> skipped;
    for (const LevelObject& object : objects) {
        if (object.fromScript) continue;
        const std::string problem = ModelSupportProblem(script, object);
        if (!problem.empty()) {
            skipped.push_back(object.name + " (" + problem + ")");
            continue;
        }
        block += FormatLevelObject(object);
        block += "\n";
        ++written;
    }
    block += endMarker;

    if (written > 0) {
        const size_t insert = FindAreaCommandInsertPoint(script);
        if (insert == std::string::npos) {
            error = "Could not find an AREA block to insert BoxStudio objects.";
            return false;
        }
        script.insert(insert, block);
    }

    std::ofstream out(level.scriptPath, std::ios::binary);
    if (!out) {
        error = "Could not write level script.";
        return false;
    }
    out << script;
    if (warning != nullptr) {
        warning->clear();
        if (!skipped.empty()) {
            *warning = "Skipped unsupported object";
            if (skipped.size() != 1) *warning += "s";
            *warning += ": ";
            for (size_t i = 0; i < skipped.size(); ++i) {
                if (i > 0) *warning += "; ";
                *warning += skipped[i];
            }
        }
    }
    return true;
}

static bool CopyWorkspace(const fs::path& source, const fs::path& dest, std::string& error, std::atomic<int>* copied)
{
    std::error_code ec;
    fs::create_directories(dest, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    const fs::path canonicalSource = fs::weakly_canonical(source, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    const fs::path canonicalDest = fs::weakly_canonical(dest, ec);
    ec.clear();
    if (copied != nullptr) copied->store(0);

    try {
        for (fs::recursive_directory_iterator it(canonicalSource), end; it != end; ++it) {
            const fs::path relative = fs::relative(it->path(), canonicalSource);
            if (!relative.empty() && *relative.begin() == ".git") {
                if (it->is_directory()) it.disable_recursion_pending();
                continue;
            }

            const fs::path target = canonicalDest / relative;
            if (it->is_directory()) {
                fs::create_directories(target);
            } else if (it->is_regular_file()) {
                fs::create_directories(target.parent_path());
                fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing);
                if (copied != nullptr) copied->fetch_add(1);
            }
        }
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    return true;
}

static std::string Slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string StripCComments(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    bool lineComment = false;
    bool blockComment = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const char n = (i + 1 < text.size()) ? text[i + 1] : '\0';
        if (lineComment) {
            if (c == '\n') {
                lineComment = false;
                out += c;
            } else {
                out += ' ';
            }
        } else if (blockComment) {
            if (c == '*' && n == '/') {
                blockComment = false;
                out += "  ";
                ++i;
            } else {
                out += (c == '\n') ? '\n' : ' ';
            }
        } else if (c == '/' && n == '/') {
            lineComment = true;
            out += "  ";
            ++i;
        } else if (c == '/' && n == '*') {
            blockComment = true;
            out += "  ";
            ++i;
        } else {
            out += c;
        }
    }
    return out;
}

static std::string CleanToken(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

static std::string JsonString(const std::string& text, const char* key)
{
    std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(text, match, pattern) && match.size() > 1) return match[1].str();
    return {};
}

static bool LoadProject(const fs::path& rootOrManifest, Project& project, std::string& error)
{
    fs::path manifest = rootOrManifest;
    if (fs::is_directory(rootOrManifest)) manifest = rootOrManifest / ".boxstudio" / "boxstudio.project.json";
    if (!fs::exists(manifest)) {
        error = "No .boxstudio/boxstudio.project.json found.";
        return false;
    }

    const std::string text = Slurp(manifest);
    project.name = JsonString(text, "name");
    project.game = JsonString(text, "game");
    project.decompBase = JsonString(text, "decompBase");
    project.root = manifest.parent_path().parent_path();
    project.sourcePath = JsonString(text, "sourcePath");
    project.manifestPath = manifest;
    if (project.name.empty()) project.name = project.root.filename().string();
    if (project.decompBase.empty()) project.decompBase = "HackerSM64";
    return true;
}

static std::vector<LevelInfo> DiscoverLevels(const fs::path& projectRoot)
{
    std::vector<LevelInfo> levels;
    const fs::path levelsDir = projectRoot / "levels";
    std::error_code ec;
    if (!fs::exists(levelsDir, ec)) return levels;

    for (const fs::directory_entry& entry : fs::directory_iterator(levelsDir, ec)) {
        if (!entry.is_directory()) continue;
        LevelInfo info;
        info.name = entry.path().filename().string();
        info.path = entry.path();
        info.scriptPath = entry.path() / "script.c";
        levels.push_back(info);
    }

    std::sort(levels.begin(), levels.end(), [](const LevelInfo& a, const LevelInfo& b) {
        return a.name < b.name;
    });
    return levels;
}

static std::vector<std::string> SplitArgs(const std::string& args)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    for (char c : args) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(CleanToken(current));
            current.clear();
        } else {
            current += c;
        }
    }
    current = CleanToken(current);
    if (!current.empty()) out.push_back(current);
    return out;
}

static float NumberOrZero(const std::string& token)
{
    try {
        size_t index = 0;
        float value = std::stof(token, &index);
        return value;
    } catch (...) {
        return 0.0f;
    }
}

static std::vector<MacroCall> FindObjectMacros(const std::string& text)
{
    std::vector<MacroCall> calls;
    size_t pos = 0;
    while ((pos = text.find("OBJECT", pos)) != std::string::npos) {
        if (pos > 0) {
            const unsigned char before = static_cast<unsigned char>(text[pos - 1]);
            if (std::isalnum(before) || before == '_') {
                pos += 6;
                continue;
            }
        }

        std::string name = "OBJECT";
        size_t nameEnd = pos + 6;
        if (text.compare(nameEnd, 10, "_WITH_ACTS") == 0) {
            name = "OBJECT_WITH_ACTS";
            nameEnd += 10;
        }
        if (nameEnd >= text.size() || text[nameEnd] != '(') {
            pos = nameEnd;
            continue;
        }

        int depth = 0;
        size_t close = std::string::npos;
        for (size_t i = nameEnd; i < text.size(); ++i) {
            if (text[i] == '(') ++depth;
            if (text[i] == ')') {
                --depth;
                if (depth == 0) {
                    close = i;
                    break;
                }
            }
        }
        if (close == std::string::npos) break;

        calls.push_back({ pos, close + 1, name, text.substr(nameEnd + 1, close - nameEnd - 1) });
        pos = close + 1;
    }
    return calls;
}

static int IntOrZero(const std::string& token)
{
    try {
        size_t index = 0;
        return std::stoi(token, &index, 0);
    } catch (...) {
        return 0;
    }
}

static int SignedByte(int value)
{
    value &= 0xff;
    return value >= 128 ? value - 256 : value;
}

static float TextureScaleOrOne(const std::string& token)
{
    const int value = IntOrZero(token);
    if (value <= 0 || value >= 0xffff) return 1.0f;
    return std::clamp(static_cast<float>(value) / 65535.0f, 1.0f / 1024.0f, 1.0f);
}

static bool HasTextureFlag(const std::string& token, const char* flag)
{
    return token.find(flag) != std::string::npos;
}

static ImU32 TextureColor(const std::string& texture, ImU32 fallback)
{
    if (texture.empty()) return fallback;
    unsigned int hash = 2166136261u;
    for (char c : texture) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 16777619u;
    }
    const int r = 80 + static_cast<int>(hash & 0x7f);
    const int g = 80 + static_cast<int>((hash >> 8) & 0x7f);
    const int b = 80 + static_cast<int>((hash >> 16) & 0x7f);
    return IM_COL32(r, g, b, 235);
}

struct RenderVertex {
    Vec3 pos;
    ImVec2 uv = ImVec2(0.0f, 0.0f);
    Vec3 normal = { 0.0f, 1.0f, 0.0f };
    ImU32 color = IM_COL32(160, 160, 160, 230);
};

struct RenderInstance {
    std::vector<std::string> roots;
    Vec3 translation;
    float yawDegrees = 0.0f;
};

static Vec3 TransformRenderPoint(const Vec3& point, const RenderInstance& instance)
{
    const float radians = instance.yawDegrees * 3.1415926535f / 180.0f;
    const float c = cosf(radians);
    const float s = sinf(radians);
    return {
        point.x * c + point.z * s + instance.translation.x,
        point.y + instance.translation.y,
        -point.x * s + point.z * c + instance.translation.z
    };
}

static ImU32 AverageColor(ImU32 a, ImU32 b, ImU32 c)
{
    const int ar = a & 0xff, ag = (a >> 8) & 0xff, ab = (a >> 16) & 0xff;
    const int br = b & 0xff, bg = (b >> 8) & 0xff, bb = (b >> 16) & 0xff;
    const int cr = c & 0xff, cg = (c >> 8) & 0xff, cb = (c >> 16) & 0xff;
    return IM_COL32((ar + br + cr) / 3, (ag + bg + cg) / 3, (ab + bb + cb) / 3, 235);
}

static std::unordered_map<std::string, std::vector<std::string>> CollectGeoDisplayLists(const LevelInfo& level)
{
    std::unordered_map<std::string, std::vector<std::string>> result;
    std::error_code ec;
    const fs::path areas = level.path / "areas";
    if (!fs::exists(areas, ec)) return result;

    std::regex displayPattern(R"(GEO_DISPLAY_LIST\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\))");
    for (fs::directory_iterator it(areas, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory()) continue;
        const fs::path geo = it->path() / "geo.inc.c";
        if (!fs::exists(geo, ec)) continue;
        const std::string areaName = it->path().filename().string();
        const std::string text = StripCComments(Slurp(geo));
        for (auto match = std::sregex_iterator(text.begin(), text.end(), displayPattern), mend = std::sregex_iterator(); match != mend; ++match) {
            result[areaName].push_back((*match)[2].str());
        }
    }
    return result;
}

static std::unordered_map<std::string, fs::path> CollectGeoLayoutFiles(const LevelInfo& level)
{
    std::unordered_map<std::string, fs::path> result;
    std::error_code ec;
    const fs::path projectRoot = level.path.parent_path().parent_path();
    std::vector<fs::path> roots = { level.path, projectRoot / "actors" };
    std::regex layoutPattern(R"(const\s+GeoLayout\s+([A-Za-z0-9_]+)\s*\[\]\s*=\s*\{)");
    for (const fs::path& root : roots) {
        if (!fs::exists(root, ec)) continue;
        ec.clear();
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().filename() != "geo.inc.c") continue;
            const std::string text = StripCComments(Slurp(it->path()));
            for (auto match = std::sregex_iterator(text.begin(), text.end(), layoutPattern), mend = std::sregex_iterator(); match != mend; ++match) {
                result[(*match)[1].str()] = it->path();
            }
        }
    }
    return result;
}

static std::string ExtractGeoLayoutBody(const std::string& text, const std::string& layoutName)
{
    const std::string needle = "const GeoLayout " + layoutName;
    size_t pos = text.find(needle);
    if (pos == std::string::npos) return {};
    pos = text.find('{', pos);
    if (pos == std::string::npos) return {};
    int depth = 0;
    for (size_t i = pos; i < text.size(); ++i) {
        if (text[i] == '{') ++depth;
        if (text[i] == '}') {
            --depth;
            if (depth == 0) return text.substr(pos + 1, i - pos - 1);
        }
    }
    return {};
}

static std::vector<std::string> CollectDisplayListsFromGeoFile(const fs::path& geoFile, const std::string& layoutName)
{
    std::vector<std::string> roots;
    const std::string text = StripCComments(Slurp(geoFile));
    const std::string body = ExtractGeoLayoutBody(text, layoutName);
    const std::string& searchText = body.empty() ? text : body;
    std::regex pattern(R"(GEO_DISPLAY_LIST\s*\(\s*[A-Za-z0-9_]+,\s*([A-Za-z0-9_]+)\s*\)|GEO_ANIMATED_PART\s*\(\s*[A-Za-z0-9_]+,\s*[-+]?\d+,\s*[-+]?\d+,\s*[-+]?\d+,\s*([A-Za-z0-9_]+)\s*\))");
    for (auto it = std::sregex_iterator(searchText.begin(), searchText.end(), pattern), end = std::sregex_iterator(); it != end; ++it) {
        const std::string root = (*it)[1].matched ? (*it)[1].str() : (*it)[2].str();
        if (root != "NULL") roots.push_back(root);
    }
    return roots;
}

static int AreaIndexFromPath(const fs::path& file)
{
    const std::vector<std::string> parts = [&]() {
        std::vector<std::string> out;
        for (const fs::path& part : file) out.push_back(part.string());
        return out;
    }();
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i] == "areas") return IntOrZero(parts[i + 1]);
    }
    return -1;
}

static std::vector<std::string> RootDisplayListsForFile(const fs::path& file, const std::unordered_map<std::string, std::vector<std::string>>& geoDisplayLists)
{
    const int areaIndex = AreaIndexFromPath(file);
    if (areaIndex < 0) return {};
    auto found = geoDisplayLists.find(std::to_string(areaIndex));
    return found == geoDisplayLists.end() ? std::vector<std::string>{} : found->second;
}

static std::unordered_map<std::string, std::vector<RenderInstance>> CollectSpecialGeometryInstances(const LevelInfo& level)
{
    std::unordered_map<std::string, std::vector<RenderInstance>> instances;
    const std::unordered_map<std::string, fs::path> geoFiles = CollectGeoLayoutFiles(level);
    std::unordered_map<std::string, std::string> modelToGeo;

    const std::string script = StripCComments(Slurp(level.scriptPath));
    std::regex loadPattern(R"(LOAD_MODEL_FROM_GEO\s*\(\s*(MODEL_[A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\))");
    for (auto it = std::sregex_iterator(script.begin(), script.end(), loadPattern), end = std::sregex_iterator(); it != end; ++it) {
        modelToGeo[(*it)[1].str()] = (*it)[2].str();
    }

    std::error_code ec;
    std::vector<fs::path> collisionFiles;
    for (fs::recursive_directory_iterator it(level.path, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().filename() == "collision.inc.c") collisionFiles.push_back(it->path());
    }

    std::regex specialPattern(R"(SPECIAL_OBJECT(?:_WITH_YAW)?(?:_AND_PARAM)?\s*\(\s*(?:/\*preset\*/\s*)?(special_[A-Za-z0-9_]+)\s*,\s*(?:/\*pos\*/\s*)?([-+]?\d+)\s*,\s*([-+]?\d+)\s*,\s*([-+]?\d+)(?:\s*,\s*(?:/\*yaw\*/\s*)?([-+]?\d+))?)");
    for (const fs::path& collisionFile : collisionFiles) {
        const std::string text = StripCComments(Slurp(collisionFile));
        for (auto it = std::sregex_iterator(text.begin(), text.end(), specialPattern), end = std::sregex_iterator(); it != end; ++it) {
            const std::string preset = (*it)[1].str();
            std::string model;
            if (preset.find("special_level_geo_") == 0) {
                std::string suffix = preset.substr(strlen("special_level_geo_"));
                std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                model = "MODEL_LEVEL_GEOMETRY_" + suffix;
            } else if (preset == "special_bubble_tree") {
                model = "MODEL_CASTLE_GROUNDS_BUBBLY_TREE";
            } else if (preset == "special_spiky_tree") {
                model = "MODEL_COURTYARD_SPIKY_TREE";
            }
            if (model.empty()) continue;
            auto geoName = modelToGeo.find(model);
            if (geoName == modelToGeo.end()) continue;
            auto geoFile = geoFiles.find(geoName->second);
            if (geoFile == geoFiles.end()) continue;
            const fs::path modelFile = geoFile->second.parent_path() / "model.inc.c";
            if (!fs::exists(modelFile, ec)) continue;
            RenderInstance instance;
            instance.roots = CollectDisplayListsFromGeoFile(geoFile->second, geoName->second);
            instance.translation = {
                static_cast<float>(IntOrZero((*it)[2].str())),
                static_cast<float>(IntOrZero((*it)[3].str())),
                static_cast<float>(IntOrZero((*it)[4].str()))
            };
            instance.yawDegrees = (*it)[5].matched ? static_cast<float>(IntOrZero((*it)[5].str())) * (360.0f / 256.0f) : 0.0f;
            if (!instance.roots.empty()) instances[ToUtf8(modelFile)].push_back(instance);
        }
    }
    return instances;
}

static void AppendWaterBoxes(const LevelInfo& level, LevelMesh& mesh)
{
    std::error_code ec;
    if (!fs::exists(level.path, ec)) return;

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(level.path, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string filename = it->path().filename().string();
        if (filename == "collision.inc.c" || filename.find("collision") != std::string::npos) files.push_back(it->path());
    }

    std::regex waterPattern(R"(COL_WATER_BOX\s*\(\s*\d+\s*,\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*\))");
    for (const fs::path& file : files) {
        const std::string text = StripCComments(Slurp(file));
        for (auto it = std::sregex_iterator(text.begin(), text.end(), waterPattern), end = std::sregex_iterator(); it != end; ++it) {
            const float x1 = static_cast<float>(IntOrZero((*it)[1].str()));
            const float z1 = static_cast<float>(IntOrZero((*it)[2].str()));
            const float x2 = static_cast<float>(IntOrZero((*it)[3].str()));
            const float z2 = static_cast<float>(IntOrZero((*it)[4].str()));
            const float y = static_cast<float>(IntOrZero((*it)[5].str()));
            const int base = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back({ x1, y, z1 });
            mesh.vertices.push_back({ x1, y, z2 });
            mesh.vertices.push_back({ x2, y, z2 });
            mesh.vertices.push_back({ x2, y, z1 });

            MeshTriangle a;
            a.a = base;
            a.b = base + 1;
            a.c = base + 2;
            a.uva = ImVec2(0.0f, 0.0f);
            a.uvb = ImVec2(0.0f, 8.0f);
            a.uvc = ImVec2(8.0f, 8.0f);
            a.surface = "SURFACE_WATER";
            a.texture = kEditorWaterTexture;
            a.color = IM_COL32(45, 125, 190, 150);
            MeshTriangle b = a;
            b.a = base;
            b.b = base + 2;
            b.c = base + 3;
            b.uva = ImVec2(0.0f, 0.0f);
            b.uvb = ImVec2(8.0f, 8.0f);
            b.uvc = ImVec2(8.0f, 0.0f);
            mesh.triangles.push_back(a);
            mesh.triangles.push_back(b);
            mesh.sources.push_back(file);
        }
    }
}

static LevelMesh LoadRenderMesh(const LevelInfo& level)
{
    LevelMesh mesh;
    mesh.renderMesh = true;
    std::error_code ec;
    if (!fs::exists(level.path, ec)) return mesh;
    const std::unordered_map<std::string, std::vector<std::string>> geoDisplayLists = CollectGeoDisplayLists(level);
    const std::unordered_map<std::string, std::vector<RenderInstance>> specialInstances = CollectSpecialGeometryInstances(level);

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(level.path, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().filename() == "model.inc.c") files.push_back(it->path());
    }
    for (const auto& item : specialInstances) {
        fs::path modelPath = fs::path(item.first);
        if (std::find(files.begin(), files.end(), modelPath) == files.end()) files.push_back(modelPath);
    }

    for (const fs::path& file : files) {
        const std::string text = StripCComments(Slurp(file));
        if (text.empty()) continue;
        std::vector<RenderInstance> instances;
        const std::vector<std::string> areaRoots = RootDisplayListsForFile(file, geoDisplayLists);
        if (!areaRoots.empty()) {
            RenderInstance instance;
            instance.roots = areaRoots;
            instances.push_back(instance);
        }
        auto special = specialInstances.find(ToUtf8(file));
        if (special != specialInstances.end()) {
            instances.insert(instances.end(), special->second.begin(), special->second.end());
        }
        if (instances.empty()) continue;

        std::unordered_map<std::string, std::vector<RenderVertex>> arrays;
        std::unordered_map<std::string, std::string> gfxBlocks;
        std::regex arrayPattern(R"(static\s+const\s+Vtx\s+([A-Za-z0-9_]+)\s*\[\]\s*=\s*\{([\s\S]*?)\};)");
        std::regex vertexPattern(R"(\{\{\{\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*\}\s*,\s*0\s*,\s*\{\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*\}\s*,\s*\{\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*,\s*(0x[0-9a-fA-F]+|\d+)\s*\}\}\})");
        for (auto ait = std::sregex_iterator(text.begin(), text.end(), arrayPattern), end = std::sregex_iterator(); ait != end; ++ait) {
            std::vector<RenderVertex> vertices;
            const std::string body = (*ait)[2].str();
            for (auto vit = std::sregex_iterator(body.begin(), body.end(), vertexPattern), vend = std::sregex_iterator(); vit != vend; ++vit) {
                const int r = std::clamp(IntOrZero((*vit)[6].str()), 0, 255);
                const int g = std::clamp(IntOrZero((*vit)[7].str()), 0, 255);
                const int b = std::clamp(IntOrZero((*vit)[8].str()), 0, 255);
                const int a = std::clamp(IntOrZero((*vit)[9].str()), 0, 255);
                vertices.push_back({
                    { static_cast<float>(IntOrZero((*vit)[1].str())), static_cast<float>(IntOrZero((*vit)[2].str())), static_cast<float>(IntOrZero((*vit)[3].str())) },
                    ImVec2(static_cast<float>(IntOrZero((*vit)[4].str())) / 32.0f, static_cast<float>(IntOrZero((*vit)[5].str())) / 32.0f),
                    { static_cast<float>(SignedByte(r)) / 127.0f, static_cast<float>(SignedByte(g)) / 127.0f, static_cast<float>(SignedByte(b)) / 127.0f },
                    IM_COL32(r, g, b, std::max(190, a))
                });
            }
            arrays[(*ait)[1].str()] = vertices;
        }

        std::regex gfxPattern(R"((?:static\s+)?const\s+Gfx\s+([A-Za-z0-9_]+)\s*\[\]\s*=\s*\{([\s\S]*?)\};)");
        for (auto git = std::sregex_iterator(text.begin(), text.end(), gfxPattern), end = std::sregex_iterator(); git != end; ++git) {
            gfxBlocks[(*git)[1].str()] = (*git)[2].str();
        }
        bool hasReferencedList = false;
        for (const RenderInstance& instance : instances) {
            for (const std::string& root : instance.roots) {
                if (gfxBlocks.find(root) != gfxBlocks.end()) {
                    hasReferencedList = true;
                    break;
                }
            }
            if (hasReferencedList) break;
        }
        if (!hasReferencedList) continue;
        mesh.sources.push_back(file);

        std::array<RenderVertex, 64> slots{};
        std::string currentTexture;
        std::string currentTextureFormat = "G_IM_FMT_RGBA";
        std::string currentTextureSize = "G_IM_SIZ_16b";
        int currentTextureWidth = 32;
        int currentTextureHeight = 32;
        bool currentTextureClamp = false;
        bool currentTextureClampS = false;
        bool currentTextureClampT = false;
        bool currentTextureMirrorS = false;
        bool currentTextureMirrorT = false;
        float currentTextureScaleS = 1.0f;
        float currentTextureScaleT = 1.0f;
        bool currentTextureEnabled = true;
        bool currentTextureGen = false;
        std::string currentVertexArray;
        const RenderInstance* activeInstance = nullptr;
        std::regex commandPattern(R"(gsDPSetTextureImage\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,[^,]+,\s*([A-Za-z0-9_]+)\s*\)|gsDPLoadBlock\s*\([^,]+,[^,]+,[^,]+,\s*(\d+)\s*\*\s*(\d+)\s*-\s*1|gsDPSetTile\s*\(([^)]*)\)|gsDPSetTileSize\s*\([^,]+,[^,]+,[^,]+,\s*\(?\s*(\d+)\s*-\s*1\s*\)?\s*<<\s*G_TEXTURE_IMAGE_FRAC\s*,\s*\(?\s*(\d+)\s*-\s*1\s*\)?\s*<<\s*G_TEXTURE_IMAGE_FRAC\s*\)|gsSPVertex\s*\(\s*([A-Za-z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)|gsSP1Triangle\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)|gsSP2Triangles\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*0x[0-9a-fA-F]+,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)|gsSP(?:DisplayList|BranchList)\s*\(\s*([A-Za-z0-9_]+)\s*\)|gsDPLoadTextureBlock\s*\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*[^,]+,\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^)]+)\)|gsSPTexture\s*\(\s*([^,]+)\s*,\s*([^,]+)\s*,[^,]+,[^,]+,\s*(G_ON|G_OFF)\s*\)|gsSPSetGeometryMode\s*\(([^)]*)\)|gsSPClearGeometryMode\s*\(([^)]*)\))");
        auto emitTriangleVertices = [&](const RenderVertex& va, const RenderVertex& vb, const RenderVertex& vc) {
            if (activeInstance == nullptr) return;
            const int base = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back(TransformRenderPoint(va.pos, *activeInstance));
            mesh.vertices.push_back(TransformRenderPoint(vb.pos, *activeInstance));
            mesh.vertices.push_back(TransformRenderPoint(vc.pos, *activeInstance));
            auto textureGenUv = [&](const RenderVertex& vertex) {
                return ImVec2(
                    (vertex.normal.x * 0.5f + 0.5f) * static_cast<float>(std::max(1, currentTextureWidth)),
                    (0.5f - vertex.normal.y * 0.5f) * static_cast<float>(std::max(1, currentTextureHeight)));
            };
            MeshTriangle tri;
            tri.a = base;
            tri.b = base + 1;
            tri.c = base + 2;
            tri.uva = currentTextureGen ? textureGenUv(va) : va.uv;
            tri.uvb = currentTextureGen ? textureGenUv(vb) : vb.uv;
            tri.uvc = currentTextureGen ? textureGenUv(vc) : vc.uv;
            tri.texture = currentTextureEnabled ? currentTexture : "";
            tri.textureFormat = currentTextureFormat;
            tri.textureSize = currentTextureSize;
            tri.textureWidth = currentTextureWidth;
            tri.textureHeight = currentTextureHeight;
            tri.textureClamp = currentTextureClamp;
            tri.textureClampS = currentTextureClampS;
            tri.textureClampT = currentTextureClampT;
            tri.textureMirrorS = currentTextureMirrorS;
            tri.textureMirrorT = currentTextureMirrorT;
            tri.textureScaleS = currentTextureGen ? 1.0f : currentTextureScaleS;
            tri.textureScaleT = currentTextureGen ? 1.0f : currentTextureScaleT;
            tri.textureGen = currentTextureGen;
            tri.surface = "RENDER";
            tri.color = AverageColor(va.color, vb.color, vc.color);
            mesh.triangles.push_back(tri);
        };
        auto emitTri = [&](int ia, int ib, int ic) {
            if (ia < 0 || ia >= 64 || ib < 0 || ib >= 64 || ic < 0 || ic >= 64) return;
            if (activeInstance == nullptr) return;
            if (currentVertexArray == "tree_seg3_vertex_bubbly_left_side" && ia == 0 && ib == 1 && ic == 2) {
                RenderVertex topLeft = slots[0];
                topLeft.pos.y = slots[2].pos.y;
                topLeft.uv.y = slots[2].uv.y;
                emitTriangleVertices(slots[0], slots[1], slots[2]);
                emitTriangleVertices(slots[0], slots[2], topLeft);
                return;
            }
            if (currentVertexArray == "tree_seg3_vertex_bubbly_right_side" && ia == 0 && ib == 1 && ic == 2) {
                RenderVertex topRight = slots[1];
                topRight.pos.y = slots[2].pos.y;
                topRight.uv.y = slots[2].uv.y;
                emitTriangleVertices(slots[0], slots[1], topRight);
                emitTriangleVertices(slots[0], topRight, slots[2]);
                return;
            }
            emitTriangleVertices(slots[ia], slots[ib], slots[ic]);
        };

        std::vector<std::string> stack;
        auto runBlock = [&](const std::string& name, auto&& runBlockRef) -> void {
            if (std::find(stack.begin(), stack.end(), name) != stack.end()) return;
            auto block = gfxBlocks.find(name);
            if (block == gfxBlocks.end()) return;
            stack.push_back(name);
            for (auto it = std::sregex_iterator(block->second.begin(), block->second.end(), commandPattern), end = std::sregex_iterator(); it != end; ++it) {
                if ((*it)[1].matched) {
                    currentTextureFormat = (*it)[1].str();
                    currentTextureSize = (*it)[2].str();
                    currentTexture = (*it)[3].str();
                } else if ((*it)[4].matched) {
                    currentTextureWidth = std::max(1, IntOrZero((*it)[4].str()));
                    currentTextureHeight = std::max(1, IntOrZero((*it)[5].str()));
                } else if ((*it)[6].matched) {
                    const std::vector<std::string> args = SplitArgs((*it)[6].str());
                    if (args.size() >= 12) {
                        const std::string& tMode = args[6];
                        const std::string& sMode = args[9];
                        currentTextureClampS = HasTextureFlag(sMode, "G_TX_CLAMP");
                        currentTextureClampT = HasTextureFlag(tMode, "G_TX_CLAMP");
                        currentTextureMirrorS = HasTextureFlag(sMode, "G_TX_MIRROR");
                        currentTextureMirrorT = HasTextureFlag(tMode, "G_TX_MIRROR");
                        currentTextureClamp = currentTextureClampS || currentTextureClampT;
                    }
                } else if ((*it)[7].matched) {
                    currentTextureWidth = std::max(1, IntOrZero((*it)[7].str()));
                    currentTextureHeight = std::max(1, IntOrZero((*it)[8].str()));
                } else if ((*it)[9].matched) {
                    const std::string arrayName = (*it)[9].str();
                    const int count = IntOrZero((*it)[10].str());
                    const int start = IntOrZero((*it)[11].str());
                    auto found = arrays.find(arrayName);
                    if (found == arrays.end()) continue;
                    currentVertexArray = arrayName;
                    for (int i = 0; i < count && i < static_cast<int>(found->second.size()) && start + i < 64; ++i) {
                        slots[start + i] = found->second[i];
                    }
                } else if ((*it)[12].matched) {
                    emitTri(IntOrZero((*it)[12].str()), IntOrZero((*it)[13].str()), IntOrZero((*it)[14].str()));
                } else if ((*it)[15].matched) {
                    emitTri(IntOrZero((*it)[15].str()), IntOrZero((*it)[16].str()), IntOrZero((*it)[17].str()));
                    emitTri(IntOrZero((*it)[18].str()), IntOrZero((*it)[19].str()), IntOrZero((*it)[20].str()));
                } else if ((*it)[21].matched) {
                    runBlockRef((*it)[21].str(), runBlockRef);
                } else if ((*it)[22].matched) {
                    currentTexture = (*it)[22].str();
                    currentTextureFormat = (*it)[23].str();
                    currentTextureSize = (*it)[24].str();
                    currentTextureWidth = std::max(1, IntOrZero((*it)[25].str()));
                    currentTextureHeight = std::max(1, IntOrZero((*it)[26].str()));
                    const std::string sMode = (*it)[27].str();
                    const std::string tMode = (*it)[28].str();
                    currentTextureClampS = HasTextureFlag(sMode, "G_TX_CLAMP");
                    currentTextureClampT = HasTextureFlag(tMode, "G_TX_CLAMP");
                    currentTextureMirrorS = HasTextureFlag(sMode, "G_TX_MIRROR");
                    currentTextureMirrorT = HasTextureFlag(tMode, "G_TX_MIRROR");
                    currentTextureClamp = currentTextureClampS || currentTextureClampT;
                } else if ((*it)[33].matched) {
                    currentTextureScaleS = TextureScaleOrOne((*it)[33].str());
                    currentTextureScaleT = TextureScaleOrOne((*it)[34].str());
                    currentTextureEnabled = (*it)[35].str() == "G_ON";
                } else if ((*it)[36].matched) {
                    if (HasTextureFlag((*it)[36].str(), "G_TEXTURE_GEN")) currentTextureGen = true;
                } else if ((*it)[37].matched) {
                    if (HasTextureFlag((*it)[37].str(), "G_TEXTURE_GEN")) currentTextureGen = false;
                }
            }
            stack.pop_back();
        };

        for (const RenderInstance& instance : instances) {
            slots = {};
            currentTexture.clear();
            currentTextureFormat = "G_IM_FMT_RGBA";
            currentTextureSize = "G_IM_SIZ_16b";
            currentTextureWidth = 32;
            currentTextureHeight = 32;
            currentTextureClamp = false;
            currentTextureClampS = false;
            currentTextureClampT = false;
            currentTextureMirrorS = false;
            currentTextureMirrorT = false;
            currentTextureScaleS = 1.0f;
            currentTextureScaleT = 1.0f;
            currentTextureEnabled = true;
            currentTextureGen = false;
            stack.clear();
            activeInstance = &instance;
            for (const std::string& root : instance.roots) {
                runBlock(root, runBlock);
            }
        }
    }

    return mesh;
}

static LevelMesh LoadCollisionMesh(const LevelInfo& level)
{
    LevelMesh mesh;
    std::error_code ec;
    if (!fs::exists(level.path, ec)) return mesh;

    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(level.path, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string filename = it->path().filename().string();
        if (filename == "collision.inc.c" || filename.find("collision") != std::string::npos) {
            files.push_back(it->path());
        }
    }

    for (const fs::path& file : files) {
        const std::string text = StripCComments(Slurp(file));
        if (text.empty()) continue;
        mesh.sources.push_back(file);
        const int baseVertex = static_cast<int>(mesh.vertices.size());

        std::regex vertexPattern(R"(COL_VERTEX\s*\(\s*([-+]?0x[0-9a-fA-F]+|[-+]?\d+)\s*,\s*([-+]?0x[0-9a-fA-F]+|[-+]?\d+)\s*,\s*([-+]?0x[0-9a-fA-F]+|[-+]?\d+)\s*\))");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), vertexPattern), end = std::sregex_iterator(); it != end; ++it) {
            mesh.vertices.push_back({
                static_cast<float>(IntOrZero((*it)[1].str())),
                static_cast<float>(IntOrZero((*it)[2].str())),
                static_cast<float>(IntOrZero((*it)[3].str()))
            });
        }

        std::string currentSurface = "SURFACE_DEFAULT";
        std::regex tokenPattern(R"(COL_TRI_INIT\s*\(\s*([A-Za-z0-9_]+)\s*,\s*\d+\s*\)|COL_TRI(?:_SPECIAL)?\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+))");
        for (auto it = std::sregex_iterator(text.begin(), text.end(), tokenPattern), end = std::sregex_iterator(); it != end; ++it) {
            if ((*it)[1].matched) {
                currentSurface = (*it)[1].str();
            } else if ((*it)[2].matched) {
                MeshTriangle tri;
                tri.a = baseVertex + IntOrZero((*it)[2].str());
                tri.b = baseVertex + IntOrZero((*it)[3].str());
                tri.c = baseVertex + IntOrZero((*it)[4].str());
                tri.surface = currentSurface;
                mesh.triangles.push_back(tri);
            }
        }
    }

    return mesh;
}

static std::vector<TextureAsset> ScanLevelTextures(const LevelInfo& level)
{
    std::vector<TextureAsset> textures;
    std::error_code ec;
    if (!fs::exists(level.path, ec)) return textures;

    for (fs::recursive_directory_iterator it(level.path, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        std::string ext = it->path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string filename = it->path().filename().string();
        if (ext == ".png" || ext == ".bmp" || ext == ".tga" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".rgba16" || ext == ".ci4" || ext == ".ci8" || filename.find("texture") != std::string::npos) {
            textures.push_back({ filename, it->path() });
        }
    }

    std::sort(textures.begin(), textures.end(), [](const TextureAsset& a, const TextureAsset& b) {
        return a.name < b.name;
    });
    return textures;
}

static bool CopyTextureIntoLevel(const LevelInfo& level, const fs::path& source, std::string& error)
{
    std::error_code ec;
    const fs::path targetDir = level.path / "textures";
    fs::create_directories(targetDir, ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    fs::copy_file(source, targetDir / source.filename(), fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

static bool ReplaceTextureAsset(const TextureAsset& texture, const fs::path& source, std::string& error)
{
    std::error_code ec;
    fs::copy_file(source, texture.path, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

static void ReleaseTextureCache(AppState& app)
{
    for (auto& item : app.textureCache) {
        if (item.second.srv != nullptr) {
            item.second.srv->Release();
            item.second.srv = nullptr;
        }
    }
    app.textureCache.clear();
    app.viewportBatches.clear();
    app.viewportBatchesDirty = true;
    app.textureTrianglesDrawn = 0;
    app.textureTrianglesMissing = 0;
}

static std::vector<unsigned char> ParseByteList(const std::string& text)
{
    std::vector<unsigned char> bytes;
    std::regex bytePattern(R"(0x([0-9a-fA-F]{1,2})|(?:^|[,\s])(\d{1,3})(?=[,\s]|$))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), bytePattern), end = std::sregex_iterator(); it != end; ++it) {
        int value = 0;
        if ((*it)[1].matched) {
            value = IntOrZero("0x" + (*it)[1].str());
        } else {
            value = IntOrZero((*it)[2].str());
        }
        bytes.push_back(static_cast<unsigned char>(std::clamp(value, 0, 255)));
    }
    return bytes;
}

static unsigned char Expand5(int value)
{
    value &= 31;
    return static_cast<unsigned char>((value << 3) | (value >> 2));
}

static std::vector<unsigned char> DecodeTextureBytes(const std::vector<unsigned char>& bytes, const std::string& format, const std::string& size, int width, int height)
{
    const int pixels = std::max(1, width * height);
    std::vector<unsigned char> rgba(static_cast<size_t>(pixels) * 4, 255);
    if (format.find("G_IM_FMT_IA") != std::string::npos) {
        if (size.find("16b") != std::string::npos) {
            for (int i = 0; i < pixels && i * 2 + 1 < static_cast<int>(bytes.size()); ++i) {
                rgba[i * 4 + 0] = bytes[i * 2 + 0];
                rgba[i * 4 + 1] = bytes[i * 2 + 0];
                rgba[i * 4 + 2] = bytes[i * 2 + 0];
                rgba[i * 4 + 3] = bytes[i * 2 + 1];
            }
        } else {
            for (int i = 0; i < pixels && i < static_cast<int>(bytes.size()); ++i) {
                const unsigned char intensity = static_cast<unsigned char>((bytes[i] >> 4) * 17);
                const unsigned char alpha = static_cast<unsigned char>((bytes[i] & 0x0f) * 17);
                rgba[i * 4 + 0] = intensity;
                rgba[i * 4 + 1] = intensity;
                rgba[i * 4 + 2] = intensity;
                rgba[i * 4 + 3] = alpha;
            }
        }
        return rgba;
    }

    for (int i = 0; i < pixels && i * 2 + 1 < static_cast<int>(bytes.size()); ++i) {
        const unsigned short packed = static_cast<unsigned short>((bytes[i * 2] << 8) | bytes[i * 2 + 1]);
        rgba[i * 4 + 0] = Expand5(packed >> 11);
        rgba[i * 4 + 1] = Expand5(packed >> 6);
        rgba[i * 4 + 2] = Expand5(packed >> 1);
        rgba[i * 4 + 3] = (packed & 1) ? 255 : 0;
    }
    return rgba;
}

static fs::path ResolveIncludedTexturePath(const Project& project, const fs::path& includePath)
{
    std::vector<fs::path> candidates = {
        project.root / includePath,
        project.root / "build" / "us_n64" / includePath,
        project.root / "build" / "us_pc" / includePath,
        project.root / "build" / "us" / includePath
    };
    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate, ec)) return candidate;
    }
    return {};
}

static fs::path FindTextureSource(const Project& project, const LevelInfo& level, const std::string& textureName)
{
    std::vector<fs::path> wrappers;
    std::error_code ec;
    const fs::path levelTexture = level.path / "texture.inc.c";
    if (fs::exists(levelTexture, ec)) wrappers.push_back(levelTexture);
    const fs::path binDir = project.root / "bin";
    if (fs::exists(binDir, ec)) {
        for (fs::directory_iterator it(binDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file() && it->path().extension() == ".c") wrappers.push_back(it->path());
        }
    }
    const fs::path actorsDir = project.root / "actors";
    if (fs::exists(actorsDir, ec)) {
        ec.clear();
        for (fs::recursive_directory_iterator it(actorsDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file() && it->path().filename() == "model.inc.c") wrappers.push_back(it->path());
        }
    }

    const std::string escaped = textureName;
    for (const fs::path& wrapper : wrappers) {
        const std::string text = StripCComments(Slurp(wrapper));
        if (text.find(textureName) == std::string::npos) continue;
        std::regex texturePattern("(?:ALIGNED8\\s+)?(?:static\\s+)?const\\s+Texture\\s+" + escaped + "\\s*\\[\\]\\s*=\\s*\\{\\s*#include\\s+\"([^\"]+)\"");
        std::smatch match;
        if (std::regex_search(text, match, texturePattern)) {
            return ResolveIncludedTexturePath(project, fs::path(match[1].str()));
        }
    }

    return {};
}

static std::string TextureCacheKey(const MeshTriangle& tri)
{
    return tri.texture + "|" + tri.textureFormat + "|" + tri.textureSize + "|" + std::to_string(tri.textureWidth) + "x" + std::to_string(tri.textureHeight);
}

static GpuTexture* GetGpuTexture(AppState& app, const MeshTriangle& tri)
{
    if (tri.texture.empty() || g_pd3dDevice == nullptr) return nullptr;
    if (app.selectedLevel < 0 || app.selectedLevel >= static_cast<int>(app.levels.size())) return nullptr;
    const std::string key = TextureCacheKey(tri);
    auto cached = app.textureCache.find(key);
    if (cached != app.textureCache.end()) return cached->second.srv != nullptr ? &cached->second : nullptr;

    GpuTexture texture;
    texture.name = tri.texture;
    texture.width = std::max(1, tri.textureWidth);
    texture.height = std::max(1, tri.textureHeight);
    texture.source = FindTextureSource(app.project, app.levels[app.selectedLevel], tri.texture);
    if (texture.source.empty()) {
        texture.error = "Texture include not found.";
        app.textureCache.emplace(key, texture);
        return nullptr;
    }

    const std::vector<unsigned char> bytes = ParseByteList(Slurp(texture.source));
    const std::vector<unsigned char> rgba = DecodeTextureBytes(bytes, tri.textureFormat, tri.textureSize, texture.width, texture.height);
    if (rgba.empty()) {
        texture.error = "Texture data could not be decoded.";
        app.textureCache.emplace(key, texture);
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(texture.width);
    desc.Height = static_cast<UINT>(texture.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = rgba.data();
    data.SysMemPitch = static_cast<UINT>(texture.width * 4);

    ID3D11Texture2D* nativeTexture = nullptr;
    HRESULT hr = g_pd3dDevice->CreateTexture2D(&desc, &data, &nativeTexture);
    if (SUCCEEDED(hr) && nativeTexture != nullptr) {
        hr = g_pd3dDevice->CreateShaderResourceView(nativeTexture, nullptr, &texture.srv);
        nativeTexture->Release();
    }
    if (FAILED(hr) || texture.srv == nullptr) {
        texture.error = "D3D texture upload failed.";
    }

    auto inserted = app.textureCache.emplace(key, texture);
    return inserted.first->second.srv != nullptr ? &inserted.first->second : nullptr;
}

static void ReleaseViewportTarget()
{
    if (g_viewportRenderer.color) { g_viewportRenderer.color->Release(); g_viewportRenderer.color = nullptr; }
    if (g_viewportRenderer.rtv) { g_viewportRenderer.rtv->Release(); g_viewportRenderer.rtv = nullptr; }
    if (g_viewportRenderer.srv) { g_viewportRenderer.srv->Release(); g_viewportRenderer.srv = nullptr; }
    if (g_viewportRenderer.depth) { g_viewportRenderer.depth->Release(); g_viewportRenderer.depth = nullptr; }
    if (g_viewportRenderer.dsv) { g_viewportRenderer.dsv->Release(); g_viewportRenderer.dsv = nullptr; }
    g_viewportRenderer.width = 0;
    g_viewportRenderer.height = 0;
}

static void ReleaseViewportRenderer()
{
    ReleaseViewportTarget();
    if (g_viewportRenderer.vs) { g_viewportRenderer.vs->Release(); g_viewportRenderer.vs = nullptr; }
    if (g_viewportRenderer.ps) { g_viewportRenderer.ps->Release(); g_viewportRenderer.ps = nullptr; }
    if (g_viewportRenderer.inputLayout) { g_viewportRenderer.inputLayout->Release(); g_viewportRenderer.inputLayout = nullptr; }
    if (g_viewportRenderer.vertexBuffer) { g_viewportRenderer.vertexBuffer->Release(); g_viewportRenderer.vertexBuffer = nullptr; }
    if (g_viewportRenderer.cameraBuffer) { g_viewportRenderer.cameraBuffer->Release(); g_viewportRenderer.cameraBuffer = nullptr; }
    if (g_viewportRenderer.drawBuffer) { g_viewportRenderer.drawBuffer->Release(); g_viewportRenderer.drawBuffer = nullptr; }
    if (g_viewportRenderer.blendState) { g_viewportRenderer.blendState->Release(); g_viewportRenderer.blendState = nullptr; }
    if (g_viewportRenderer.depthWriteState) { g_viewportRenderer.depthWriteState->Release(); g_viewportRenderer.depthWriteState = nullptr; }
    if (g_viewportRenderer.depthReadState) { g_viewportRenderer.depthReadState->Release(); g_viewportRenderer.depthReadState = nullptr; }
    if (g_viewportRenderer.solidRasterizer) { g_viewportRenderer.solidRasterizer->Release(); g_viewportRenderer.solidRasterizer = nullptr; }
    if (g_viewportRenderer.wireRasterizer) { g_viewportRenderer.wireRasterizer->Release(); g_viewportRenderer.wireRasterizer = nullptr; }
    if (g_viewportRenderer.whiteSrv) { g_viewportRenderer.whiteSrv->Release(); g_viewportRenderer.whiteSrv = nullptr; }
    g_viewportRenderer.vertexCapacity = 0;
}

static bool CompileShader(const char* source, const char* entry, const char* target, ID3DBlob** blob)
{
    ID3DBlob* errors = nullptr;
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    HRESULT hr = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr, entry, target, flags, 0, blob, &errors);
    if (errors != nullptr) {
        errors->Release();
    }
    return SUCCEEDED(hr) && *blob != nullptr;
}

static bool EnsureViewportRenderer()
{
    if (g_pd3dDevice == nullptr) return false;
    if (g_viewportRenderer.vs != nullptr && g_viewportRenderer.ps != nullptr) return true;

    const char* shader = R"(
cbuffer CameraBuffer : register(b0) {
    float4 cameraPos;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 viewportData;
};
cbuffer DrawBuffer : register(b1) {
    float useTexture;
    float alphaTest;
    float alphaScale;
    float drawPad;
};
Texture2D tex0 : register(t0);
SamplerState sampler0 : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VSOut VSMain(VSIn input) {
    float3 rel = input.pos - cameraPos.xyz;
    float viewX = dot(rel, cameraRight.xyz);
    float viewY = dot(rel, cameraUp.xyz);
    float depth = dot(rel, cameraForward.xyz);
    float nearZ = viewportData.z;
    float farZ = viewportData.w;
    float aspect = max(viewportData.x / max(viewportData.y, 1.0), 0.01);
    float proj = 1.0 / tan(55.0 * 0.5 * 0.017453292519943295);
    float z = depth * farZ / (farZ - nearZ) - nearZ * farZ / (farZ - nearZ);
    VSOut output;
    output.pos = float4(viewX * proj / aspect, viewY * proj, z, depth);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET {
    float4 tex = useTexture > 0.5 ? tex0.Sample(sampler0, input.uv) : float4(1.0, 1.0, 1.0, 1.0);
    float4 color = tex * input.color;
    color.a *= alphaScale;
    if (alphaTest > 0.5 && color.a < 0.35) discard;
    return color;
}
)";

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    if (!CompileShader(shader, "VSMain", "vs_4_0", &vsBlob) || !CompileShader(shader, "PSMain", "ps_4_0", &psBlob)) {
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        return false;
    }

    HRESULT hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_viewportRenderer.vs);
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_viewportRenderer.ps);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_viewportRenderer.inputLayout);
    vsBlob->Release();
    psBlob->Release();
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(ViewportCameraConstants);
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = g_pd3dDevice->CreateBuffer(&bufferDesc, nullptr, &g_viewportRenderer.cameraBuffer);
    bufferDesc.ByteWidth = sizeof(ViewportDrawConstants);
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateBuffer(&bufferDesc, nullptr, &g_viewportRenderer.drawBuffer);

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateBlendState(&blendDesc, &g_viewportRenderer.blendState);

    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    depthDesc.DepthEnable = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateDepthStencilState(&depthDesc, &g_viewportRenderer.depthWriteState);
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateDepthStencilState(&depthDesc, &g_viewportRenderer.depthReadState);

    D3D11_RASTERIZER_DESC rasterDesc{};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateRasterizerState(&rasterDesc, &g_viewportRenderer.solidRasterizer);
    rasterDesc.FillMode = D3D11_FILL_WIREFRAME;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateRasterizerState(&rasterDesc, &g_viewportRenderer.wireRasterizer);

    const unsigned char white[] = { 255, 255, 255, 255 };
    D3D11_TEXTURE2D_DESC texDesc{};
    texDesc.Width = 1;
    texDesc.Height = 1;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA texData{};
    texData.pSysMem = white;
    texData.SysMemPitch = 4;
    ID3D11Texture2D* whiteTexture = nullptr;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateTexture2D(&texDesc, &texData, &whiteTexture);
    if (SUCCEEDED(hr) && whiteTexture != nullptr) {
        hr = g_pd3dDevice->CreateShaderResourceView(whiteTexture, nullptr, &g_viewportRenderer.whiteSrv);
        whiteTexture->Release();
    }
    return SUCCEEDED(hr);
}

static bool EnsureViewportTarget(int width, int height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    if (!EnsureViewportRenderer()) return false;
    if (g_viewportRenderer.srv != nullptr && g_viewportRenderer.width == width && g_viewportRenderer.height == height) return true;
    ReleaseViewportTarget();

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = static_cast<UINT>(width);
    colorDesc.Height = static_cast<UINT>(height);
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    HRESULT hr = g_pd3dDevice->CreateTexture2D(&colorDesc, nullptr, &g_viewportRenderer.color);
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateRenderTargetView(g_viewportRenderer.color, nullptr, &g_viewportRenderer.rtv);
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateShaderResourceView(g_viewportRenderer.color, nullptr, &g_viewportRenderer.srv);

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = static_cast<UINT>(width);
    depthDesc.Height = static_cast<UINT>(height);
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &g_viewportRenderer.depth);
    if (SUCCEEDED(hr)) hr = g_pd3dDevice->CreateDepthStencilView(g_viewportRenderer.depth, nullptr, &g_viewportRenderer.dsv);
    if (FAILED(hr)) {
        ReleaseViewportTarget();
        return false;
    }
    g_viewportRenderer.width = width;
    g_viewportRenderer.height = height;
    return true;
}

static float WrapUv(float value)
{
    value = value - floorf(value);
    if (value < 0.0f) value += 1.0f;
    return value;
}

static float MirrorUv(float value)
{
    value = fmodf(value, 2.0f);
    if (value < 0.0f) value += 2.0f;
    return value <= 1.0f ? value : 2.0f - value;
}

static ImVec2 NormalizeUv(const ImVec2& uv, const GpuTexture& texture, bool clamp)
{
    float u = uv.x / static_cast<float>(std::max(1, texture.width));
    float v = uv.y / static_cast<float>(std::max(1, texture.height));
    if (clamp) {
        const float maxU = 1.0f - (0.5f / static_cast<float>(std::max(1, texture.width)));
        const float maxV = 1.0f - (0.5f / static_cast<float>(std::max(1, texture.height)));
        u = std::clamp(u, 0.0f, maxU);
        v = std::clamp(v, 0.0f, maxV);
    }
    return ImVec2(u, v);
}

static ImVec2 NormalizeUv(const ImVec2& uv, const GpuTexture& texture, const MeshTriangle& tri)
{
    const int width = std::max(1, texture.width);
    const int height = std::max(1, texture.height);
    float u = (uv.x * tri.textureScaleS) / static_cast<float>(width);
    float v = (uv.y * tri.textureScaleT) / static_cast<float>(height);

    if (tri.textureMirrorS) u = MirrorUv(u);
    if (tri.textureMirrorT) v = MirrorUv(v);

    if (tri.textureClampS) {
        const float maxU = 1.0f - (0.5f / static_cast<float>(width));
        u = std::clamp(u, 0.0f, maxU);
    }
    if (tri.textureClampT) {
        const float maxV = 1.0f - (0.5f / static_cast<float>(height));
        v = std::clamp(v, 0.0f, maxV);
    }
    return ImVec2(u, v);
}

static ImVec2 ProjectPoint(const Vec3& point, const AppState& app, const ImVec2& origin, const ImVec2& size, float& depth);
static bool ProjectPointVisible(const Vec3& point, const AppState& app, const ImVec2& origin, const ImVec2& size, ImVec2& out, float& depth);

static void DrawCallback_SetWrapSampler(const ImDrawList*, const ImDrawCmd*)
{
    if (g_pd3dDeviceContext != nullptr && g_wrapSamplerState != nullptr) {
        g_pd3dDeviceContext->PSSetSamplers(0, 1, &g_wrapSamplerState);
    }
}

static void AddTexturedTriangleUv(ImDrawList* draw, const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& uva, const ImVec2& uvb, const ImVec2& uvc, GpuTexture& texture, bool clamp, ImU32 tint)
{
    draw->PushTextureID(reinterpret_cast<ImTextureID>(texture.srv));
    draw->PrimReserve(3, 3);
    const ImDrawIdx idx = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
    draw->PrimWriteIdx(idx);
    draw->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 1));
    draw->PrimWriteIdx(static_cast<ImDrawIdx>(idx + 2));
    draw->PrimWriteVtx(a, NormalizeUv(uva, texture, clamp), tint);
    draw->PrimWriteVtx(b, NormalizeUv(uvb, texture, clamp), tint);
    draw->PrimWriteVtx(c, NormalizeUv(uvc, texture, clamp), tint);
    draw->PopTextureID();
}

static Vec3 LerpVec3(const Vec3& a, const Vec3& b, const Vec3& c, float u, float v)
{
    const float w = 1.0f - u - v;
    return {
        a.x * w + b.x * u + c.x * v,
        a.y * w + b.y * u + c.y * v,
        a.z * w + b.z * u + c.z * v
    };
}

static ImVec2 LerpUv(const ImVec2& a, const ImVec2& b, const ImVec2& c, float u, float v)
{
    const float w = 1.0f - u - v;
    return ImVec2(a.x * w + b.x * u + c.x * v, a.y * w + b.y * u + c.y * v);
}

static float Distance3(const Vec3& a, const Vec3& b)
{
    const float x = a.x - b.x;
    const float y = a.y - b.y;
    const float z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}

static int DrawTexturedTriangleSubdivided(ImDrawList* draw, const AppState& app, const ImVec2& origin, const ImVec2& size, const MeshTriangle& tri, GpuTexture& texture)
{
    if (tri.a >= static_cast<int>(app.mesh.vertices.size()) || tri.b >= static_cast<int>(app.mesh.vertices.size()) || tri.c >= static_cast<int>(app.mesh.vertices.size())) return 0;
    const Vec3& a = app.mesh.vertices[tri.a];
    const Vec3& b = app.mesh.vertices[tri.b];
    const Vec3& c = app.mesh.vertices[tri.c];
    const float longest = std::max(Distance3(a, b), std::max(Distance3(b, c), Distance3(c, a)));
    const int steps = std::clamp(static_cast<int>(longest / 360.0f) + 1, 1, 7);
    int drawn = 0;

    auto emit = [&](float u0, float v0, float u1, float v1, float u2, float v2) {
        const Vec3 p0 = LerpVec3(a, b, c, u0, v0);
        const Vec3 p1 = LerpVec3(a, b, c, u1, v1);
        const Vec3 p2 = LerpVec3(a, b, c, u2, v2);
        float d0, d1, d2;
        const ImVec2 s0 = ProjectPoint(p0, app, origin, size, d0);
        const ImVec2 s1 = ProjectPoint(p1, app, origin, size, d1);
        const ImVec2 s2 = ProjectPoint(p2, app, origin, size, d2);
        if (std::max(d0, std::max(d1, d2)) <= 1.0f) {
            return;
        }
        AddTexturedTriangleUv(draw, s0, s1, s2,
            LerpUv(tri.uva, tri.uvb, tri.uvc, u0, v0),
            LerpUv(tri.uva, tri.uvb, tri.uvc, u1, v1),
            LerpUv(tri.uva, tri.uvb, tri.uvc, u2, v2),
            texture, tri.textureClampS || tri.textureClampT, IM_COL32(255, 255, 255, 255));
        ++drawn;
    };

    const float inv = 1.0f / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        for (int j = 0; j < steps - i; ++j) {
            const float u0 = i * inv;
            const float v0 = j * inv;
            const float u1 = (i + 1) * inv;
            const float v1 = j * inv;
            const float u2 = i * inv;
            const float v2 = (j + 1) * inv;
            emit(u0, v0, u1, v1, u2, v2);
            if (i + j < steps - 1) {
                emit(u1, v1, (i + 1) * inv, (j + 1) * inv, u2, v2);
            }
        }
    }
    return drawn;
}

static std::vector<LevelObject> ParseLevelObjects(const LevelInfo& level)
{
    std::vector<LevelObject> objects;
    int index = 1;

    auto parseObjects = [&](const std::string& text, bool fromScript) {
        const std::string script = StripCComments(text);

        for (const MacroCall& call : FindObjectMacros(script)) {
            const std::vector<std::string> args = SplitArgs(call.args);
            if (args.empty()) continue;
            LevelObject object;
            object.name = (fromScript ? "Script Object " : "BoxStudio Object ") + std::to_string(index++);
            object.fromScript = fromScript;
            if (!args.empty()) object.model = args[0];
            if (args.size() >= 4) {
                object.position = { NumberOrZero(args[1]), NumberOrZero(args[2]), NumberOrZero(args[3]) };
            }
            if (args.size() >= 7) {
                object.rotation = { NumberOrZero(args[4]), NumberOrZero(args[5]), NumberOrZero(args[6]) };
            }
            if (args.size() >= 9) {
                object.behParam = args[7];
                object.behavior = args[8];
            } else if (args.size() >= 8) {
                if (args[7].find("bhv") == 0) {
                    object.behParam = "0x00000000";
                    object.behavior = args[7];
                } else {
                    object.behParam = args[7];
                    object.behavior = "bhvStaticObject";
                }
            }
            objects.push_back(object);
        }
    };

    std::string raw = Slurp(level.scriptPath);
    const std::string beginText = "        /* BOXSTUDIO_OBJECTS_BEGIN */";
    const std::string endText = "        /* BOXSTUDIO_OBJECTS_END */";
    size_t begin = raw.find(beginText);
    if (begin != std::string::npos) {
        size_t end = raw.find(endText, begin);
        if (end != std::string::npos) {
            size_t endLine = raw.find('\n', end);
            const size_t eraseEnd = endLine == std::string::npos ? raw.size() : endLine + 1;
            parseObjects(raw.substr(begin, eraseEnd - begin), false);
            raw.erase(begin, eraseEnd - begin);
        }
    }
    parseObjects(raw, true);

    if (objects.empty()) {
        objects.push_back({ "Mario start", "MODEL_MARIO", "0x00000000", "bhvMario", { 0.0f, 120.0f, 0.0f }, {}, 1.0f, false });
        objects.push_back({ "Goomba", "MODEL_GOOMBA", "0x00000000", "bhvGoomba", { 320.0f, 0.0f, -180.0f }, {}, 1.0f, false });
        objects.push_back({ "Coin line", "MODEL_YELLOW_COIN", "0x00000000", "bhvYellowCoin", { -260.0f, 80.0f, 220.0f }, {}, 1.0f, false });
    }

    return objects;
}

static void LoadLevelForEditing(AppState& app, int levelIndex)
{
    if (levelIndex < 0 || levelIndex >= static_cast<int>(app.levels.size())) return;
    ReleaseTextureCache(app);
    app.selectedLevel = levelIndex;
    app.objects = ParseLevelObjects(app.levels[levelIndex]);
    app.mesh = LoadRenderMesh(app.levels[levelIndex]);
    if (app.mesh.triangles.empty()) app.mesh = LoadCollisionMesh(app.levels[levelIndex]);
    AppendWaterBoxes(app.levels[levelIndex], app.mesh);
    app.textures = ScanLevelTextures(app.levels[levelIndex]);
    app.selectedObject = app.objects.empty() ? -1 : 0;
    app.selectedTexture = app.textures.empty() ? -1 : 0;
    app.levelDirty = false;
    app.pendingLevelMenu = false;
    app.cameraYaw = 0.72f;
    app.cameraPitch = 0.62f;
    app.cameraDistance = 1250.0f;
    app.cameraTarget = { 0.0f, 0.0f, 0.0f };
    app.cameraPosition = { 900.0f, 700.0f, 900.0f };
    app.cameraPan = ImVec2(0.0f, 0.0f);
    app.editorMode = EditorMode::EditingLevel;
    app.status = "Editing " + app.levels[levelIndex].name + ": loaded " + std::to_string(app.mesh.triangles.size()) + (app.mesh.renderMesh ? " render triangles." : " collision triangles.");
}

static void StartLevelLoad(AppState& app, int levelIndex)
{
    if (levelIndex < 0 || levelIndex >= static_cast<int>(app.levels.size())) return;
    if (app.levelLoadJob.worker.joinable()) app.levelLoadJob.worker.join();

    app.levelLoadJob.levelIndex = levelIndex;
    app.levelLoadJob.levelName = app.levels[levelIndex].name;
    app.levelLoadJob.running.store(true);
    app.levelLoadJob.finished.store(false);
    app.levelLoadJob.success.store(false);
    {
        std::lock_guard<std::mutex> lock(app.levelLoadJob.mutex);
        app.levelLoadJob.objects.clear();
        app.levelLoadJob.mesh = {};
        app.levelLoadJob.textures.clear();
        app.levelLoadJob.error.clear();
    }
    app.status = "Loading " + app.levelLoadJob.levelName + "...";

    const LevelInfo level = app.levels[levelIndex];
    app.levelLoadJob.worker = std::thread([job = &app.levelLoadJob, level]() {
        std::vector<LevelObject> objects;
        LevelMesh mesh;
        std::vector<TextureAsset> textures;
        std::string error;
        bool ok = false;
        try {
            objects = ParseLevelObjects(level);
            mesh = LoadRenderMesh(level);
            if (mesh.triangles.empty()) mesh = LoadCollisionMesh(level);
            AppendWaterBoxes(level, mesh);
            textures = ScanLevelTextures(level);
            ok = true;
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "Unknown level load error.";
        }

        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->objects = std::move(objects);
            job->mesh = std::move(mesh);
            job->textures = std::move(textures);
            job->error = error;
        }
        job->success.store(ok);
        job->running.store(false);
        job->finished.store(true);
    });
}

static void PollLevelLoad(AppState& app)
{
    if (!app.levelLoadJob.finished.load()) return;
    if (app.levelLoadJob.worker.joinable()) app.levelLoadJob.worker.join();
    app.levelLoadJob.finished.store(false);

    if (!app.levelLoadJob.success.load()) {
        std::lock_guard<std::mutex> lock(app.levelLoadJob.mutex);
        app.status = app.levelLoadJob.error.empty() ? "Level load failed." : app.levelLoadJob.error;
        return;
    }

    ReleaseTextureCache(app);
    {
        std::lock_guard<std::mutex> lock(app.levelLoadJob.mutex);
        app.selectedLevel = app.levelLoadJob.levelIndex;
        app.objects = std::move(app.levelLoadJob.objects);
        app.mesh = std::move(app.levelLoadJob.mesh);
        app.textures = std::move(app.levelLoadJob.textures);
    }
    app.viewportBatches.clear();
    app.viewportBatchesDirty = true;
    app.cachedViewMode = app.viewMode;
    app.selectedObject = app.objects.empty() ? -1 : 0;
    app.selectedTexture = app.textures.empty() ? -1 : 0;
    app.levelDirty = false;
    app.pendingLevelMenu = false;
    app.cameraYaw = 0.72f;
    app.cameraPitch = 0.62f;
    app.cameraDistance = 1250.0f;
    app.cameraTarget = { 0.0f, 0.0f, 0.0f };
    app.cameraPosition = { 900.0f, 700.0f, 900.0f };
    app.cameraPan = ImVec2(0.0f, 0.0f);
    app.editorMode = EditorMode::EditingLevel;
    app.status = "Editing " + app.levels[app.selectedLevel].name + ": loaded " + std::to_string(app.mesh.triangles.size()) + (app.mesh.renderMesh ? " render triangles." : " collision triangles.");
}

static bool RegisterBoxStudioLevel(Project& project, const std::string& levelName, std::string& error)
{
    const fs::path definesPath = project.root / "levels" / "level_defines.h";
    std::string defines = Slurp(definesPath);
    if (defines.empty()) {
        error = "Could not read levels/level_defines.h for level registration.";
        return false;
    }
    const std::string levelEnum = ToLevelEnumName(levelName);
    if (defines.find(levelEnum) != std::string::npos) return true;

    std::ostringstream line;
    line << "DEFINE_LEVEL(\"BOXSTUDIO\",      " << levelEnum
         << ",     COURSE_NONE,     " << levelName
         << ",       generic,  20000, 0x08, 0x08, 0x08, _,         _)\n";

    size_t insert = defines.find("STUB_LEVEL(  \"\",               LEVEL_UNKNOWN_37");
    if (insert == std::string::npos) {
        if (!defines.empty() && defines.back() != '\n') defines += "\n";
        insert = defines.size();
    }
    defines.insert(insert, line.str());

    std::ofstream out(definesPath, std::ios::binary);
    if (!out) {
        error = "Could not write levels/level_defines.h.";
        return false;
    }
    out << defines;
    return true;
}

static bool CreateBoxStudioLevel(Project& project, const std::string& rawName, LevelInfo& outLevel, std::string& error)
{
    const std::string levelName = SanitizeLevelName(rawName);
    const std::string levelEnum = ToLevelEnumName(levelName);
    fs::path levelPath = project.root / "levels" / levelName;
    std::error_code ec;
    if (fs::exists(levelPath, ec)) {
        error = "Level folder already exists.";
        return false;
    }

    fs::create_directories(levelPath / "areas" / "1", ec);
    if (ec) {
        error = ec.message();
        return false;
    }

    std::ofstream script(levelPath / "script.c", std::ios::binary);
    if (!script) {
        error = "Could not create level script.";
        return false;
    }

    script << "#include <ultra64.h>\n";
    script << "#include \"sm64.h\"\n";
    script << "#include \"behavior_data.h\"\n";
    script << "#include \"model_ids.h\"\n";
    script << "#include \"seq_ids.h\"\n";
    script << "#include \"segment_symbols.h\"\n";
    script << "#include \"level_commands.h\"\n\n";
    script << "#include \"game/level_update.h\"\n\n";
    script << "#include \"levels/scripts.h\"\n\n";
    script << "#include \"actors/common1.h\"\n\n";
    script << "#include \"make_const_nonconst.h\"\n";
    script << "#include \"levels/" << levelName << "/header.h\"\n\n";
    script << "const LevelScript level_" << levelName << "_entry[] = {\n";
    script << "    INIT_LEVEL(),\n";
    script << "    JUMP_LINK(script_func_global_1),\n";
    script << "    LOAD_MIO0(        0x07, _" << levelName << "_segment_7SegmentRomStart, _" << levelName << "_segment_7SegmentRomEnd),\n";
    script << "    AREA(1, " << levelName << "_geo_000000),\n";
    script << "        WARP_NODE(0x0A, " << levelEnum << ", 0x01, 0x0A, WARP_NO_CHECKPOINT),\n";
    script << "        MARIO_POS(0x01, 0, 0, 120, 0),\n";
    script << "        OBJECT(MODEL_GOOMBA, 300, 0, -180, 0, 0, 0, 0x00000000, bhvGoomba),\n";
    script << "        OBJECT(MODEL_YELLOW_COIN, -240, 80, 220, 0, 0, 0, 0x00000000, bhvYellowCoin),\n";
    script << "    END_AREA(),\n";
    script << "    MARIO_POS(0x01, 0, 0, 120, 0),\n";
    script << "    CALL(0, lvl_init_or_update),\n";
    script << "    CALL_LOOP(1, lvl_init_or_update),\n";
    script << "    CLEAR_LEVEL(),\n";
    script << "    SLEEP_BEFORE_EXIT(1),\n";
    script << "    EXIT(),\n";
    script << "};\n";

    std::ofstream header(levelPath / "header.h", std::ios::binary);
    header << "#pragma once\n\n#include \"types.h\"\n\nextern const GeoLayout " << levelName << "_geo_000000[];\nextern const LevelScript level_" << levelName << "_entry[];\n";

    std::ofstream geo(levelPath / "areas" / "1" / "geo.inc.c", std::ios::binary);
    geo << "#include \"src/game/envfx_snow.h\"\n\nconst GeoLayout " << levelName << "_geo_000000[] = {\n";
    geo << "    GEO_NODE_START(),\n    GEO_OPEN_NODE(),\n        GEO_RENDER_RANGE(-10000, 10000),\n        GEO_OPEN_NODE(),\n        GEO_CLOSE_NODE(),\n    GEO_CLOSE_NODE(),\n    GEO_END(),\n};\n";

    std::ofstream collision(levelPath / "areas" / "1" / "collision.inc.c", std::ios::binary);
    collision << "const Collision " << levelName << "_area_1_collision[] = {\n    COL_INIT(),\n    COL_VERTEX_INIT(4),\n";
    collision << "    COL_VERTEX(-600, 0, -600),\n    COL_VERTEX(600, 0, -600),\n    COL_VERTEX(600, 0, 600),\n    COL_VERTEX(-600, 0, 600),\n";
    collision << "    COL_TRI_INIT(SURFACE_DEFAULT, 2),\n    COL_TRI(0, 1, 2),\n    COL_TRI(0, 2, 3),\n    COL_TRI_STOP(),\n    COL_END(),\n};\n";

    if (!RegisterBoxStudioLevel(project, levelName, error)) return false;

    outLevel.name = levelName;
    outLevel.path = levelPath;
    outLevel.scriptPath = levelPath / "script.c";
    return true;
}

static LevelObject MakePresetObject(const char* name, const char* model, const char* behavior, Vec3 position, float scale, const char* behParam = "0x00000000")
{
    LevelObject object;
    object.name = name;
    object.model = model;
    object.behParam = behParam;
    object.behavior = behavior;
    object.position = position;
    object.scale = scale;
    return object;
}

static void AddPresetObject(AppState& app, const char* name, const char* model, const char* behavior, float scale = 1.0f, const char* behParam = "0x00000000")
{
    const float offset = static_cast<float>(app.objects.size() % 7) * 80.0f;
    LevelObject object = MakePresetObject(name, model, behavior, { offset - 240.0f, 0.0f, offset * 0.5f }, scale, behParam);
    app.objects.push_back(object);
    app.selectedObject = static_cast<int>(app.objects.size()) - 1;
    app.levelDirty = true;
    app.status = std::string("Added ") + name + ".";
}

static void ApplyBoxStyle(float scale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.078f, 0.086f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.105f, 0.109f, 0.120f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.153f, 0.165f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.190f, 0.203f, 0.220f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.180f, 0.285f, 0.360f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.245f, 0.390f, 0.485f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.125f, 0.210f, 0.285f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.175f, 0.250f, 0.230f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.235f, 0.325f, 0.305f, 1.0f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.130f, 0.215f, 0.205f, 1.0f);
    c[ImGuiCol_Tab] = ImVec4(0.130f, 0.136f, 0.150f, 1.0f);
    c[ImGuiCol_TabSelected] = ImVec4(0.210f, 0.300f, 0.285f, 1.0f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.075f, 0.078f, 0.086f, 1.0f);
}

static void DrawTopBar(AppState& app)
{
    ImGui::BeginChild("TopBar", ImVec2(0.0f, 44.0f), false, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("BoxStudio");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", app.projectLoaded ? app.project.name.c_str() : "HackerSM64 workspace editor");

    const float w = ImGui::GetWindowWidth();
    ImGui::SetCursorPosX(w - 144.0f);
    HWND hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandle);
    if (ImGui::Button("_", ImVec2(38.0f, 24.0f))) ::ShowWindow(hwnd, SW_MINIMIZE);
    ImGui::SameLine();
    if (ImGui::Button("[]", ImVec2(38.0f, 24.0f))) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(WINDOWPLACEMENT);
        ::GetWindowPlacement(hwnd, &wp);
        ::ShowWindow(hwnd, wp.showCmd == SW_MAXIMIZE ? SW_RESTORE : SW_MAXIMIZE);
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.12f, 0.10f, 1.0f));
    if (ImGui::Button("X", ImVec2(38.0f, 24.0f))) {
        if (app.levelDirty) {
            app.pendingExit = true;
            ImGui::OpenPopup("Unsaved Changes");
        } else {
            ::PostQuitMessage(0);
        }
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

static void RenderHome(AppState& app)
{
    ImGui::SetCursorPosY(96.0f);
    ImGui::SetCursorPosX(80.0f);
    ImGui::BeginGroup();
    ImGui::Text("Create and edit N64 decomp projects without fighting the filesystem.");
    ImGui::Spacing();
    if (ImGui::Button("New HackerSM64 Project", ImVec2(250.0f, 36.0f))) {
        app.screen = Screen::CreateProject;
        app.status = "Choose a project name, destination, and built HackerSM64 base.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Existing Project", ImVec2(220.0f, 36.0f))) {
        app.status = "Paste a BoxStudio project folder or manifest path below.";
    }
    ImGui::Spacing();
    ImGui::SetNextItemWidth(620.0f);
    ImGui::InputText("Project or manifest path", app.openProjectPath, IM_ARRAYSIZE(app.openProjectPath));
    ImGui::SameLine();
    if (ImGui::Button("Select", ImVec2(84.0f, 0.0f))) {
        fs::path picked;
        if (PickFolder(picked)) strncpy_s(app.openProjectPath, ToUtf8(picked).c_str(), _TRUNCATE);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open", ImVec2(84.0f, 0.0f))) {
        std::string error;
        if (LoadProject(app.openProjectPath, app.project, error)) {
            app.levels = DiscoverLevels(app.project.root);
            app.selectedLevel = -1;
            app.selectedObject = -1;
            app.objects.clear();
            app.projectLoaded = true;
            app.screen = Screen::Editor;
            app.editorMode = EditorMode::LevelMenu;
            app.status = "Project opened. Choose a level to edit or create a new one.";
        } else {
            app.status = error;
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Status: %s", app.status.c_str());
    ImGui::EndGroup();
}

static void StartProjectCopy(AppState& app, const Project& project)
{
    if (app.copyJob.worker.joinable()) app.copyJob.worker.join();
    app.copyJob.project = project;
    app.copyJob.copied.store(0);
    app.copyJob.running.store(true);
    app.copyJob.finished.store(false);
    app.copyJob.success.store(false);
    {
        std::lock_guard<std::mutex> lock(app.copyJob.mutex);
        app.copyJob.error.clear();
    }
    app.status = "Copying HackerSM64 into the project folder...";

    app.copyJob.worker = std::thread([job = &app.copyJob]() {
        std::string error;
        bool ok = false;
        if (fs::exists(job->project.root)) {
            error = "Project folder already exists. Choose a different name or root.";
        } else if (!CopyWorkspace(job->project.sourcePath, job->project.root, error, &job->copied)) {
            error = "Copy failed: " + error;
        } else if (!WriteManifest(job->project, error)) {
            error = "Manifest failed: " + error;
        } else {
            ok = true;
        }

        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->error = error;
        }
        job->success.store(ok);
        job->running.store(false);
        job->finished.store(true);
    });
}

static void PollProjectCopy(AppState& app)
{
    if (!app.copyJob.finished.load()) return;
    if (app.copyJob.worker.joinable()) app.copyJob.worker.join();
    app.copyJob.finished.store(false);

    if (app.copyJob.success.load()) {
        app.project = app.copyJob.project;
        app.levels = DiscoverLevels(app.project.root);
        app.selectedLevel = -1;
        app.selectedObject = -1;
        app.objects.clear();
        app.projectLoaded = true;
        app.screen = Screen::Editor;
        app.editorMode = EditorMode::LevelMenu;
        app.status = "Copied " + std::to_string(app.copyJob.copied.load()) + " files. Choose a level to edit or create a new one.";
    } else {
        std::lock_guard<std::mutex> lock(app.copyJob.mutex);
        app.status = app.copyJob.error.empty() ? "Project creation failed." : app.copyJob.error;
    }
}

static void RenderCreateProject(AppState& app)
{
    PollProjectCopy(app);

    ImGui::SetCursorPos(ImVec2(62.0f, 82.0f));
    ImGui::BeginChild("CreateProjectPanel", ImVec2(760.0f, 440.0f), true);
    ImGui::Text("New HackerSM64 Project");
    ImGui::Separator();
    ImGui::InputText("Project name", app.projectName, IM_ARRAYSIZE(app.projectName));
    ImGui::SetNextItemWidth(590.0f);
    ImGui::InputText("Projects root", app.projectsRoot, IM_ARRAYSIZE(app.projectsRoot));
    ImGui::SameLine();
    if (ImGui::Button("Select##ProjectsRoot", ImVec2(78.0f, 0.0f))) {
        fs::path picked;
        if (PickFolder(picked)) strncpy_s(app.projectsRoot, ToUtf8(picked).c_str(), _TRUNCATE);
    }
    ImGui::SetNextItemWidth(590.0f);
    ImGui::InputText("Built HackerSM64 folder", app.hackerSm64Path, IM_ARRAYSIZE(app.hackerSm64Path));
    ImGui::SameLine();
    if (ImGui::Button("Select##HackerSM64", ImVec2(78.0f, 0.0f))) {
        fs::path picked;
        if (PickFolder(picked)) strncpy_s(app.hackerSm64Path, ToUtf8(picked).c_str(), _TRUNCATE);
    }

    app.baseLooksValid = ValidateHackerSm64(app.hackerSm64Path, app.validation, app.baseLooksBuilt);
    ImGui::TextColored(app.baseLooksValid ? ImVec4(0.45f, 0.90f, 0.62f, 1.0f) : ImVec4(0.95f, 0.43f, 0.35f, 1.0f),
        "%s", app.validation.c_str());
    if (app.baseLooksValid && !app.baseLooksBuilt) {
        ImGui::TextDisabled("BoxStudio can still create the project, but built ROM test workflows will need a build folder later.");
    }

    const fs::path target = fs::path(app.projectsRoot) / SanitizeFolderName(app.projectName);
    ImGui::TextDisabled("Project target: %s", ToUtf8(target).c_str());
    ImGui::Spacing();

    ImGui::BeginDisabled(app.copyJob.running.load());
    if (ImGui::Button("Back", ImVec2(110.0f, 32.0f))) app.screen = Screen::Home;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(app.copyJob.running.load() || !app.baseLooksValid || strlen(app.projectName) == 0 || strlen(app.projectsRoot) == 0);
    if (ImGui::Button("Create And Open", ImVec2(180.0f, 32.0f))) {
        Project project;
        project.name = SanitizeFolderName(app.projectName);
        project.root = target;
        project.sourcePath = fs::path(app.hackerSm64Path);
        project.manifestPath = project.root / ".boxstudio" / "boxstudio.project.json";
        StartProjectCopy(app, project);
    }
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (app.copyJob.running.load()) {
        const int copied = app.copyJob.copied.load();
        const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 5.0f);
        ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "Copying HackerSM64 workspace...");
        ImGui::Text("Files copied: %d", copied);
        ImGui::TextDisabled("BoxStudio is responsive while this runs. Large built decomp folders can take a little while.");
    }
    ImGui::TextWrapped("Workspace model: BoxStudio copies the HackerSM64 tree into the project folder and stores editor metadata in .boxstudio. The copied tree stays buildable as HackerSM64.");
    ImGui::TextDisabled("Status: %s", app.status.c_str());
    ImGui::EndChild();
}

static ImVec2 ProjectPoint(const Vec3& point, const AppState& app, const ImVec2& origin, const ImVec2& size, float& depth)
{
    const float cy = cosf(app.cameraYaw);
    const float sy = sinf(app.cameraYaw);
    const float cp = cosf(app.cameraPitch);
    const float sp = sinf(app.cameraPitch);

    const Vec3 camera = app.flyCamera ? app.cameraPosition : Vec3{
        app.cameraTarget.x + sy * cp * app.cameraDistance,
        app.cameraTarget.y + sp * app.cameraDistance,
        app.cameraTarget.z + cy * cp * app.cameraDistance
    };

    const Vec3 rel = { point.x - camera.x, point.y - camera.y, point.z - camera.z };
    const Vec3 right = { cy, 0.0f, -sy };
    const Vec3 up = { -sy * sp, cp, -cy * sp };
    const Vec3 forward = { -sy * cp, -sp, -cy * cp };

    const float x = rel.x * right.x + rel.y * right.y + rel.z * right.z;
    const float y = rel.x * up.x + rel.y * up.y + rel.z * up.z;
    depth = rel.x * forward.x + rel.y * forward.y + rel.z * forward.z;
    const float f = 760.0f / std::max(80.0f, depth);
    return ImVec2(origin.x + size.x * 0.5f + app.cameraPan.x + x * f, origin.y + size.y * 0.5f + app.cameraPan.y - y * f);
}

static bool ProjectPointVisible(const Vec3& point, const AppState& app, const ImVec2& origin, const ImVec2& size, ImVec2& out, float& depth)
{
    out = ProjectPoint(point, app, origin, size, depth);
    return depth > 1.0f;
}

static float DistancePointToSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b)
{
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float wx = point.x - a.x;
    const float wy = point.y - a.y;
    const float lenSq = vx * vx + vy * vy;
    if (lenSq <= 0.0001f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return sqrtf(dx * dx + dy * dy);
    }
    const float t = std::clamp((wx * vx + wy * vy) / lenSq, 0.0f, 1.0f);
    const float px = a.x + vx * t;
    const float py = a.y + vy * t;
    const float dx = point.x - px;
    const float dy = point.y - py;
    return sqrtf(dx * dx + dy * dy);
}

static float TriangleAverageDepth(const MeshTriangle& tri, const AppState& app)
{
    if (tri.a >= static_cast<int>(app.mesh.vertices.size()) || tri.b >= static_cast<int>(app.mesh.vertices.size()) || tri.c >= static_cast<int>(app.mesh.vertices.size())) return 0.0f;
    const Vec3& a = app.mesh.vertices[tri.a];
    const Vec3& b = app.mesh.vertices[tri.b];
    const Vec3& c = app.mesh.vertices[tri.c];
    const Vec3 center = { (a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f, (a.z + b.z + c.z) / 3.0f };
    float depth = 0.0f;
    ProjectPoint(center, app, ImVec2(0, 0), ImVec2(1, 1), depth);
    return depth;
}

static ImU32 SurfaceColor(const MeshTriangle& tri, const AppState& app)
{
    if (tri.surface == "RENDER") {
        if (app.viewMode == LevelViewMode::TextureMode) return TextureColor(tri.texture, tri.color);
        if (tri.a >= static_cast<int>(app.mesh.vertices.size()) || tri.b >= static_cast<int>(app.mesh.vertices.size()) || tri.c >= static_cast<int>(app.mesh.vertices.size())) {
            return IM_COL32(128, 134, 130, 224);
        }
        const Vec3& a = app.mesh.vertices[tri.a];
        const Vec3& b = app.mesh.vertices[tri.b];
        const Vec3& c = app.mesh.vertices[tri.c];
        const Vec3 u = { b.x - a.x, b.y - a.y, b.z - a.z };
        const Vec3 v = { c.x - a.x, c.y - a.y, c.z - a.z };
        Vec3 normal = {
            u.y * v.z - u.z * v.y,
            u.z * v.x - u.x * v.z,
            u.x * v.y - u.y * v.x
        };
        const float len = std::max(1.0f, sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z));
        normal.y /= len;
        const int light = static_cast<int>(std::clamp(118.0f + normal.y * 58.0f, 78.0f, 178.0f));
        return IM_COL32(light, light + 3, light - 6, 226);
    }
    if (tri.a >= static_cast<int>(app.mesh.vertices.size()) || tri.b >= static_cast<int>(app.mesh.vertices.size()) || tri.c >= static_cast<int>(app.mesh.vertices.size())) {
        return IM_COL32(85, 92, 96, 210);
    }
    const Vec3& a = app.mesh.vertices[tri.a];
    const Vec3& b = app.mesh.vertices[tri.b];
    const Vec3& c = app.mesh.vertices[tri.c];
    const Vec3 u = { b.x - a.x, b.y - a.y, b.z - a.z };
    const Vec3 v = { c.x - a.x, c.y - a.y, c.z - a.z };
    Vec3 normal = {
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    const float len = std::max(1.0f, sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z));
    normal.y /= len;

    const bool grass = tri.surface.find("GRASS") != std::string::npos || tri.surface.find("DEFAULT") != std::string::npos;
    const bool water = tri.surface.find("WATER") != std::string::npos;
    const bool sand = tri.surface.find("SAND") != std::string::npos;
    int light = static_cast<int>(std::clamp(110.0f + normal.y * 80.0f, 60.0f, 210.0f));
    if (water) return IM_COL32(45, 105, 160, 175);
    if (sand) return IM_COL32(light + 20, light + 5, 105, 220);
    if (grass) return IM_COL32(55, std::min(205, light + 35), 80, 220);
    return IM_COL32(light, light, light - 10, 220);
}

static void CameraVectors(const AppState& app, Vec3& forward, Vec3& right, Vec3& up)
{
    const float cy = cosf(app.cameraYaw);
    const float sy = sinf(app.cameraYaw);
    const float cp = cosf(app.cameraPitch);
    const float sp = sinf(app.cameraPitch);
    forward = { -sy * cp, -sp, -cy * cp };
    right = { cy, 0.0f, -sy };
    up = { -sy * sp, cp, -cy * sp };
}

static Vec3 CurrentCameraPosition(const AppState& app)
{
    if (app.flyCamera) return app.cameraPosition;
    const float cy = cosf(app.cameraYaw);
    const float sy = sinf(app.cameraYaw);
    const float cp = cosf(app.cameraPitch);
    const float sp = sinf(app.cameraPitch);
    return {
        app.cameraTarget.x + sy * cp * app.cameraDistance,
        app.cameraTarget.y + sp * app.cameraDistance,
        app.cameraTarget.z + cy * cp * app.cameraDistance
    };
}

static float ColorChannel(ImU32 color, int shift)
{
    return static_cast<float>((color >> shift) & 0xff) / 255.0f;
}

static ViewportVertex MakeViewportVertex(const Vec3& pos, const ImVec2& uv, ImU32 color)
{
    return {
        pos.x, pos.y, pos.z,
        uv.x, uv.y,
        ColorChannel(color, 0),
        ColorChannel(color, 8),
        ColorChannel(color, 16),
        ColorChannel(color, 24)
    };
}

static void EnsureViewportVertexBuffer(size_t vertexCount)
{
    if (g_pd3dDevice == nullptr || vertexCount == 0) return;
    if (g_viewportRenderer.vertexBuffer != nullptr && g_viewportRenderer.vertexCapacity >= static_cast<int>(vertexCount)) return;
    if (g_viewportRenderer.vertexBuffer != nullptr) {
        g_viewportRenderer.vertexBuffer->Release();
        g_viewportRenderer.vertexBuffer = nullptr;
    }
    g_viewportRenderer.vertexCapacity = static_cast<int>(std::max<size_t>(vertexCount, 4096));
    D3D11_BUFFER_DESC desc{};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = static_cast<UINT>(sizeof(ViewportVertex) * g_viewportRenderer.vertexCapacity);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pd3dDevice->CreateBuffer(&desc, nullptr, &g_viewportRenderer.vertexBuffer);
}

static void UpdateConstantBuffer(ID3D11Buffer* buffer, const void* data, size_t size)
{
    if (buffer == nullptr || g_pd3dDeviceContext == nullptr) return;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(g_pd3dDeviceContext->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, data, size);
        g_pd3dDeviceContext->Unmap(buffer, 0);
    }
}

static void DrawViewportVertices(const std::vector<ViewportVertex>& vertices, ID3D11ShaderResourceView* texture, ID3D11SamplerState* sampler, const ViewportDrawConstants& constants)
{
    if (vertices.empty() || g_viewportRenderer.vertexBuffer == nullptr || g_pd3dDeviceContext == nullptr) return;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_pd3dDeviceContext->Map(g_viewportRenderer.vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(ViewportVertex));
    g_pd3dDeviceContext->Unmap(g_viewportRenderer.vertexBuffer, 0);

    UpdateConstantBuffer(g_viewportRenderer.drawBuffer, &constants, sizeof(constants));
    ID3D11ShaderResourceView* srv = texture != nullptr ? texture : g_viewportRenderer.whiteSrv;
    ID3D11SamplerState* chosenSampler = sampler != nullptr ? sampler : g_clampSamplerState;
    g_pd3dDeviceContext->PSSetShaderResources(0, 1, &srv);
    g_pd3dDeviceContext->PSSetSamplers(0, 1, &chosenSampler);
    g_pd3dDeviceContext->Draw(static_cast<UINT>(vertices.size()), 0);
}

static bool RenderViewportScene(AppState& app, int width, int height)
{
    if (!EnsureViewportTarget(width, height)) return false;
    ID3D11RenderTargetView* rtv = g_viewportRenderer.rtv;
    g_pd3dDeviceContext->OMSetRenderTargets(1, &rtv, g_viewportRenderer.dsv);
    const float clear[] = { 0.074f, 0.083f, 0.090f, 1.0f };
    g_pd3dDeviceContext->ClearRenderTargetView(g_viewportRenderer.rtv, clear);
    g_pd3dDeviceContext->ClearDepthStencilView(g_viewportRenderer.dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    g_pd3dDeviceContext->RSSetViewports(1, &viewport);
    g_pd3dDeviceContext->IASetInputLayout(g_viewportRenderer.inputLayout);
    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(ViewportVertex);
    UINT offset = 0;
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_viewportRenderer.vertexBuffer, &stride, &offset);
    g_pd3dDeviceContext->VSSetShader(g_viewportRenderer.vs, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(g_viewportRenderer.ps, nullptr, 0);
    g_pd3dDeviceContext->VSSetConstantBuffers(0, 1, &g_viewportRenderer.cameraBuffer);
    g_pd3dDeviceContext->PSSetConstantBuffers(1, 1, &g_viewportRenderer.drawBuffer);

    Vec3 forward, right, up;
    CameraVectors(app, forward, right, up);
    const Vec3 camera = CurrentCameraPosition(app);
    ViewportCameraConstants cameraConstants{};
    cameraConstants.camera[0] = camera.x; cameraConstants.camera[1] = camera.y; cameraConstants.camera[2] = camera.z;
    cameraConstants.right[0] = right.x; cameraConstants.right[1] = right.y; cameraConstants.right[2] = right.z;
    cameraConstants.up[0] = up.x; cameraConstants.up[1] = up.y; cameraConstants.up[2] = up.z;
    cameraConstants.forward[0] = forward.x; cameraConstants.forward[1] = forward.y; cameraConstants.forward[2] = forward.z;
    cameraConstants.viewport[0] = static_cast<float>(width);
    cameraConstants.viewport[1] = static_cast<float>(height);
    cameraConstants.viewport[2] = 8.0f;
    cameraConstants.viewport[3] = 24000.0f;
    UpdateConstantBuffer(g_viewportRenderer.cameraBuffer, &cameraConstants, sizeof(cameraConstants));

    if (app.viewportBatchesDirty || app.cachedViewMode != app.viewMode) {
        app.viewportBatches.clear();
        app.cachedViewMode = app.viewMode;
        std::unordered_map<std::string, size_t> batchByKey;
        auto getBatch = [&](const std::string& key) -> ViewportMeshBatch& {
            auto found = batchByKey.find(key);
            if (found != batchByKey.end()) return app.viewportBatches[found->second];
            ViewportMeshBatch batch;
            batchByKey[key] = app.viewportBatches.size();
            app.viewportBatches.push_back(std::move(batch));
            return app.viewportBatches.back();
        };

        for (const MeshTriangle& tri : app.mesh.triangles) {
            if (tri.a >= static_cast<int>(app.mesh.vertices.size()) || tri.b >= static_cast<int>(app.mesh.vertices.size()) || tri.c >= static_cast<int>(app.mesh.vertices.size())) continue;
            const bool isWater = tri.surface.find("WATER") != std::string::npos;
            const bool textured = app.viewMode == LevelViewMode::TextureMode && tri.surface == "RENDER";
            ImU32 color = SurfaceColor(tri, app);
            if (app.viewMode == LevelViewMode::TextureMode && tri.surface == "RENDER") color = IM_COL32(255, 255, 255, 255);
            if (isWater) color = IM_COL32(70, 145, 190, 105);

            GpuTexture* gpuTexture = nullptr;
            std::string key = isWater ? "__water" : "__flat";
            if (textured) {
                gpuTexture = GetGpuTexture(app, tri);
                if (gpuTexture != nullptr) key = TextureCacheKey(tri);
            }

            ViewportMeshBatch& batch = getBatch(key);
            batch.water = isWater;
            batch.textured = gpuTexture != nullptr && !isWater;
            batch.clamp = false;
            if (gpuTexture != nullptr) batch.textureKey = TextureCacheKey(tri);

            ImVec2 uva = tri.uva, uvb = tri.uvb, uvc = tri.uvc;
            if (gpuTexture != nullptr) {
                uva = NormalizeUv(tri.uva, *gpuTexture, tri);
                uvb = NormalizeUv(tri.uvb, *gpuTexture, tri);
                uvc = NormalizeUv(tri.uvc, *gpuTexture, tri);
            }
            batch.vertices.push_back(MakeViewportVertex(app.mesh.vertices[tri.a], uva, color));
            batch.vertices.push_back(MakeViewportVertex(app.mesh.vertices[tri.b], uvb, color));
            batch.vertices.push_back(MakeViewportVertex(app.mesh.vertices[tri.c], uvc, color));
        }
        app.viewportBatchesDirty = false;
    }

    size_t maxVertices = 0;
    for (const ViewportMeshBatch& batch : app.viewportBatches) maxVertices = std::max(maxVertices, batch.vertices.size());
    EnsureViewportVertexBuffer(maxVertices);
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_viewportRenderer.vertexBuffer, &stride, &offset);

    float blendFactor[4] = { 0, 0, 0, 0 };
    g_pd3dDeviceContext->OMSetBlendState(g_viewportRenderer.blendState, blendFactor, 0xffffffff);
    g_pd3dDeviceContext->OMSetDepthStencilState(g_viewportRenderer.depthWriteState, 0);
    g_pd3dDeviceContext->RSSetState(app.viewMode == LevelViewMode::Wireframe ? g_viewportRenderer.wireRasterizer : g_viewportRenderer.solidRasterizer);

    ViewportDrawConstants drawConstants{};
    drawConstants.useTexture = 0.0f;
    drawConstants.alphaTest = 0.0f;
    drawConstants.alphaScale = 1.0f;
    for (const ViewportMeshBatch& batch : app.viewportBatches) {
        if (batch.water || batch.textured) continue;
        DrawViewportVertices(batch.vertices, g_viewportRenderer.whiteSrv, g_clampSamplerState, drawConstants);
    }

    drawConstants.useTexture = 1.0f;
    drawConstants.alphaTest = 1.0f;
    drawConstants.alphaScale = 1.0f;
    for (const ViewportMeshBatch& batch : app.viewportBatches) {
        if (!batch.textured || batch.water) continue;
        auto cached = app.textureCache.find(batch.textureKey);
        ID3D11ShaderResourceView* srv = cached != app.textureCache.end() ? cached->second.srv : nullptr;
        DrawViewportVertices(batch.vertices, srv, g_wrapSamplerState, drawConstants);
    }

    g_pd3dDeviceContext->OMSetDepthStencilState(g_viewportRenderer.depthReadState, 0);
    drawConstants.useTexture = 0.0f;
    drawConstants.alphaTest = 0.0f;
    drawConstants.alphaScale = 0.55f;
    for (const ViewportMeshBatch& batch : app.viewportBatches) {
        if (!batch.water) continue;
        DrawViewportVertices(batch.vertices, g_viewportRenderer.whiteSrv, g_clampSamplerState, drawConstants);
    }

    ID3D11ShaderResourceView* nullSrv = nullptr;
    g_pd3dDeviceContext->PSSetShaderResources(0, 1, &nullSrv);
    g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

static void Draw3DViewport(AppState& app)
{
    ImGui::BeginChild("Viewport", ImVec2(0.0f, -4.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x = std::max(canvasSize.x, 320.0f);
    canvasSize.y = std::max(canvasSize.y, 260.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(19, 22, 25, 255));
    draw->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(64, 72, 76, 255));
    ImGui::InvisibleButton("ViewportCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    app.textureTrianglesDrawn = 0;
    app.textureTrianglesMissing = 0;

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        app.cameraYaw += delta.x * 0.008f;
        app.cameraPitch = std::clamp(app.cameraPitch + delta.y * 0.006f, -1.15f, 1.20f);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        app.cameraPan.x += delta.x;
        app.cameraPan.y += delta.y;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        app.cameraDistance = std::clamp(app.cameraDistance - ImGui::GetIO().MouseWheel * 85.0f, 260.0f, 4200.0f);
    }
    if (hovered && app.flyCamera) {
        Vec3 forward, right, up;
        CameraVectors(app, forward, right, up);
        const float speed = ImGui::GetIO().KeyShift ? 42.0f : 16.0f;
        auto move = [&](const Vec3& direction, float amount) {
            app.cameraPosition.x += direction.x * amount;
            app.cameraPosition.y += direction.y * amount;
            app.cameraPosition.z += direction.z * amount;
        };
        if (ImGui::IsKeyDown(ImGuiKey_W)) move(forward, speed);
        if (ImGui::IsKeyDown(ImGuiKey_S)) move(forward, -speed);
        if (ImGui::IsKeyDown(ImGuiKey_D)) move(right, speed);
        if (ImGui::IsKeyDown(ImGuiKey_A)) move(right, -speed);
        if (ImGui::IsKeyDown(ImGuiKey_E)) app.cameraPosition.y += speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) app.cameraPosition.y -= speed;
    }

    if (!app.mesh.triangles.empty() && RenderViewportScene(app, static_cast<int>(canvasSize.x), static_cast<int>(canvasSize.y)) && g_viewportRenderer.srv != nullptr) {
        draw->AddImage(reinterpret_cast<ImTextureID>(g_viewportRenderer.srv),
            canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));
        if (app.viewMode == LevelViewMode::TextureMode && app.mesh.renderMesh) {
            const std::string textureStatus = "DX11 viewport: " + std::to_string(app.mesh.triangles.size()) + " triangles";
            draw->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y - 24.0f), IM_COL32(190, 230, 180, 230), textureStatus.c_str());
        }
    } else {
        draw->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + 30.0f), IM_COL32(230, 190, 90, 255), "No renderable level mesh found yet");
    }

    const ImVec2 gizmoPanel(canvasPos.x + 12.0f, canvasPos.y + canvasSize.y - 58.0f);
    draw->AddRectFilled(gizmoPanel, ImVec2(gizmoPanel.x + 178.0f, gizmoPanel.y + 30.0f), IM_COL32(16, 18, 20, 205), 4.0f);
    draw->AddText(ImVec2(gizmoPanel.x + 10.0f, gizmoPanel.y + 8.0f), app.rotateGizmo ? IM_COL32(230, 210, 130, 255) : IM_COL32(160, 220, 185, 255),
        app.rotateGizmo ? "Gizmo: Rotate" : "Gizmo: Move");
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_R, false)) app.rotateGizmo = true;
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_G, false)) app.rotateGizmo = false;

    int hoveredObject = -1;
    float bestDistance = 99999.0f;
    for (int i = 0; i < static_cast<int>(app.objects.size()); ++i) {
        float depth = 0.0f;
        ImVec2 p;
        if (!ProjectPointVisible(app.objects[i].position, app, canvasPos, canvasSize, p, depth)) continue;
        const float r = std::clamp(1200.0f / depth, 5.0f, 15.0f) * app.objects[i].scale;
        const float dist = (mouse.x - p.x) * (mouse.x - p.x) + (mouse.y - p.y) * (mouse.y - p.y);
        if (dist < (r + 8.0f) * (r + 8.0f) && dist < bestDistance) {
            hoveredObject = i;
            bestDistance = dist;
        }
    }

    int hoveredAxis = -1;
    ImVec2 axisStart{};
    std::array<ImVec2, 3> axisEnds{};
    std::array<bool, 3> axisVisible{ false, false, false };
    if (app.selectedObject >= 0 && app.selectedObject < static_cast<int>(app.objects.size())) {
        const Vec3 origin = app.objects[app.selectedObject].position;
        float originDepth = 0.0f;
        if (ProjectPointVisible(origin, app, canvasPos, canvasSize, axisStart, originDepth)) {
            const float axisLen = std::clamp(originDepth * 0.16f, 120.0f, 420.0f);
            const Vec3 axisPoints[3] = {
                { origin.x + axisLen, origin.y, origin.z },
                { origin.x, origin.y + axisLen, origin.z },
                { origin.x, origin.y, origin.z + axisLen }
            };
            float bestAxisDistance = 16.0f;
            for (int axis = 0; axis < 3; ++axis) {
                float depth = 0.0f;
                axisVisible[axis] = ProjectPointVisible(axisPoints[axis], app, canvasPos, canvasSize, axisEnds[axis], depth);
                if (!axisVisible[axis]) continue;
                const float dist = DistancePointToSegment(mouse, axisStart, axisEnds[axis]);
                if (dist < bestAxisDistance) {
                    hoveredAxis = axis;
                    bestAxisDistance = dist;
                }
            }
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredAxis >= 0) {
            app.activeGizmoAxis = hoveredAxis;
        } else {
            app.selectedObject = hoveredObject;
            app.activeGizmoAxis = -1;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        app.activeGizmoAxis = -1;
    }
    if (hovered && app.selectedObject >= 0 && app.activeGizmoAxis >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        const ImVec2 a = axisStart;
        const ImVec2 b = axisEnds[app.activeGizmoAxis];
        const float vx = b.x - a.x;
        const float vy = b.y - a.y;
        const float len = std::max(1.0f, sqrtf(vx * vx + vy * vy));
        const float signedPixels = (delta.x * vx + delta.y * vy) / len;
        if (app.rotateGizmo) {
            float* components[3] = {
                &app.objects[app.selectedObject].rotation.x,
                &app.objects[app.selectedObject].rotation.y,
                &app.objects[app.selectedObject].rotation.z
            };
            *components[app.activeGizmoAxis] += signedPixels * 0.6f;
        } else {
            Vec3* pos = &app.objects[app.selectedObject].position;
            if (app.activeGizmoAxis == 0) pos->x += signedPixels * 3.0f;
            if (app.activeGizmoAxis == 1) pos->y += signedPixels * 3.0f;
            if (app.activeGizmoAxis == 2) pos->z += signedPixels * 3.0f;
        }
        app.levelDirty = true;
    } else if (hovered && app.selectedObject >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && hoveredAxis < 0) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        app.objects[app.selectedObject].position.x += delta.x * 2.0f;
        app.objects[app.selectedObject].position.z += delta.y * 2.0f;
        app.levelDirty = true;
    }

    for (int i = 0; i < static_cast<int>(app.objects.size()); ++i) {
        float depth = 0.0f;
        ImVec2 p;
        if (!ProjectPointVisible(app.objects[i].position, app, canvasPos, canvasSize, p, depth)) continue;
        const float r = std::clamp(1200.0f / depth, 5.0f, 15.0f) * app.objects[i].scale;
        const bool selected = i == app.selectedObject;
        const bool hot = i == hoveredObject;
        const std::string signature = app.objects[i].model + " " + app.objects[i].behavior;
        ImU32 fill = selected ? IM_COL32(246, 195, 84, 255) : (hot ? IM_COL32(126, 206, 185, 255) : IM_COL32(78, 149, 190, 255));
        if (signature.find("PIPE") != std::string::npos || signature.find("Pipe") != std::string::npos) {
            draw->AddRectFilled(ImVec2(p.x - r * 1.25f, p.y - r * 2.2f), ImVec2(p.x + r * 1.25f, p.y + r * 1.0f), IM_COL32(46, 158, 78, 255), 3.0f);
            draw->AddEllipseFilled(ImVec2(p.x, p.y - r * 2.2f), ImVec2(r * 1.5f, r * 0.55f), IM_COL32(76, 214, 112, 255), 24);
            draw->AddEllipse(ImVec2(p.x, p.y - r * 2.2f), ImVec2(r * 1.5f, r * 0.55f), IM_COL32(14, 70, 34, 255), 24, 2.0f);
        } else if (signature.find("COIN") != std::string::npos || signature.find("Coin") != std::string::npos) {
            draw->AddEllipseFilled(p, ImVec2(r * 0.55f, r * 1.35f), IM_COL32(248, 210, 48, 255), 24);
            draw->AddEllipse(p, ImVec2(r * 0.55f, r * 1.35f), IM_COL32(118, 83, 14, 255), 24, 2.0f);
        } else if (signature.find("STAR") != std::string::npos || signature.find("Star") != std::string::npos) {
            std::array<ImVec2, 10> pts{};
            for (int n = 0; n < 10; ++n) {
                const float a = -1.5708f + n * 0.6283f;
                const float rr = (n % 2 == 0) ? r * 1.55f : r * 0.72f;
                pts[n] = ImVec2(p.x + cosf(a) * rr, p.y + sinf(a) * rr);
            }
            draw->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), IM_COL32(255, 214, 64, 255));
            draw->AddPolyline(pts.data(), static_cast<int>(pts.size()), IM_COL32(120, 82, 12, 255), ImDrawFlags_Closed, 2.0f);
        } else if (signature.find("GOOMBA") != std::string::npos || signature.find("Goomba") != std::string::npos) {
            draw->AddCircleFilled(ImVec2(p.x, p.y - r * 0.25f), r * 1.2f, IM_COL32(142, 82, 42, 255), 24);
            draw->AddRectFilled(ImVec2(p.x - r * 1.05f, p.y + r * 0.2f), ImVec2(p.x + r * 1.05f, p.y + r * 1.15f), IM_COL32(92, 48, 28, 255), 4.0f);
            draw->AddCircleFilled(ImVec2(p.x - r * 0.42f, p.y - r * 0.35f), r * 0.18f, IM_COL32(24, 18, 16, 255));
            draw->AddCircleFilled(ImVec2(p.x + r * 0.42f, p.y - r * 0.35f), r * 0.18f, IM_COL32(24, 18, 16, 255));
        } else if (signature.find("KOOPA") != std::string::npos || signature.find("Koopa") != std::string::npos) {
            draw->AddCircleFilled(ImVec2(p.x, p.y - r * 0.15f), r * 1.05f, IM_COL32(75, 170, 64, 255), 24);
            draw->AddCircleFilled(ImVec2(p.x, p.y + r * 0.28f), r * 0.78f, IM_COL32(230, 202, 88, 255), 24);
            draw->AddCircleFilled(ImVec2(p.x, p.y - r * 1.1f), r * 0.48f, IM_COL32(82, 190, 72, 255), 18);
            draw->AddCircleFilled(ImVec2(p.x - r * 0.36f, p.y - r * 1.18f), r * 0.08f, IM_COL32(20, 24, 16, 255));
            draw->AddCircleFilled(ImVec2(p.x + r * 0.36f, p.y - r * 1.18f), r * 0.08f, IM_COL32(20, 24, 16, 255));
        } else {
            draw->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), fill, 3.0f);
            draw->AddLine(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), IM_COL32(210, 230, 236, 120), 1.0f);
            draw->AddLine(ImVec2(p.x + r, p.y - r), ImVec2(p.x - r, p.y + r), IM_COL32(210, 230, 236, 120), 1.0f);
        }
        draw->AddCircle(p, r + 3.0f, selected ? IM_COL32(255, 255, 255, 230) : IM_COL32(16, 18, 20, 210), 20, selected ? 2.0f : 1.0f);
        if (selected || hot) {
            draw->AddText(ImVec2(p.x + r + 6.0f, p.y - 8.0f), IM_COL32(226, 231, 232, 255), app.objects[i].name.c_str());
        }
    }

    if (app.selectedObject >= 0 && app.selectedObject < static_cast<int>(app.objects.size())) {
        const ImU32 axisColors[3] = {
            IM_COL32(235, 75, 75, 245),
            IM_COL32(82, 210, 105, 245),
            IM_COL32(78, 145, 245, 245)
        };
        const char* labels[3] = { "X", "Y", "Z" };
        for (int axis = 0; axis < 3; ++axis) {
            if (!axisVisible[axis]) continue;
            const bool active = app.activeGizmoAxis == axis;
            const bool hotAxis = hoveredAxis == axis;
            const float thickness = active ? 5.0f : (hotAxis ? 4.0f : 2.5f);
            draw->AddLine(axisStart, axisEnds[axis], axisColors[axis], thickness);
            if (app.rotateGizmo) {
                draw->AddCircle(axisStart, DistancePointToSegment(axisEnds[axis], axisStart, axisEnds[axis]) + 18.0f + axis * 7.0f,
                    axisColors[axis], 48, hotAxis || active ? 2.5f : 1.5f);
            }
            draw->AddCircleFilled(axisEnds[axis], hotAxis || active ? 7.5f : 6.0f, axisColors[axis], 18);
            draw->AddText(ImVec2(axisEnds[axis].x + 8.0f, axisEnds[axis].y - 8.0f), axisColors[axis], labels[axis]);
        }
    }

    draw->AddText(ImVec2(canvasPos.x + 12.0f, canvasPos.y + 10.0f), IM_COL32(198, 206, 208, 255),
        app.flyCamera ? "Right-drag look  WASD/QE fly  G move gizmo  R rotate gizmo" : "Right-drag orbit  Middle-drag pan  Wheel zoom  G move gizmo  R rotate gizmo");
    ImGui::EndChild();
}

static void RenderEditor(AppState& app)
{
    PollLevelLoad(app);
    const bool loadingLevel = app.levelLoadJob.running.load();
    ImGui::BeginChild("EditorToolbar", ImVec2(0.0f, 38.0f), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted("Level Editor");
    ImGui::SameLine();
    ImGui::BeginDisabled(loadingLevel || app.editorMode != EditorMode::EditingLevel || app.selectedLevel < 0);
    if (ImGui::Button("Save And Write", ImVec2(130.0f, 26.0f))) {
        std::string error;
        std::string warning;
        if (!WriteLevelScene(app.project, app.levels[app.selectedLevel], app.objects, error)) {
            app.status = "Scene save failed: " + error;
        } else if (!WriteObjectsToLevelScript(app.levels[app.selectedLevel], app.objects, error, &warning)) {
            app.status = "Script write failed: " + error;
        } else {
            app.levelDirty = false;
            app.status = warning.empty() ? "Saved and wrote level script." : warning;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Texture", ImVec2(125.0f, 26.0f))) {
        fs::path picked;
        if (PickTextureFile(picked)) {
            std::string error;
            if (CopyTextureIntoLevel(app.levels[app.selectedLevel], picked, error)) {
                app.textures = ScanLevelTextures(app.levels[app.selectedLevel]);
                app.status = "Imported texture " + picked.filename().string() + ".";
            } else {
                app.status = "Texture import failed: " + error;
            }
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", loadingLevel ? app.status.c_str() : (app.levelDirty ? "Unsaved changes" : app.status.c_str()));
    ImGui::EndChild();

    ImGui::BeginChild("LeftRail", ImVec2(260.0f, 0.0f), true);
    ImGui::Text("Project");
    ImGui::TextDisabled("%s", ToUtf8(app.project.root).c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(loadingLevel);
    if (ImGui::Button("Level Menu", ImVec2(-1.0f, 30.0f))) {
        if (app.levelDirty) {
            app.pendingLevelMenu = true;
            ImGui::OpenPopup("Unsaved Changes");
        } else {
            app.editorMode = EditorMode::LevelMenu;
            app.selectedLevel = -1;
            app.selectedObject = -1;
            app.selectedTexture = -1;
            app.objects.clear();
            app.mesh = {};
            app.textures.clear();
            ReleaseTextureCache(app);
        }
    }
    ImGui::Separator();
    ImGui::Text("Existing Levels");
    for (int i = 0; i < static_cast<int>(app.levels.size()); ++i) {
        if (ImGui::Selectable(app.levels[i].name.c_str(), app.selectedLevel == i)) {
            StartLevelLoad(app, i);
        }
    }
    ImGui::EndDisabled();
    if (app.levels.empty()) ImGui::TextDisabled("No levels folder found.");
    ImGui::Separator();
    ImGui::Text("Add To Level");
    ImGui::BeginDisabled(loadingLevel || app.editorMode != EditorMode::EditingLevel);
    const bool canAddGoomba = app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size()) &&
        LevelScriptSupportsModel(app.levels[app.selectedLevel], "MODEL_GOOMBA");
    const bool canAddKoopa = app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size()) &&
        LevelScriptSupportsModel(app.levels[app.selectedLevel], "MODEL_KOOPA_WITH_SHELL");
    ImGui::BeginDisabled(!canAddGoomba);
    if (ImGui::Button("Goomba", ImVec2(-1.0f, 28.0f))) AddPresetObject(app, "Goomba", "MODEL_GOOMBA", "bhvGoomba");
    if (!canAddGoomba && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This level must load common0 before Goombas can be written safely.");
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!canAddKoopa);
    if (ImGui::Button("Koopa", ImVec2(-1.0f, 28.0f))) AddPresetObject(app, "Koopa", "MODEL_KOOPA_WITH_SHELL", "bhvKoopa", 1.0f, "0x00010000");
    if (!canAddKoopa && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Koopa belongs to actor group14. This level is using another segment 0x06/0x0D actor group.");
    }
    ImGui::EndDisabled();
    if (ImGui::Button("Coin", ImVec2(-1.0f, 28.0f))) AddPresetObject(app, "Yellow Coin", "MODEL_YELLOW_COIN", "bhvYellowCoin", 0.75f);
    if (ImGui::Button("Star", ImVec2(-1.0f, 28.0f))) AddPresetObject(app, "Power Star", "MODEL_STAR", "bhvStar", 1.15f);
    if (ImGui::Button("Warp Pipe", ImVec2(-1.0f, 28.0f))) AddPresetObject(app, "Warp Pipe", "MODEL_BITS_WARP_PIPE", "bhvWarpPipe", 1.45f);
    if (ImGui::Button("Custom Model", ImVec2(-1.0f, 28.0f))) AddPresetObject(app, "Custom Model", "MODEL_NONE", "bhvStaticObject", 1.25f);
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine();

    if (loadingLevel) {
        ImGui::BeginChild("LoadingLevel", ImVec2(0.0f, 0.0f), true);
        const float pulse = 0.5f + 0.5f * sinf(static_cast<float>(ImGui::GetTime()) * 5.0f);
        ImGui::SetCursorPosY(96.0f);
        ImGui::Text("Loading %s", app.levelLoadJob.levelName.c_str());
        ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "Reading HackerSM64 level data...");
        ImGui::TextDisabled("Parsing script objects, render mesh, collision fallback, and level textures.");
        ImGui::TextDisabled("BoxStudio is still responsive while this runs.");
        ImGui::EndChild();
        return;
    }

    if (app.editorMode == EditorMode::LevelMenu) {
        ImGui::BeginChild("LevelMenu", ImVec2(0.0f, 0.0f), true);
        ImGui::Text("Level Editor");
        ImGui::Separator();
        ImGui::Text("Edit Existing Level");
        ImGui::TextDisabled("Choose a HackerSM64 level folder from levels/. BoxStudio loads script objects plus collision geometry from collision.inc.c.");
        ImGui::BeginChild("LevelGrid", ImVec2(0.0f, 230.0f), true);
        for (int i = 0; i < static_cast<int>(app.levels.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Button(app.levels[i].name.c_str(), ImVec2(170.0f, 44.0f))) StartLevelLoad(app, i);
            ImGui::SameLine();
            if ((i + 1) % 4 == 0) ImGui::NewLine();
            ImGui::PopID();
        }
        if (app.levels.empty()) ImGui::TextDisabled("No HackerSM64 levels were found in this project.");
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Text("Add New Level");
        ImGui::TextDisabled("This creates a BoxStudio starter level folder with script, header, geo, and collision files.");
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("Level folder name", app.newLevelName, IM_ARRAYSIZE(app.newLevelName));
        ImGui::SameLine();
        if (ImGui::Button("Create Level", ImVec2(130.0f, 0.0f))) {
            LevelInfo info;
            std::string error;
            if (CreateBoxStudioLevel(app.project, app.newLevelName, info, error)) {
                app.levels = DiscoverLevels(app.project.root);
                for (int i = 0; i < static_cast<int>(app.levels.size()); ++i) {
                    if (app.levels[i].name == info.name) {
                        StartLevelLoad(app, i);
                        break;
                    }
                }
                app.status = "Created " + info.name + ". Add it to HackerSM64's level tables before building.";
            } else {
                app.status = "Could not create level: " + error;
            }
        }

        ImGui::Spacing();
        ImGui::TextWrapped("HackerSM64 keeps level assets under levels/<name>/, with script, geo, collision, and area files. BoxStudio now uses that layout as its editing surface and stores editor-only scene metadata under .boxstudio.");
        ImGui::TextDisabled("Status: %s", app.status.c_str());
        ImGui::EndChild();
        return;
    }

    ImGui::BeginChild("Center", ImVec2(-320.0f, 0.0f), true);
    if (app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size())) {
        ImGui::Text("Level Editor: %s", app.levels[app.selectedLevel].name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d objects  %d %s tris", static_cast<int>(app.objects.size()), static_cast<int>(app.mesh.triangles.size()), app.mesh.renderMesh ? "render" : "collision");
    } else {
        ImGui::Text("Level Editor");
    }
    ImGui::Separator();
    if (ImGui::Button(app.viewMode == LevelViewMode::Wireframe ? "[Wireframe]" : "Wireframe", ImVec2(112.0f, 28.0f))) app.viewMode = LevelViewMode::Wireframe;
    ImGui::SameLine();
    if (ImGui::Button(app.viewMode == LevelViewMode::GeometryOnly ? "[Geometry Only]" : "Geometry Only", ImVec2(134.0f, 28.0f))) app.viewMode = LevelViewMode::GeometryOnly;
    ImGui::SameLine();
    if (ImGui::Button(app.viewMode == LevelViewMode::TextureMode ? "[Texture Mode]" : "Texture Mode", ImVec2(128.0f, 28.0f))) app.viewMode = LevelViewMode::TextureMode;
    if (app.cachedViewMode != app.viewMode) {
        app.cachedViewMode = app.viewMode;
        app.viewportBatchesDirty = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Fly", &app.flyCamera);
    if (app.levelDirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.96f, 0.72f, 0.30f, 1.0f), "Unsaved changes");
    }
    ImGui::BeginTabBar("LevelTabs");
    if (ImGui::BeginTabItem("Scene")) {
        Draw3DViewport(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Models")) {
        ImGui::Text("Model Slots");
        ImGui::TextDisabled("Use the inspector's Model field for now. HackerSM64 supports large model-id ranges, so BoxStudio keeps this text-based until it can index project headers safely.");
        ImGui::BulletText("MODEL_GOOMBA");
        ImGui::BulletText("MODEL_YELLOW_COIN");
        ImGui::BulletText("MODEL_STAR");
        ImGui::BulletText("MODEL_BITS_WARP_PIPE");
        ImGui::BulletText("MODEL_NONE");
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Textures")) {
        ImGui::Text("Level Textures");
        ImGui::TextDisabled("Import copies a texture into levels/<level>/textures. Replace overwrites the selected asset.");
        if (ImGui::Button("Refresh", ImVec2(92.0f, 28.0f))) {
            if (app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size())) {
                app.textures = ScanLevelTextures(app.levels[app.selectedLevel]);
                app.selectedTexture = app.textures.empty() ? -1 : std::clamp(app.selectedTexture, 0, static_cast<int>(app.textures.size()) - 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Texture", ImVec2(140.0f, 28.0f))) {
            fs::path picked;
            if (PickTextureFile(picked) && app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size())) {
                std::string error;
                if (CopyTextureIntoLevel(app.levels[app.selectedLevel], picked, error)) {
                    app.textures = ScanLevelTextures(app.levels[app.selectedLevel]);
                    app.status = "Imported texture " + picked.filename().string() + ".";
                } else {
                    app.status = "Texture import failed: " + error;
                }
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(app.selectedTexture < 0 || app.selectedTexture >= static_cast<int>(app.textures.size()));
        if (ImGui::Button("Replace Selected", ImVec2(150.0f, 28.0f))) {
            fs::path picked;
            if (PickTextureFile(picked)) {
                std::string error;
                if (ReplaceTextureAsset(app.textures[app.selectedTexture], picked, error)) {
                    app.status = "Replaced " + app.textures[app.selectedTexture].name + ".";
                    app.textures = ScanLevelTextures(app.levels[app.selectedLevel]);
                } else {
                    app.status = "Texture replace failed: " + error;
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::BeginChild("TextureList", ImVec2(0.0f, 0.0f), true);
        for (int i = 0; i < static_cast<int>(app.textures.size()); ++i) {
            if (ImGui::Selectable(app.textures[i].name.c_str(), app.selectedTexture == i)) app.selectedTexture = i;
            if (app.selectedTexture == i) ImGui::TextDisabled("%s", ToUtf8(app.textures[i].path).c_str());
        }
        if (app.textures.empty()) ImGui::TextDisabled("No level-local texture files found yet.");
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Level Files")) {
        if (app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size())) {
            ImGui::Text("Script: %s", ToUtf8(app.levels[app.selectedLevel].scriptPath).c_str());
            ImGui::Text("Folder: %s", ToUtf8(app.levels[app.selectedLevel].path).c_str());
            ImGui::Text("Collision sources: %d", static_cast<int>(app.mesh.sources.size()));
            for (const fs::path& source : app.mesh.sources) {
                ImGui::BulletText("%s", ToUtf8(source).c_str());
            }
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::Separator();
    if (app.selectedLevel >= 0 && app.selectedLevel < static_cast<int>(app.levels.size())) {
        if (ImGui::Button("Save And Write", ImVec2(150.0f, 30.0f))) {
            std::string error;
            std::string warning;
            if (!WriteLevelScene(app.project, app.levels[app.selectedLevel], app.objects, error)) {
                app.status = "Scene save failed: " + error;
            } else if (!WriteObjectsToLevelScript(app.levels[app.selectedLevel], app.objects, error, &warning)) {
                app.status = "Script write failed: " + error;
            } else {
                app.levelDirty = false;
                app.status = warning.empty()
                    ? "Saved metadata and wrote object changes to " + app.levels[app.selectedLevel].scriptPath.filename().string() + "."
                    : warning;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", app.status.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Inspector", ImVec2(0.0f, 0.0f), true);
    ImGui::Text("Inspector");
    ImGui::Separator();
    if (app.selectedObject >= 0 && app.selectedObject < static_cast<int>(app.objects.size())) {
        LevelObject& object = app.objects[app.selectedObject];
        char name[128];
        char model[128];
        char behParam[128];
        char behavior[160];
        strncpy_s(name, object.name.c_str(), _TRUNCATE);
        strncpy_s(model, object.model.c_str(), _TRUNCATE);
        strncpy_s(behParam, object.behParam.c_str(), _TRUNCATE);
        strncpy_s(behavior, object.behavior.c_str(), _TRUNCATE);
        if (ImGui::InputText("Name", name, IM_ARRAYSIZE(name))) { object.name = name; app.levelDirty = true; }
        if (ImGui::InputText("Model", model, IM_ARRAYSIZE(model))) { object.model = model; app.levelDirty = true; }
        if (ImGui::InputText("Behavior Param", behParam, IM_ARRAYSIZE(behParam))) { object.behParam = behParam; app.levelDirty = true; }
        if (ImGui::InputText("Behavior", behavior, IM_ARRAYSIZE(behavior))) { object.behavior = behavior; app.levelDirty = true; }
        if (ImGui::DragFloat3("Position", &object.position.x, 1.0f)) app.levelDirty = true;
        if (ImGui::DragFloat3("Rotation", &object.rotation.x, 1.0f)) app.levelDirty = true;
        if (ImGui::DragFloat("Scale", &object.scale, 0.02f, 0.25f, 8.0f)) app.levelDirty = true;
        if (ImGui::Checkbox("Loaded from script", &object.fromScript)) app.levelDirty = true;
        ImGui::Spacing();
        if (ImGui::Button("Duplicate", ImVec2(120.0f, 30.0f))) {
            LevelObject clone = object;
            clone.name += " Copy";
            clone.fromScript = false;
            clone.position.x += 80.0f;
            clone.position.z += 80.0f;
            app.objects.push_back(clone);
            app.selectedObject = static_cast<int>(app.objects.size()) - 1;
            app.levelDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(90.0f, 30.0f))) {
            app.objects.erase(app.objects.begin() + app.selectedObject);
            app.selectedObject = app.objects.empty() ? -1 : std::min(app.selectedObject, static_cast<int>(app.objects.size()) - 1);
            app.levelDirty = true;
        }
        ImGui::TextWrapped("Save And Write updates the level script object macros so rebuilds include these object edits.");
    } else {
        ImGui::TextDisabled("Select an object in the viewport or object list.");
    }
    ImGui::Separator();
    ImGui::Text("Objects");
    for (int i = 0; i < static_cast<int>(app.objects.size()); ++i) {
        if (ImGui::Selectable(app.objects[i].name.c_str(), app.selectedObject == i)) app.selectedObject = i;
    }
    ImGui::EndChild();
}

static bool SaveAndWriteCurrentLevel(AppState& app)
{
    if (app.selectedLevel < 0 || app.selectedLevel >= static_cast<int>(app.levels.size())) return true;
    std::string error;
    if (!WriteLevelScene(app.project, app.levels[app.selectedLevel], app.objects, error)) {
        app.status = "Scene save failed: " + error;
        return false;
    }
    std::string warning;
    if (!WriteObjectsToLevelScript(app.levels[app.selectedLevel], app.objects, error, &warning)) {
        app.status = "Script write failed: " + error;
        return false;
    }
    app.levelDirty = false;
    app.status = warning.empty() ? "Saved and wrote level changes." : warning;
    return true;
}

static int RunHeadlessRepairLevel(const fs::path& projectPath, const std::string& levelName)
{
    Project project;
    std::string error;
    if (!LoadProject(projectPath, project, error)) {
        printf("BoxStudio repair failed: %s\n", error.c_str());
        return 2;
    }

    std::vector<LevelInfo> levels = DiscoverLevels(project.root);
    auto levelIt = std::find_if(levels.begin(), levels.end(), [&](const LevelInfo& level) {
        return level.name == levelName;
    });
    if (levelIt == levels.end()) {
        printf("BoxStudio repair failed: level '%s' was not found.\n", levelName.c_str());
        return 3;
    }

    std::vector<LevelObject> objects = ParseLevelObjects(*levelIt);
    std::string warning;
    if (!WriteLevelScene(project, *levelIt, objects, error)) {
        printf("BoxStudio repair failed: %s\n", error.c_str());
        return 4;
    }
    if (!WriteObjectsToLevelScript(*levelIt, objects, error, &warning)) {
        printf("BoxStudio repair failed: %s\n", error.c_str());
        return 5;
    }

    printf("BoxStudio repaired %s/%s.\n", project.root.string().c_str(), levelName.c_str());
    if (!warning.empty()) printf("%s\n", warning.c_str());
    return 0;
}

static void LeaveLevelEditor(AppState& app)
{
    app.editorMode = EditorMode::LevelMenu;
    app.selectedLevel = -1;
    app.selectedObject = -1;
    app.selectedTexture = -1;
    app.objects.clear();
    app.mesh = {};
    app.textures.clear();
    app.levelDirty = false;
    app.pendingLevelMenu = false;
}

static void RenderUnsavedPopup(AppState& app)
{
    if (app.pendingExit || app.pendingLevelMenu) ImGui::OpenPopup("Unsaved Changes");
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You have made changes to your project. Do you want to save changes before exiting?");
        ImGui::Spacing();
        if (ImGui::Button("Save And Write", ImVec2(130.0f, 30.0f))) {
            if (SaveAndWriteCurrentLevel(app)) {
                if (app.pendingLevelMenu) LeaveLevelEditor(app);
                if (app.pendingExit) ::PostQuitMessage(0);
                app.pendingExit = false;
                app.pendingLevelMenu = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(100.0f, 30.0f))) {
            app.levelDirty = false;
            if (app.pendingLevelMenu) LeaveLevelEditor(app);
            if (app.pendingExit) ::PostQuitMessage(0);
            app.pendingExit = false;
            app.pendingLevelMenu = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90.0f, 30.0f))) {
            app.pendingExit = false;
            app.pendingLevelMenu = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "--version") {
        printf("BoxStudio 0.1.0-dev\n");
        return 0;
    }
    if (argc >= 4 && std::string(argv[1]) == "--repair-level") {
        return RunHeadlessRepairLevel(fs::path(argv[2]), argv[3]);
    }

    ::AllocConsole();
    freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    ImGui_ImplWin32_EnableDpiAwareness();
    HMONITOR monitor = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    float mainScale = monitor ? ImGui_ImplWin32_GetDpiScaleForMonitor(monitor) : 1.0f;

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"BoxStudioWindow", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"BoxStudio", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        100, 100, static_cast<int>(1400 * mainScale), static_cast<int>(860 * mainScale), nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    ApplyBoxStyle(mainScale);
    ImFont* mainFont = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 17.0f * mainScale);
    if (!mainFont) io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    AppState app;
    g_appState = &app;
    const std::string defaultRoot = ToUtf8(DefaultProjectsRoot());
    strncpy_s(app.projectsRoot, defaultRoot.c_str(), _TRUNCATE);

    bool done = false;
    ImVec4 clearColor = ImVec4(0.075f, 0.078f, 0.086f, 1.0f);
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("BoxStudioRoot", nullptr, flags);
        DrawTopBar(app);
        ImGui::Separator();

        if (app.screen == Screen::Home) RenderHome(app);
        if (app.screen == Screen::CreateProject) RenderCreateProject(app);
        if (app.screen == Screen::Editor) RenderEditor(app);
        RenderUnsavedPopup(app);

        ImGui::End();

        ImGui::Render();
        const float clear[4] = { clearColor.x, clearColor.y, clearColor.z, clearColor.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    if (app.copyJob.worker.joinable()) {
        app.copyJob.worker.join();
    }
    if (app.levelLoadJob.worker.joinable()) {
        app.levelLoadJob.worker.join();
    }
    ReleaseTextureCache(app);
    g_appState = nullptr;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    RECT rect;
    ::GetClientRect(hWnd, &rect);
    UINT width = std::max<UINT>(1280, rect.right - rect.left);
    UINT height = std::max<UINT>(760, rect.bottom - rect.top);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 3;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (res != S_OK) {
        char errorMsg[256];
        sprintf_s(errorMsg, "D3D11CreateDeviceAndSwapChain failed.\nHRESULT: 0x%08lX", res);
        ::MessageBoxA(nullptr, errorMsg, "BoxStudio DirectX Error", MB_OK | MB_ICONERROR);
        return false;
    }

    CreateRenderTarget();
    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = 0.0f;
    g_pd3dDevice->CreateSamplerState(&samplerDesc, &g_wrapSamplerState);
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_pd3dDevice->CreateSamplerState(&samplerDesc, &g_clampSamplerState);
    return true;
}

void CleanupDeviceD3D()
{
    ReleaseViewportRenderer();
    CleanupRenderTarget();
    if (g_wrapSamplerState) { g_wrapSamplerState->Release(); g_wrapSamplerState = nullptr; }
    if (g_clampSamplerState) { g_clampSamplerState->Release(); g_clampSamplerState = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;
    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ::ScreenToClient(hWnd, &pt);
        RECT rect;
        ::GetClientRect(hWnd, &rect);
        const float buttonZoneWidth = 150.0f;
        if (pt.y < 44) {
            if (pt.x > (rect.right - buttonZoneWidth)) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = static_cast<UINT>(LOWORD(lParam));
        g_ResizeHeight = static_cast<UINT>(HIWORD(lParam));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        if (g_appState != nullptr && g_appState->levelDirty) {
            g_appState->pendingExit = true;
            return 0;
        }
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
