#include "Launcher.hpp"
#include "LaunchOptions.hpp"
#include "WindowsProtocolRegistration.hpp"

#include <exception>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <cstdlib>
#define WIN32_LEAN_AND_MEAN
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#include <windows.h>
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

#ifdef _WIN32
namespace {
void EnableUpdaterDebugConsole() {
    const char *enabled = std::getenv("BLUBOX_UPDATER_DEBUG");
    if (enabled == nullptr || std::string_view(enabled) != "1") return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED) {
        (void)AllocConsole();
    }
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);
}
} // namespace
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    EnableUpdaterDebugConsole();
#endif
    LogLauncherDiagnostic("Launcher process entered main().");
    try {
        LaunchOptions options = ParseLaunchOptions(argc, argv);
#ifdef BLUBOX_STUDIO_LAUNCHER
        options.launchStudio = true;
#endif
        if (options.showHelp) {
            PrintUsage(argc > 0 ? argv[0] : nullptr);
            return 0;
        }
        if (options.uninstallClient || options.uninstallStudio) {
#ifdef _WIN32
            const wchar_t *localAppData = _wgetenv(L"LOCALAPPDATA");
            if (localAppData == nullptr || *localAppData == L'\0') {
                throw std::runtime_error("LOCALAPPDATA is not defined.");
            }
            UninstallBluboxComponent(std::filesystem::path(localAppData) / L"Blubox",
                                     options.uninstallStudio);
            std::cout << (options.uninstallStudio ? "Blubox Studio" : "Blubox")
                      << " has been removed.\n";
            return 0;
#else
            throw std::runtime_error("Uninstallation is available only on Windows.");
#endif
        }
        if (options.registerProtocol) {
            RegisterBluboxProtocolForCurrentUser();
            std::cout << "Registered blubox:// for the current user.\n";
            return 0;
        }
        if (options.unregisterProtocol) {
            UnregisterBluboxProtocolForCurrentUser();
            std::cout << "Removed the blubox:// registration for the current user.\n";
            return 0;
        }
        return RunLauncher(options, argc, argv);
    } catch (const std::exception &error) {
        LogLauncherDiagnostic(std::string("fatal: ") + error.what());
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "Blubox Launcher", MB_OK | MB_ICONERROR);
#else
        std::cerr << "Launcher error: " << error.what() << '\n';
#endif
        return 1;
    }
}
