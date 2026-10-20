#pragma once

#include "ProtocolLaunchData.hpp"

#include <string>
#include <string_view>
#include <vector>

// Per-process session state. The token is kept only in memory and overwritten
// before its backing buffer is released.
class ClientSession {
public:
    explicit ClientSession(ProtocolLaunchData *launchData = nullptr);
    ~ClientSession();

    ClientSession(const ClientSession &) = delete;
    ClientSession &operator=(const ClientSession &) = delete;

    bool HasToken() const noexcept { return !token_.empty(); }
    std::string_view Token() const noexcept {
        return token_.empty() ? std::string_view{} : std::string_view(token_.data(), token_.size());
    }
    bool HasWorldId() const noexcept { return !worldId_.empty(); }
    const std::string &WorldId() const noexcept { return worldId_; }

private:
    std::vector<char> token_;
    std::string worldId_;
};
