#pragma once

#include "ClientSession.hpp"
#include "PresenceReporter.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Keep ENet's Windows headers out of public headers: they pull in windows.h,
// whose GDI/User symbols collide with Raylib names such as Rectangle/DrawText.
struct _ENetHost;
struct _ENetPeer;

struct NetworkPlayerSnapshot {
    std::uint64_t sessionId = 0;
    std::string userId;
    std::string displayName;
    bool isVerified = false;
    std::string bodyColor = "#dca77b";
    std::string shirtId = "shirt-cyan-hoodie";
    std::string shirtColor = "#22b8cf";
    std::string pantsId = "pants-dark-denim";
    std::string pantsColor = "#334b78";
    std::vector<std::string> accessories;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class DisconnectKind {
    None,
    ConnectionLost,
    Kicked,
    JoinRejected,
    ConnectionFailed
};

class NetworkClient {
public:
    NetworkClient() = default;
    ~NetworkClient();

    NetworkClient(const NetworkClient &) = delete;
    NetworkClient &operator=(const NetworkClient &) = delete;

    // Starts an asynchronous connection when session credentials are present.
    bool Connect(const ClientSession &session);
    // Poll with timeout zero so the game loop never waits on the network.
    void Poll();
    void SendPlayerPosition(float x, float y, float z);

    const std::vector<NetworkPlayerSnapshot> &Players() const noexcept { return players_; }
    const std::string &UserId() const noexcept { return userId_; }
    const NetworkPlayerSnapshot &LocalAvatar() const noexcept { return localAvatar_; }
    std::uint64_t SessionId() const noexcept { return sessionId_; }
    bool UserVerified() const noexcept { return userVerified_; }
    DisconnectKind DisconnectReason() const noexcept { return disconnectKind_; }
    bool HasDisconnectNotice() const noexcept { return disconnectKind_ != DisconnectKind::None; }

private:
    bool SendJoinHandshake();
    void HandlePacket(const unsigned char *data, std::size_t size);
    void Shutdown() noexcept;

    _ENetHost *host_ = nullptr;
    _ENetPeer *peer_ = nullptr;
    const ClientSession *session_ = nullptr;
    std::string userId_;
    std::uint64_t sessionId_ = 0;
    bool userVerified_ = false;
    PresenceReporter presenceReporter_;
    std::vector<NetworkPlayerSnapshot> players_;
    NetworkPlayerSnapshot localAvatar_{};
    double nextPositionSendTime_ = 0.0;
    bool authenticated_ = false;
    bool enetInitialized_ = false;
    DisconnectKind disconnectKind_ = DisconnectKind::None;
};
