#pragma once

#include <map>
#include <optional>
#include <string>

// Parsed launch information stays available to the client without starting a
// network connection or persisting credentials.
struct ProtocolLaunchData {
    std::string rawUrl;
    std::string action;
    std::string path;
    std::optional<std::string> sessionToken;
    std::optional<std::string> worldId;
    std::map<std::string, std::string> queryParameters;
};

ProtocolLaunchData ParseBluboxUri(const std::string &url);
