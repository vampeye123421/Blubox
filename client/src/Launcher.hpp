#pragma once

#include "LaunchOptions.hpp"

#include <string_view>

int RunLauncher(const LaunchOptions &options, int argc, char *argv[]);
void LogLauncherDiagnostic(std::string_view message) noexcept;
