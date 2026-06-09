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
