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
        } else if (argument == "--mode=guest") {
            options.mode = "guest";
        } else if (argument == "--mode") {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).starts_with("--")) {
                throw std::invalid_argument("--mode richiede un valore (per esempio: --mode guest).");
            }
            options.mode = argv[++i];
            if (options.mode != "guest") {
                throw std::invalid_argument("Modalita' non supportata: " + options.mode + ". Per ora e' disponibile solo guest.");
            }
        } else if (HasBluboxScheme(argument)) {
            if (options.protocolLaunch.has_value()) {
                throw std::invalid_argument("E' possibile passare un solo URL blubox://.");
            }
            options.protocolLaunch = ParseBluboxUri(std::string(argument));
        } else {
            throw std::invalid_argument("Argomento non riconosciuto: " + std::string(argument));
        }
    }

    if (options.registerProtocol && options.unregisterProtocol) {
        throw std::invalid_argument("Scegliere --register-protocol oppure --unregister-protocol.");
    }

    return options;
}

void PrintUsage(const char *executableName) {
    const char *name = executableName != nullptr ? executableName : "blubox_client";
    // Keep help output simple and portable across terminals.
    std::printf("Uso: %s [--mode=guest] [--fullscreen] [blubox://...] [--help]\n", name);
    std::printf("     %s --register-protocol | --unregister-protocol\n", name);
    std::printf("La modalita' guest avvia la scena locale di test senza connessione a un server.\n");
    std::printf("Su Windows usa --register-protocol per registrare blubox:// per l'utente corrente.\n");
    std::printf("Controlli: WASD movimento, mouse visuale, TAB cattura/libera il mouse, F11 fullscreen, ESC esci.\n");
}
