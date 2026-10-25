#include "LaunchOptions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <charconv>
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
        } else if (argument == "--studio-playtest") {
            if (i + 5 >= argc) throw std::invalid_argument("--studio-playtest requires host, port, credential, map file, and display name.");
            options.studioPlaytest = true;
            options.studioHost = argv[++i];
            const std::string_view portText(argv[++i]); unsigned port = 0;
            const auto parsed = std::from_chars(portText.data(), portText.data()+portText.size(), port);
            if (parsed.ec != std::errc{} || parsed.ptr != portText.data()+portText.size() || port == 0 || port > 65535)
                throw std::invalid_argument("Studio playtest port is invalid.");
            options.studioPort = static_cast<std::uint16_t>(port);
            options.studioCredential = argv[++i]; options.studioMapFile = argv[++i]; options.studioDisplayName = argv[++i];
            if (options.studioHost != "127.0.0.1" || options.studioCredential.size() != 64 || options.studioMapFile.empty())
                throw std::invalid_argument("Studio playtest arguments are invalid.");
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
    if (options.studioPlaytest && (options.protocolLaunch || options.uninstallClient || options.uninstallStudio))
        throw std::invalid_argument("Studio playtest cannot be combined with other launch actions.");
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
    std::printf("Controls: WASD to move, mouse to look, TAB to capture/release the mouse, ESC to release the mouse, F11 for fullscreen.\n");
}
