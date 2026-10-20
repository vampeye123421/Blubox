#include "GameClient.hpp"
#include "LaunchOptions.hpp"

#include <exception>
#include <iostream>
#include <utility>

int main(int argc, char *argv[]) {
    try {
        LaunchOptions options = ParseLaunchOptions(argc, argv);
        if (options.showHelp) {
            PrintUsage(argc > 0 ? argv[0] : nullptr);
            return 0;
        }

        GameClient client(std::move(options));
        return client.Run();
    } catch (const std::exception &error) {
        std::cerr << "Errore: " << error.what() << '\n';
        return 1;
    }
}
