#include "HackerSM64.h"

#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace boxstudio::decomps::sm64::hackersm64 {

static bool Has(const fs::path& root, const char* child)
{
    std::error_code ec;
    return fs::exists(root / child, ec);
}

bool ValidateWorkspace(const fs::path& root, std::string& reason, bool& built)
{
    built = false;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        reason = "Folder does not exist.";
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

std::string ModelSegmentRequirement(const std::string& model)
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

std::string UnsupportedModelReason(const std::string& model, const std::string& levelScript)
{
    const std::string required = ModelSegmentRequirement(model);
    if (required.empty() || levelScript.find(required) != std::string::npos) return {};

    if (model == "MODEL_KOOPA_WITH_SHELL") {
        return "Koopa needs actor group14 loaded in segment 0x06/0x0D.";
    }
    if (model == "MODEL_GOOMBA") {
        return "Goomba needs common0 loaded in segment 0x08/0x0F.";
    }
    return model + " needs " + required + " loaded by this level.";
}

} // namespace boxstudio::decomps::sm64::hackersm64
