#include "NetworkClient.hpp"

#include <raylib.h>

#if defined(_WIN32)
// ENet includes winsock2.h -> windows.h. Rename the handful of Win32 API
// identifiers that collide with Raylib while those system headers are parsed.
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#define LoadImage Win32LoadImage
#define PlaySound Win32PlaySound
#endif
#include <enet/enet.h>
#include <nlohmann/json.hpp>
#if defined(_WIN32)
#undef PlaySound
#undef LoadImage
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

#include <cstdint>
#include <algorithm>
#include <bit>
#include <cmath>
#include <chrono>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The public hostname is a routing alias, not a way to hide the node's IP:
// DNS and the UDP connection still reveal the resolved destination.
constexpr char kServerHost[] = "blubox-node1.duckdns.org";
constexpr enet_uint16 kServerPort = 7777;
constexpr std::size_t kMaxTokenBytes = 8192;
constexpr std::size_t kMaxWorldIdBytes = 1024;
constexpr enet_uint8 kJoinHandshakeMessage = 0x01;
constexpr enet_uint8 kClientPositionMessage = 0x02;
constexpr enet_uint8 kJoinAcceptedMessage = 0x81;
constexpr enet_uint8 kWorldSnapshotMessage = 0x82;
constexpr enet_uint8 kProtocolVersion = 5;
constexpr std::size_t kMaxUserIdBytes = 256;
constexpr std::size_t kMaxDisplayNameBytes = 64;
constexpr std::size_t kMaxSnapshotPlayers = 128;
constexpr std::size_t kMaxAvatarDescriptionBytes = 8192;

bool ReadAvatar(std::string_view payload, NetworkPlayerSnapshot& player) {
    if (payload.empty() || payload.size() > kMaxAvatarDescriptionBytes) return false;
    try {
        const auto value = nlohmann::json::parse(payload);
        if (!value.is_object() || value.value("version", 0) != 1) return false;
        player.shirtId = value.value("shirtId", player.shirtId);
        player.pantsId = value.value("pantsId", player.pantsId);
        player.faceId = value.value("faceId", player.faceId);
        if (player.shirtId.empty() || player.shirtId.size() > 64 ||
            player.pantsId.empty() || player.pantsId.size() > 64) return false;
        const auto readColor = [&](const char* key, std::string& output) {
            const std::string color = value.value(key, output);
            if (color.size() != 7 || color[0] != '#') return false;
            for (std::size_t i = 1; i < color.size(); ++i)
                if (!std::isxdigit(static_cast<unsigned char>(color[i]))) return false;
            output = color;
            return true;
        };
        if (!readColor("bodyColor", player.bodyColor) || !readColor("shirtColor", player.shirtColor) ||
            !readColor("pantsColor", player.pantsColor)) return false;
        const auto readAssetUrl = [&](const char* key, std::string& output) {
            if (!value.contains(key) || value[key].is_null()) { output.clear(); return true; }
            if (!value[key].is_string()) return false;
            const std::string candidate = value[key].get<std::string>();
            constexpr std::string_view prefix = "https://";
            constexpr std::string_view path = "/storage/v1/object/public/marketplace-assets/";
            if (candidate.size() > 2048 || !candidate.starts_with(prefix) ||
                candidate.find(path) == std::string::npos || candidate.find_first_of("\\\\\r\n") != std::string::npos ||
                candidate.find('@') != std::string::npos) return false;
            output = candidate;
            return true;
        };
        if (!readAssetUrl("shirtAssetUrl", player.shirtAssetUrl) ||
            !readAssetUrl("pantsAssetUrl", player.pantsAssetUrl) ||
            !readAssetUrl("faceAssetUrl", player.faceAssetUrl)) return false;
        const auto accessories = value.value("accessories", nlohmann::json::array());
        if (!accessories.is_array() || accessories.size() > 32) return false;
        for (const auto& item : accessories) {
            if (!item.is_string()) return false;
            const std::string id = item.get<std::string>();
            if (id.empty() || id.size() > 64 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-';
            })) return false;
            player.accessories.push_back(id);
        }
        const auto assets = value.value("accessoryAssets", nlohmann::json::array());
        if (!assets.is_array() || assets.size() > 32) return false;
        for (const auto& item : assets) {
            if (!item.is_object() || !item.contains("assetUrl") || !item["assetUrl"].is_string() ||
                !item.contains("attachmentName") || !item["attachmentName"].is_string()) return false;
            NetworkPlayerSnapshot::AccessoryAsset asset;
            asset.id = item.value("id", std::string{});
            asset.assetUrl = item["assetUrl"].get<std::string>();
            asset.attachmentName = item["attachmentName"].get<std::string>();
            asset.clientStyle = item.value("clientStyle", std::string{});
            asset.color = item.value("color", std::string{"#4c8dff"});
            asset.scale = item.value("scale", 1.0f);
            asset.offsetX = item.value("offsetX", 0.0f);
            asset.offsetY = item.value("offsetY", 0.0f);
            asset.offsetZ = item.value("offsetZ", 0.0f);
            constexpr std::string_view path = "/storage/v1/object/public/marketplace-assets/";
            if (asset.id.size() > 64 || asset.assetUrl.size() > 2048 ||
                !asset.assetUrl.starts_with("https://") || asset.assetUrl.find(path) == std::string::npos ||
                asset.assetUrl.find_first_of("\\\\\r\n") != std::string::npos ||
                asset.attachmentName.empty() || asset.attachmentName.size() > 80 ||
                !std::all_of(asset.attachmentName.begin(), asset.attachmentName.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '_' || c == '.';
                }) || asset.color.size() != 7 || asset.color.front() != '#' ||
                !std::all_of(asset.color.begin() + 1, asset.color.end(), [](unsigned char c) {
                    return std::isxdigit(c) != 0;
                }) || !std::isfinite(asset.scale) || asset.scale < 0.05f || asset.scale > 5.0f ||
                !std::isfinite(asset.offsetX) || std::abs(asset.offsetX) > 3.0f ||
                !std::isfinite(asset.offsetY) || std::abs(asset.offsetY) > 3.0f ||
                !std::isfinite(asset.offsetZ) || std::abs(asset.offsetZ) > 3.0f) return false;
            if (item.contains("textureAssetUrl") && !item["textureAssetUrl"].is_null()) {
                if (!item["textureAssetUrl"].is_string()) return false;
                asset.textureAssetUrl = item["textureAssetUrl"].get<std::string>();
                if (asset.textureAssetUrl.size() > 2048 || !asset.textureAssetUrl.starts_with("https://") ||
                    asset.textureAssetUrl.find(path) == std::string::npos ||
                    asset.textureAssetUrl.find_first_of("\\\\\r\n") != std::string::npos ||
                    asset.textureAssetUrl.find('@') != std::string::npos) return false;
            }
            player.accessoryAssets.push_back(std::move(asset));
        }
        return true;
    } catch (...) { return false; }
}

void AppendU16(std::vector<enet_uint8> &buffer, std::size_t value) {
    const auto value16 = static_cast<enet_uint16>(value);
    buffer.push_back(static_cast<enet_uint8>((value16 >> 8) & 0xff));
    buffer.push_back(static_cast<enet_uint8>(value16 & 0xff));
}

void AppendLe16(std::vector<enet_uint8>& out, std::uint16_t value) { out.push_back(static_cast<enet_uint8>(value)); out.push_back(static_cast<enet_uint8>(value >> 8)); }
void AppendLe32(std::vector<enet_uint8>& out, std::uint32_t value) { for (int i=0;i<4;++i) out.push_back(static_cast<enet_uint8>(value >> (i*8))); }
void AppendLe64(std::vector<enet_uint8>& out, std::uint64_t value) { for (int i=0;i<8;++i) out.push_back(static_cast<enet_uint8>(value >> (i*8))); }
std::uint16_t ReadLe16(const enet_uint8* p) { return static_cast<std::uint16_t>(p[0] | (std::uint16_t(p[1])<<8)); }
std::uint32_t ReadLe32(const enet_uint8* p) { std::uint32_t v=0; for(int i=0;i<4;++i)v|=std::uint32_t(p[i])<<(i*8); return v; }
std::uint64_t ReadLe64(const enet_uint8* p) { std::uint64_t v=0; for(int i=0;i<8;++i)v|=std::uint64_t(p[i])<<(i*8); return v; }

std::uint16_t ReadU16(const enet_uint8 *data) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8) | data[1]);
}

std::uint64_t ReadU64(const enet_uint8 *data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | data[i];
    return value;
}

float ReadFloat(const enet_uint8 *data) {
    const std::uint32_t bits = (static_cast<std::uint32_t>(data[0]) << 24) |
        (static_cast<std::uint32_t>(data[1]) << 16) |
        (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
    return std::bit_cast<float>(bits);
}

void AppendFloat(std::vector<enet_uint8> &buffer, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    buffer.push_back(static_cast<enet_uint8>(bits >> 24));
    buffer.push_back(static_cast<enet_uint8>((bits >> 16) & 0xff));
    buffer.push_back(static_cast<enet_uint8>((bits >> 8) & 0xff));
    buffer.push_back(static_cast<enet_uint8>(bits & 0xff));
}

void SecureErase(std::vector<enet_uint8> &buffer) noexcept {
    volatile enet_uint8 *bytes = buffer.empty() ? nullptr : buffer.data();
    for (std::size_t i = 0; i < buffer.size(); ++i) bytes[i] = 0;
    buffer.clear();
}

} // namespace

NetworkClient::~NetworkClient() {
    Shutdown();
}

bool NetworkClient::Connect(const ClientSession &session) {
    Shutdown();
    disconnectKind_ = DisconnectKind::None;
    if (!session.HasToken()) {
        TraceLog(LOG_WARNING, "ENet not started: the join session token is missing.");
        disconnectKind_ = DisconnectKind::JoinRejected;
        return false;
    }
    if (!session.HasWorldId()) {
        TraceLog(LOG_WARNING, "ENet not started: the join session worldId is missing.");
        disconnectKind_ = DisconnectKind::JoinRejected;
        return false;
    }

    if (enet_initialize() != 0) {
        TraceLog(LOG_ERROR, "ENet initialization failed; continuing in local mode.");
        disconnectKind_ = DisconnectKind::ConnectionFailed;
        return false;
    }
    enetInitialized_ = true;
    session_ = &session;

    host_ = enet_host_create(nullptr, 1, 1, 0, 0);
    if (host_ == nullptr) {
        TraceLog(LOG_ERROR, "Could not create the ENet host; continuing in local mode.");
        Shutdown();
        disconnectKind_ = DisconnectKind::ConnectionFailed;
        return false;
    }

    ENetAddress address{};
    const char *serverHost = kServerHost;
    if (enet_address_set_host(&address, serverHost) != 0) {
        TraceLog(LOG_ERROR, "DNS lookup failed for the Blubox server.");
        Shutdown();
        disconnectKind_ = DisconnectKind::ConnectionFailed;
        return false;
    }
    address.port = kServerPort;

    peer_ = enet_host_connect(host_, &address, 1, 0);
    if (peer_ == nullptr) {
        TraceLog(LOG_ERROR, "Could not start the ENet connection; continuing in local mode.");
        Shutdown();
        disconnectKind_ = DisconnectKind::ConnectionFailed;
        return false;
    }

    TraceLog(LOG_INFO, "Started ENet connection to %s:%u.", serverHost,
             static_cast<unsigned int>(address.port));
    return true;
}

void NetworkClient::Poll() {
    if (host_ == nullptr) return;

    ENetEvent event{};
    int result = 0;
    while ((result = enet_host_service(host_, &event, 0)) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            enet_peer_timeout(event.peer, 32, 15000, 60000);
            TraceLog(LOG_INFO, "ENet connection established; sending join handshake.");
            if (!SendJoinHandshake()) {
                TraceLog(LOG_ERROR, "Could not send the ENet handshake.");
                enet_peer_disconnect(event.peer, 0);
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            TraceLog(LOG_INFO, "Received ENet packet (%zu bytes).", event.packet->dataLength);
            HandlePacket(event.packet->data, event.packet->dataLength);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            TraceLog(LOG_WARNING, "ENet connection closed by server (code %u).", event.data);
            peer_ = nullptr;
            presenceReporter_.Stop();
            authenticated_ = false;
            sessionId_ = 0;
            userVerified_ = false;
            userId_.clear();
            players_.clear();
            disconnectKind_ = event.data == 1 ? DisconnectKind::Kicked
                : event.data == 2 ? DisconnectKind::JoinRejected
                                  : DisconnectKind::ConnectionLost;
            TraceLog(LOG_ERROR, "Network session ended; client left the server session (reason code %u).",
                     event.data);
            break;
        case ENET_EVENT_TYPE_NONE:
        default:
            break;
        }
    }

    if (result < 0) {
        TraceLog(LOG_ERROR, "Error while servicing ENet events.");
        peer_ = nullptr;
        presenceReporter_.Stop();
        authenticated_ = false;
        sessionId_ = 0;
        userVerified_ = false;
        userId_.clear();
        players_.clear();
        disconnectKind_ = DisconnectKind::ConnectionLost;
    }
}

void NetworkClient::HandlePacket(const unsigned char *data, std::size_t size) {
    if (data == nullptr || size < 2) return;
    const auto *bytes = reinterpret_cast<const enet_uint8 *>(data);
    if (bytes[1] != kProtocolVersion) {
        TraceLog(LOG_WARNING, "ENet packet uses an unsupported protocol version.");
        return;
    }

    if (bytes[0] == kJoinAcceptedMessage) {
        if (size < 15) return;
        sessionId_ = ReadU64(bytes + 2);
        const std::size_t idLength = ReadU16(bytes + 10);
        const std::size_t avatarLength = ReadU16(bytes + 13);
        if (sessionId_ == 0 || idLength == 0 || idLength > kMaxUserIdBytes ||
            avatarLength == 0 || avatarLength > kMaxAvatarDescriptionBytes ||
            size != 15 + idLength + avatarLength || bytes[12] > 1) return;
        userVerified_ = bytes[12] != 0;
        userId_.assign(reinterpret_cast<const char *>(bytes + 15), idLength);
        localAvatar_ = {};
        localAvatar_.userId = userId_;
        localAvatar_.isVerified = userVerified_;
        if (!ReadAvatar(std::string_view(reinterpret_cast<const char *>(bytes + 15 + idLength), avatarLength), localAvatar_)) return;
        if (localAvatar_.faceId != "face-default" && localAvatar_.faceAssetUrl.empty())
            TraceLog(LOG_WARNING, "Marketplace face %s was selected but the join token included no face image URL.",
                     localAvatar_.faceId.c_str());
        authenticated_ = true;
        disconnectKind_ = DisconnectKind::None;
        if (session_ != nullptr) {
            presenceReporter_.Start(std::string(session_->PresenceToken()));
        }
        TraceLog(LOG_INFO, "Handshake authenticated for player %s.", userId_.c_str());
        TraceLog(LOG_INFO, "Verified badge status: %s.", userVerified_ ? "enabled" : "not verified");
        return;
    }

    if (bytes[0] != kWorldSnapshotMessage || size < 4 || !authenticated_) return;
    const std::size_t count = ReadU16(bytes + 2);
    if (count > kMaxSnapshotPlayers) {
        TraceLog(LOG_WARNING, "ENet snapshot rejected: too many players.");
        return;
    }

    std::vector<NetworkPlayerSnapshot> nextPlayers;
    nextPlayers.reserve(count);
    std::size_t offset = 4;
    for (std::size_t index = 0; index < count; ++index) {
        if (size - offset < 13) return;
        const std::uint64_t sessionId = ReadU64(bytes + offset);
        const std::size_t idLength = ReadU16(bytes + offset + 8);
        const std::size_t nameLength = ReadU16(bytes + offset + 10);
        const enet_uint8 verifiedByte = bytes[offset + 12];
        offset += 13;
        if (sessionId == 0 || idLength == 0 || idLength > kMaxUserIdBytes || nameLength == 0 ||
            nameLength > kMaxDisplayNameBytes || verifiedByte > 1 ||
            size - offset < idLength + nameLength + 14) return;

        NetworkPlayerSnapshot player;
        player.sessionId = sessionId;
        player.userId.assign(reinterpret_cast<const char *>(bytes + offset), idLength);
        offset += idLength;
        player.displayName.assign(reinterpret_cast<const char *>(bytes + offset), nameLength);
        offset += nameLength;
        player.isVerified = verifiedByte != 0;
        player.x = ReadFloat(bytes + offset);
        player.y = ReadFloat(bytes + offset + 4);
        player.z = ReadFloat(bytes + offset + 8);
        player.yaw = ReadFloat(bytes + offset + 12);
        offset += 16;
        if (!std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z) || !std::isfinite(player.yaw)) return;
        const std::size_t avatarLength = ReadU16(bytes + offset);
        offset += 2;
        if (avatarLength == 0 || avatarLength > kMaxAvatarDescriptionBytes || size - offset < avatarLength ||
            !ReadAvatar(std::string_view(reinterpret_cast<const char *>(bytes + offset), avatarLength), player)) return;
        offset += avatarLength;
        if (player.sessionId == sessionId_) localAvatar_ = player;
        nextPlayers.push_back(std::move(player));
    }
    if (offset != size) return;
    players_ = std::move(nextPlayers);
}

void NetworkClient::SendPlayerPosition(float x, float y, float z, float yaw) {
    if (peer_ == nullptr || host_ == nullptr || !authenticated_ ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw)) return;
    const double now = GetTime();
    if (now < nextPositionSendTime_) return;
    nextPositionSendTime_ = now + 0.05; // 20 Hz client input; server snapshots remain 60 Hz.

    std::vector<enet_uint8> payload{kClientPositionMessage, kProtocolVersion};
    AppendFloat(payload, x);
    AppendFloat(payload, y);
    AppendFloat(payload, z);
    AppendFloat(payload, yaw);
    ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), 0);
    if (packet == nullptr || enet_peer_send(peer_, 0, packet) != 0) {
        if (packet != nullptr) enet_packet_destroy(packet);
        return;
    }
    enet_host_flush(host_);
}

bool NetworkClient::SendJoinHandshake() {
    if (peer_ == nullptr || session_ == nullptr) return false;

    const std::string_view token = session_->Token();
    const std::string &worldId = session_->WorldId();
    if (token.empty() || worldId.empty() || token.size() > kMaxTokenBytes ||
        worldId.size() > kMaxWorldIdBytes) {
        TraceLog(LOG_ERROR, "Handshake rejected: credentials are missing or too long.");
        return false;
    }

    // Wire format: type:u8, protocolVersion:u8, tokenLength:u16be,
    // worldIdLength:u16be, token bytes, worldId bytes.
    std::vector<enet_uint8> payload;
    payload.reserve(6 + token.size() + worldId.size());
    payload.push_back(kJoinHandshakeMessage);
    payload.push_back(kProtocolVersion);
    AppendU16(payload, token.size());
    AppendU16(payload, worldId.size());
    payload.insert(payload.end(), token.begin(), token.end());
    payload.insert(payload.end(), worldId.begin(), worldId.end());

    ENetPacket *packet = enet_packet_create(payload.data(), payload.size(), ENET_PACKET_FLAG_RELIABLE);
    SecureErase(payload);
    if (packet == nullptr) return false;
    if (enet_peer_send(peer_, 0, packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }
    enet_host_flush(host_);
    return true;
}

void NetworkClient::Shutdown() noexcept {
    presenceReporter_.Stop();
    if (host_ != nullptr) {
        if (peer_ != nullptr && peer_->state == ENET_PEER_STATE_CONNECTED) {
            enet_peer_disconnect(peer_, 0);
            enet_host_flush(host_);

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(350);
            ENetEvent event{};
            while (std::chrono::steady_clock::now() < deadline) {
                const int result = enet_host_service(host_, &event, 10);
                if (result > 0 && event.type == ENET_EVENT_TYPE_DISCONNECT) break;
                if (result > 0 && event.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(event.packet);
                }
                if (result < 0) break;
            }
        }
        enet_host_destroy(host_);
        host_ = nullptr;
        peer_ = nullptr;
    }
    session_ = nullptr;
    userId_.clear();
    sessionId_ = 0;
    userVerified_ = false;
    players_.clear();
    authenticated_ = false;
    disconnectKind_ = DisconnectKind::None;
    if (enetInitialized_) {
        enet_deinitialize();
        enetInitialized_ = false;
    }
}
