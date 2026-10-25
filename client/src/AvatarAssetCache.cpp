#include "AvatarAssetCache.hpp"
#include "RaylibCompat.hpp"
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define DrawTextEx Win32DrawTextEx
#define LoadImage Win32LoadImage
#define PlaySound Win32PlaySound
#include <windows.h>
#include <winhttp.h>
#undef PlaySound
#undef LoadImage
#undef DrawTextEx
#undef DrawText
#undef ShowCursor
#undef CloseWindow
#undef Rectangle
#endif

namespace {
constexpr std::size_t kMaxTextureBytes = 5u * 1024u * 1024u;
constexpr std::size_t kMaxModelBytes = 30u * 1024u * 1024u;
constexpr std::string_view kAssetPath = "/storage/v1/object/public/marketplace-assets/";

#if defined(_WIN32)
std::wstring ToWide(std::string_view text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

bool DownloadAsset(std::string_view url, std::size_t maximum, std::vector<unsigned char>& output) {
    if (!url.starts_with("https://") || url.size() > 2048 || url.find(kAssetPath) == std::string_view::npos ||
        url.find_first_of("\\\r\n") != std::string_view::npos || url.find('@') != std::string_view::npos) return false;
    const std::wstring wide = ToWide(url);
    if (wide.empty()) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwHostNameLength == 0) return false;
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    std::wstring target = path;
    if (parts.dwExtraInfoLength) target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HINTERNET session = WinHttpOpen(L"BluboxClient/AvatarAssets", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    HINTERNET connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, statusSize = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) && status == 200) {
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available)) break;
                if (!available) { ok = !output.empty(); break; }
                if (output.size() + available > maximum) { output.clear(); break; }
                const auto old = output.size();
                output.resize(old + available);
                DWORD received = 0;
                if (!WinHttpReadData(request, output.data() + old, available, &received)) { output.clear(); break; }
                output.resize(old + received);
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}
#endif

Color ParseColor(std::string_view value) {
    if (value.size() != 7 || value[0] != '#') return Color{255, 255, 255, 255};
    unsigned int rgb = 0;
    for (std::size_t i = 1; i < value.size(); ++i) {
        const char c = value[i];
        const unsigned int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0;
        rgb = (rgb << 4) | digit;
    }
    return Color{static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8), static_cast<unsigned char>(rgb), 255};
}

void DrawOverlay(Texture2D texture, Rectangle crop, float centerX, float centerY,
                 float width, float height, float frontZ, Color tint) {
    if (!texture.id || texture.width <= 0 || texture.height <= 0) return;
    const float u0 = crop.x / static_cast<float>(texture.width);
    const float v0 = crop.y / static_cast<float>(texture.height);
    const float u1 = (crop.x + crop.width) / static_cast<float>(texture.width);
    const float v1 = (crop.y + crop.height) / static_cast<float>(texture.height);
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    rlNormal3f(0.0f, 0.0f, 1.0f);
    rlTexCoord2f(u0, v0); rlVertex3f(centerX - width * .5f, centerY + height * .5f, frontZ);
    rlTexCoord2f(u1, v0); rlVertex3f(centerX + width * .5f, centerY + height * .5f, frontZ);
    rlTexCoord2f(u1, v1); rlVertex3f(centerX + width * .5f, centerY - height * .5f, frontZ);
    rlTexCoord2f(u0, v1); rlVertex3f(centerX - width * .5f, centerY - height * .5f, frontZ);
    rlEnd();
    rlSetTexture(0);
}
} // namespace

AvatarAssetCache::~AvatarAssetCache() { Unload(); }

AvatarAssetCache::TextureEntry& AvatarAssetCache::texture(const std::string& url) {
    auto& entry = textures_[url];
    if (entry.attempted) return entry;
    entry.attempted = true;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxTextureBytes, bytes)) return entry;
    Image source = LoadImageFromMemory(".png", bytes.data(), static_cast<int>(bytes.size()));
    if (!source.data || source.width != 585 || source.height != 559) {
        if (source.data) UnloadImage(source);
        TraceLog(LOG_WARNING, "Avatar clothing asset was not a valid 585x559 PNG template.");
        return entry;
    }
    entry.texture = LoadTextureFromImage(source);
    UnloadImage(source);
#endif
    return entry;
}

AvatarAssetCache::ModelEntry& AvatarAssetCache::model(const std::string& url) {
    auto& entry = models_[url];
    if (entry.attempted) return entry;
    entry.attempted = true;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxModelBytes, bytes) || bytes.size() < 12 ||
        bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F') return entry;
    wchar_t tempRoot[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempRoot)) return entry;
    const auto key = std::hash<std::string>{}(url);
    const std::filesystem::path path = std::filesystem::path(tempRoot) /
        (L"blubox-avatar-" + std::to_wstring(key) + L".glb");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return entry;
    }
    entry.model = LoadModel(path.string().c_str());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
#endif
    return entry;
}

void AvatarAssetCache::Draw(const NetworkPlayerSnapshot& avatar) {
    if (!avatar.shirtAssetUrl.empty()) {
        const Texture2D shirtTexture = texture(avatar.shirtAssetUrl).texture;
        DrawOverlay(shirtTexture, Rectangle{231, 128, 116, 128}, 0.0f, 0.34f, 0.76f, 0.88f, 0.194f, ParseColor(avatar.shirtColor));
    }
    if (!avatar.pantsAssetUrl.empty()) {
        const Texture2D pantsTexture = texture(avatar.pantsAssetUrl).texture;
        DrawOverlay(pantsTexture, Rectangle{231, 256, 58, 128}, -0.20f, -0.39f, 0.31f, 0.72f, 0.164f, ParseColor(avatar.pantsColor));
        DrawOverlay(pantsTexture, Rectangle{347, 256, 58, 128}, 0.20f, -0.39f, 0.31f, 0.72f, 0.164f, ParseColor(avatar.pantsColor));
    }
    for (const auto& accessory : avatar.accessoryAssets) {
        Model& asset = model(accessory.assetUrl).model;
        if (!asset.meshCount) continue;
        Vector3 socket{0.0f, 1.30f, 0.0f};
        const auto& name = accessory.attachmentName;
        if (name.find("Back") != std::string::npos) socket = Vector3{0.0f, 0.34f, -0.30f};
        else if (name.find("Shoulder") != std::string::npos) socket = Vector3{name.find("Left") != std::string::npos ? -0.50f : 0.50f, 0.70f, 0.0f};
        else if (name.find("Waist") != std::string::npos) socket = Vector3{0.0f, -0.05f, 0.0f};
        DrawModel(asset, socket, 1.0f, ParseColor(accessory.color));
    }
}

void AvatarAssetCache::Unload() noexcept {
    for (auto& [_, entry] : textures_) if (entry.texture.id) UnloadTexture(entry.texture);
    for (auto& [_, entry] : models_) if (entry.model.meshCount) UnloadModel(entry.model);
    textures_.clear();
    models_.clear();
}
