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
