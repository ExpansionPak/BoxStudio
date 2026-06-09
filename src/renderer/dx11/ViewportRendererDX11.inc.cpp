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
