#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "BoxStudioApp.h"
#include "decomps/sm64/hackersm64/HackerSM64.h"

namespace fs = std::filesystem;

static int PrintUsage()
{
    std::cout
        << "BoxStudio portable build\n"
        << "\n"
        << "This build is a cross-platform command-line shell used for CI and\n"
        << "non-Windows development while the full editor renderer is being\n"
        << "ported away from Win32/DX11.\n"
        << "\n"
        << "Commands:\n"
        << "  --version\n"
        << "  --check-project <project-root>\n";
    return 0;
}

static int CheckProject(const fs::path& root)
{
    const fs::path manifest = root / ".boxstudio" / "boxstudio.project.json";

    bool ok = true;
    auto require = [&](const fs::path& path, const char* label) {
        if (!fs::exists(path)) {
            std::cerr << "Missing " << label << ": " << path.string() << "\n";
            ok = false;
        }
    };

    require(manifest, "BoxStudio manifest");

    std::string reason;
    bool built = false;
    if (!boxstudio::decomps::sm64::hackersm64::ValidateWorkspace(root, reason, built)) {
        std::cerr << "Invalid HackerSM64 workspace: " << reason << "\n";
        ok = false;
    }

    if (ok) {
        std::cout << "Project looks like a BoxStudio HackerSM64 workspace: "
                  << root.string() << "\n";
        return 0;
    }
    return 2;
}

int RunBoxStudio(int argc, char** argv)
{
    if (argc <= 1) return PrintUsage();

    const std::string command = argv[1];
    if (command == "--version") {
        std::cout << "BoxStudio 0.1.0-dev\n";
        return 0;
    }
    if (command == "--check-project") {
        if (argc < 3) {
            std::cerr << "--check-project requires a project root.\n";
            return 1;
        }
        return CheckProject(fs::path(argv[2]));
    }

    std::cerr << "Unknown command: " << command << "\n";
    return PrintUsage() == 0 ? 1 : 1;
}
