#include "Launcher.hpp"
#include "LaunchOptions.hpp"
#include "WindowsProtocolRegistration.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <cstdlib>
#endif

int main(int argc, char *argv[]) {
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
        std::cerr << "Launcher error: " << error.what() << '\n';
        return 1;
    }
}
