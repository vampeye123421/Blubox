#include "Diagnostics.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace blubox::studio {
namespace {

std::mutex g_logMutex;
bool g_enabled = false;

std::filesystem::path DiagnosticsDirectory() {
#ifdef _WIN32
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr && *localAppData != '\0') {
        return std::filesystem::path(localAppData) / "BluboxStudio";
    }
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Logs" / "BluboxStudio";
    }
#endif
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

void AttachDiagnosticConsole(bool enabled) {
    if (!enabled) return;
#ifdef _WIN32
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED) {
        (void)AllocConsole();
    }
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
#endif
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
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
#ifdef _WIN32
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif
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
