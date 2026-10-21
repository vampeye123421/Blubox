#include "Launcher.hpp"
#include "LaunchOptions.hpp"
#include "WindowsProtocolRegistration.hpp"

#include <exception>
#include <iostream>

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
        if (options.registerProtocol) {
            RegisterBluboxProtocolForCurrentUser();
            std::cout << "Protocollo blubox:// registrato per l'utente corrente.\n";
            return 0;
        }
        if (options.unregisterProtocol) {
            UnregisterBluboxProtocolForCurrentUser();
            std::cout << "Registrazione blubox:// rimossa per l'utente corrente.\n";
            return 0;
        }
        return RunLauncher(options, argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "Errore launcher: " << error.what() << '\n';
        return 1;
    }
}
