#pragma once

#include <filesystem>
#include <string>

namespace boxstudio::decomps::sm64::hackersm64 {

bool ValidateWorkspace(const std::filesystem::path& root, std::string& reason, bool& built);
std::string ModelSegmentRequirement(const std::string& model);
std::string UnsupportedModelReason(const std::string& model, const std::string& levelScript);

} // namespace boxstudio::decomps::sm64::hackersm64
