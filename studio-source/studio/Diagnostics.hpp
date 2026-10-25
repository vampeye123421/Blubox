#pragma once

#include <filesystem>
#include <string_view>

namespace blubox::studio {

bool DebugConsoleRequested();
void AttachDiagnosticConsole(bool enabled);
void InitializeDiagnostics(bool enabled);
void LogDiagnostic(std::string_view level, std::string_view message);
std::filesystem::path DiagnosticLogPath();

} // namespace blubox::studio
