#pragma once

#include <map>
#include <optional>
#include <cstdint>
#include <string>

// Parsed launch information stays available to the client without starting a
// network connection or persisting credentials.
struct ProtocolLaunchData {
    std::string rawUrl;
    std::string action;
    std::string path;
    std::optional<std::string> sessionToken;
    std::optional<std::string> presenceToken;
    std::optional<std::string> worldId;
    // Optional local-development server port. When present, the client connects
    // only to loopback; join tokens can never be redirected to an arbitrary host.
    std::optional<std::uint16_t> localServerPort;
    std::map<std::string, std::string> queryParameters;
};

ProtocolLaunchData ParseBluboxUri(const std::string &url);
