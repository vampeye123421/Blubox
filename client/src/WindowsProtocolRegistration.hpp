#pragma once

#include <filesystem>
#include <string_view>

void RegisterBluboxProtocolForCurrentUser();
void RegisterBluboxProtocolForCurrentUser(const std::filesystem::path &launcherExecutable);
void UnregisterBluboxProtocolForCurrentUser();
void RegisterBluboxStudioProtocolForCurrentUser(const std::filesystem::path &studioExecutable);
void CreateBluboxStudioStartMenuShortcut(const std::filesystem::path &launcherExecutable);
void CreateBluboxClientShortcuts(const std::filesystem::path &launcherExecutable);
void CreateBluboxStudioShortcuts(const std::filesystem::path &launcherExecutable);
void RegisterBluboxClientUninstaller(const std::filesystem::path &launcherExecutable,
                                     std::string_view version);
void RegisterBluboxStudioUninstaller(const std::filesystem::path &launcherExecutable,
                                     std::string_view version);
void UninstallBluboxComponent(const std::filesystem::path &installRoot, bool studio);
