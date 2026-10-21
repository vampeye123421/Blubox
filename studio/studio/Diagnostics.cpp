#include "Diagnostics.hpp"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>

namespace blubox::studio {
namespace {

std::mutex g_logMutex;
bool g_enabled = false;

std::filesystem::path DiagnosticsDirectory() {
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr && *localAppData != '\0') {
        return std::filesystem::path(localAppData) / "BluboxStudio";
    }
    return std::filesystem::temp_directory_path() / "BluboxStudio";
}

bool DebugMarkerExists() {
    std::error_code error;
    return std::filesystem::exists(DiagnosticsDirectory() / "debug.enabled", error);
}

} // namespace

bool DebugConsoleRequested() {
    const char* value = std::getenv("BLUBOX_STUDIO_DEBUG");
    return value != nullptr && std::string_view(value) == "1";
}

std::filesystem::path DiagnosticLogPath() {
    return DiagnosticsDirectory() / "studio-debug.log";
}

void InitializeDiagnostics(bool enabled) {
    g_enabled = enabled || DebugMarkerExists();
    if (!g_enabled) return;
    std::error_code error;
    std::filesystem::create_directories(DiagnosticsDirectory(), error);
    LogDiagnostic("INFO", "Diagnostics enabled; session tokens are never logged.");
}

void LogDiagnostic(std::string_view level, std::string_view message) {
    if (!g_enabled) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    try {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &time);
        std::ofstream output(DiagnosticLogPath(), std::ios::binary | std::ios::app);
        if (!output) return;
        output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << " [" << level << "] ";
        for (const char ch : message) output.put(ch == '\r' || ch == '\n' ? ' ' : ch);
        output.put('\n');
    } catch (...) {
        // Diagnostics must never interfere with sign-in or editor startup.
    }
}

} // namespace blubox::studio
