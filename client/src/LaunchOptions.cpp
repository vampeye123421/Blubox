#include "LaunchOptions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <string_view>

namespace {

bool HasBluboxScheme(std::string_view value) {
    constexpr std::string_view scheme = "blubox://";
    if (value.size() < scheme.size()) {
        return false;
    }

    return std::equal(scheme.begin(), scheme.end(), value.begin(), [](char expected, char actual) {
        return expected == static_cast<char>(std::tolower(static_cast<unsigned char>(actual)));
    });
}

} // namespace

LaunchOptions ParseLaunchOptions(int argc, char *argv[]) {
    LaunchOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);

        if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else if (argument == "--fullscreen") {
            options.startFullscreen = true;
        } else if (argument == "--register-protocol") {
            options.registerProtocol = true;
        } else if (argument == "--unregister-protocol") {
            options.unregisterProtocol = true;
        } else if (argument == "--uninstall-client") {
            options.uninstallClient = true;
        } else if (argument == "--uninstall-studio") {
            options.uninstallStudio = true;
        } else if (argument == "--quiet") {
            // Registered as the quiet Add/Remove Programs command; uninstall has no prompts.
        } else if (argument == "--mode=guest") {
            options.mode = "guest";
        } else if (argument == "--mode") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--")) {
                throw std::invalid_argument("--mode requires a value (for example: --mode guest).");
            }
            options.mode = argv[++i];
            if (options.mode != "guest") {
                throw std::invalid_argument("Unsupported mode: " + options.mode + ". Guest is the only mode currently available.");
            }
        } else if (HasBluboxScheme(argument)) {
            if (options.protocolLaunch.has_value()) {
                throw std::invalid_argument("Only one blubox:// URL can be provided.");
            }
            options.protocolLaunch = ParseBluboxUri(std::string(argument));
        } else {
            throw std::invalid_argument("Unrecognized argument: " + std::string(argument));
        }
    }

    if (options.registerProtocol && options.unregisterProtocol) {
        throw std::invalid_argument("Choose either --register-protocol or --unregister-protocol.");
    }
    if ((options.uninstallClient && options.uninstallStudio) ||
        ((options.uninstallClient || options.uninstallStudio) &&
         (options.registerProtocol || options.unregisterProtocol || options.protocolLaunch.has_value()))) {
        throw std::invalid_argument("Uninstall options cannot be combined with other options.");
    }

    return options;
}

void PrintUsage(const char *executableName) {
    const char *name = executableName != nullptr ? executableName : "blubox_client";
    // Keep help output simple and portable across terminals.
    std::printf("Usage: %s [--mode=guest] [--fullscreen] [blubox://...] [--help]\n", name);
    std::printf("     %s --register-protocol | --unregister-protocol\n", name);
    std::printf("     %s --uninstall-client | --uninstall-studio\n", name);
    std::printf("Guest mode starts the local test scene without connecting to a server.\n");
    std::printf("On Windows, use --register-protocol to register blubox:// for the current user.\n");
    std::printf("Controls: WASD to move, mouse to look, TAB to capture/release the mouse, F11 for fullscreen, ESC to exit.\n");
}
