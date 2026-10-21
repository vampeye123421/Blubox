#pragma once

#include "ProtocolLaunchData.hpp"

#include <optional>
#include <string>

struct LaunchOptions {
    std::string mode = "guest";
    std::optional<ProtocolLaunchData> protocolLaunch;
    bool startFullscreen = false;
    bool launchStudio = false;
    bool showHelp = false;
    bool registerProtocol = false;
    bool unregisterProtocol = false;
};

LaunchOptions ParseLaunchOptions(int argc, char *argv[]);
void PrintUsage(const char *executableName);
