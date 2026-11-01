#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace blubox::platform {

void ShowNativeErrorDialog(std::string_view title, std::string_view message);
std::optional<std::filesystem::path> ShowNativeFileDialog(
    bool save, std::string_view title, const std::vector<std::string_view>& extensions);

} // namespace blubox::platform
