#include "HackerOoT.h"

#include <sstream>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace boxstudio::decomps::oot::hackerooot {

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
    for (const char* required : { "Makefile", "src", "assets", "baseroms", "include/config" }) {
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

    reason = built ? "Valid built HackerOoT workspace detected." : "Valid HackerOoT workspace detected. Build folder not found yet.";
    return true;
}

} // namespace boxstudio::decomps::oot::hackerooot