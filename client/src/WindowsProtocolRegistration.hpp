#pragma once

#include <filesystem>

void RegisterBluboxProtocolForCurrentUser();
void RegisterBluboxProtocolForCurrentUser(const std::filesystem::path &launcherExecutable);
void UnregisterBluboxProtocolForCurrentUser();
void RegisterBluboxStudioProtocolForCurrentUser(const std::filesystem::path &studioExecutable);
void CreateBluboxStudioStartMenuShortcut(const std::filesystem::path &launcherExecutable);
