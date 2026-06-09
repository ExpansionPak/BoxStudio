#pragma once

#include <filesystem>
#include <string>

namespace boxstudio::decomps::pm::papermario_dx {

bool ValidateWorkspace(const std::filesystem::path& root, std::string& reason, bool& built);

} // namespace boxstudio::decomps::pm::papermario_dx
