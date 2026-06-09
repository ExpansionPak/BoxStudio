#pragma once

#include <filesystem>
#include <string>

namespace boxstudio::decomps::oot::hackerooot {

bool ValidateWorkspace(const std::filesystem::path& root, std::string& reason, bool& built);

} // namespace boxstudio::decomps::oot::hackerooot
