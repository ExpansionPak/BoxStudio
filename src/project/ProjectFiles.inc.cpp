static bool ValidateHackerSm64(const fs::path& root, std::string& reason, bool& built)
{
    return boxstudio::decomps::sm64::hackersm64::ValidateWorkspace(root, reason, built);
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
    return boxstudio::decomps::sm64::hackersm64::ModelSegmentRequirement(model);
}

static std::string ModelSupportProblem(const std::string& script, const LevelObject& object)
{
    if (object.model.empty() || object.behavior.empty()) {
        return "object is missing a model or behavior.";
    }

    return boxstudio::decomps::sm64::hackersm64::UnsupportedModelReason(object.model, script);
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
