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
