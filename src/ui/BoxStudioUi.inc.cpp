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
