#include "GameClient.hpp"
#include "LaunchOptions.hpp"

#include <exception>
#include <iostream>
#include <utility>

#ifdef _WIN32
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
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "Blubox Client", MB_OK | MB_ICONERROR);
#else
        std::cerr << "Error: " << error.what() << '\n';
#endif
        return 1;
    }
}
