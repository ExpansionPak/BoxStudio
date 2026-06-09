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
