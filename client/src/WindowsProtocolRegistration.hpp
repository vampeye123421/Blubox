#pragma once

#include <filesystem>

void RegisterBluboxProtocolForCurrentUser();
void UnregisterBluboxProtocolForCurrentUser();
void RegisterBluboxStudioProtocolForCurrentUser(const std::filesystem::path &studioExecutable);
void CreateBluboxStudioStartMenuShortcut(const std::filesystem::path &launcherExecutable);
