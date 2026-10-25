#include "AvatarAssetCache.hpp"
#include "RaylibCompat.hpp"
#include "BluboxR6Character.hpp"
#include <nlohmann/json.hpp>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
constexpr float kAvatarTargetHeight = 2.24f;

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

struct AccessoryFit {
    Vector3 position{};
    Vector3 center{};
    Vector3 halfSize{};
    float scale = 0.0f;
};

AccessoryFit FitAccessory(const BoundingBox& bounds,
                         const NetworkPlayerSnapshot::AccessoryAsset& accessory) {
    const Vector3 size{bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y,
                       bounds.max.z - bounds.min.z};
    const float largest = std::max({size.x, size.y, size.z});
    if (!(largest > 0.00001f) || !std::isfinite(largest)) return {};
    const Vector3 center{(bounds.min.x + bounds.max.x) * 0.5f,
                         (bounds.min.y + bounds.max.y) * 0.5f,
                         (bounds.min.z + bounds.max.z) * 0.5f};

    std::string name = accessory.attachmentName;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const bool hat = name.ends_with("hatattachment") || name.ends_with("hairattachment");
    const bool back = name.find("back") != std::string::npos;
    const bool face = name.find("face") != std::string::npos;
    const bool shoulder = name.find("shoulder") != std::string::npos;
    const bool waist = name.find("waist") != std::string::npos;
    const float desiredSize = hat ? 0.50f : shoulder ? 0.40f : back ? 0.68f : waist ? 0.50f : face ? 0.32f : 0.44f;
    const float scale = std::min(desiredSize / largest, 1000.0f) *
        std::clamp(std::isfinite(accessory.scale) ? accessory.scale : 1.0f, 0.05f, 5.0f);
    Vector3 anchor{0.0f, 1.93f, face ? 0.34f : 0.31f};
    if (hat) anchor = Vector3{0.0f, 2.24f, 0.0f};
    else if (back) anchor = Vector3{0.0f, 1.22f, -0.34f};
    else if (shoulder) anchor = Vector3{name.find("left") != std::string::npos ? -0.58f : 0.58f, 1.28f, 0.0f};
    else if (waist) anchor = Vector3{0.0f, 0.10f, 0.0f};

    const Vector3 position{anchor.x - center.x * scale + accessory.offsetX,
        (hat ? anchor.y - bounds.min.y * scale : anchor.y - center.y * scale) + accessory.offsetY,
        anchor.z - center.z * scale + accessory.offsetZ};
    return {position,
        Vector3{position.x + center.x * scale, position.y + center.y * scale,
                position.z + center.z * scale},
        Vector3{size.x * scale * 0.5f, size.y * scale * 0.5f, size.z * scale * 0.5f},
        scale};
}

void DrawShadowQuad(Texture2D texture, Vector3 ground, Vector3 center,
                    float radiusX, float radiusZ, float yaw, unsigned char opacity) {
    if (!texture.id || radiusX <= 0.01f || radiusZ <= 0.01f) return;
    constexpr float lift = 0.018f;
    const float y = ground.y + lift;
    const float c = std::cos(yaw), s = std::sin(yaw);
    const auto vertex = [&](float x, float z, float u, float v) {
        rlTexCoord2f(u, v);
        rlVertex3f(center.x + x * c - z * s, y, center.z + x * s + z * c);
    };
    rlSetTexture(texture.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, opacity);
    rlNormal3f(0.0f, 1.0f, 0.0f);
    vertex(-radiusX, -radiusZ, 0.0f, 0.0f);
    vertex(radiusX, -radiusZ, 1.0f, 0.0f);
    vertex(radiusX, radiusZ, 1.0f, 1.0f);
    vertex(-radiusX, radiusZ, 0.0f, 1.0f);
    rlEnd();
    rlSetTexture(0);
}

Vector3 RotateAvatarLocal(Vector3 value, float yaw) {
    const float rotation = PI - yaw;
    const float c = std::cos(rotation), s = std::sin(rotation);
    return Vector3{value.x * c + value.z * s, value.y,
                   -value.x * s + value.z * c};
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

bool AvatarAssetCache::LoadBaseCharacter(const Shader& lightingShader) {
    if (baseCharacterLoaded_) return baseCharacter_.meshCount > 0;
    baseCharacterLoaded_ = true;
    lightingShader_ = lightingShader;
#if defined(_WIN32)
    if (blubox::kR6CharacterDataSize < 20 ||
        blubox::kR6CharacterData[0] != 'g' || blubox::kR6CharacterData[1] != 'l' ||
        blubox::kR6CharacterData[2] != 'T' || blubox::kR6CharacterData[3] != 'F') {
        TraceLog(LOG_WARNING, "Embedded Blubox R6 character asset is invalid.");
        return false;
    }

    nlohmann::json gltf;
    const std::uint32_t jsonLength =
        static_cast<std::uint32_t>(blubox::kR6CharacterData[12]) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[13]) << 8) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[14]) << 16) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[15]) << 24);
    const std::uint32_t jsonType =
        static_cast<std::uint32_t>(blubox::kR6CharacterData[16]) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[17]) << 8) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[18]) << 16) |
        (static_cast<std::uint32_t>(blubox::kR6CharacterData[19]) << 24);
    if (jsonType == 0x4E4F534Au && jsonLength <= static_cast<std::uint32_t>(blubox::kR6CharacterDataSize - 20)) {
        try {
            const auto* jsonBegin = reinterpret_cast<const char*>(blubox::kR6CharacterData + 20);
            gltf = nlohmann::json::parse(jsonBegin, jsonBegin + jsonLength);
        } catch (const std::exception&) {
            TraceLog(LOG_WARNING, "Embedded Blubox R6 character metadata could not be parsed.");
        }
    }

    std::error_code error;
    const auto tempRoot = std::filesystem::temp_directory_path(error);
    if (error) return false;
    const auto tempPath = tempRoot / ("blubox-r6-character-" +
        std::to_string(static_cast<unsigned long long>(GetCurrentProcessId())) + ".glb");
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output || !output.write(reinterpret_cast<const char*>(blubox::kR6CharacterData),
                                     blubox::kR6CharacterDataSize)) {
            std::filesystem::remove(tempPath, error);
            TraceLog(LOG_WARNING, "Could not stage embedded Blubox R6 character asset.");
            return false;
        }
    }
    baseCharacter_ = LoadModel(tempPath.string().c_str());
    std::filesystem::remove(tempPath, error);
    if (baseCharacter_.meshCount <= 0 || baseCharacter_.materialCount <= 0) {
        if (baseCharacter_.meshCount > 0) UnloadModel(baseCharacter_);
        baseCharacter_ = Model{};
        TraceLog(LOG_WARNING, "Raylib could not load the embedded Blubox R6 character.");
        return false;
    }

    if (gltf.is_object() && gltf.contains("materials") && gltf["materials"].is_array()) {
        for (std::size_t index = 0; index < gltf["materials"].size(); ++index) {
            const auto& material = gltf["materials"][index];
            if (!material.is_object() || !material.contains("name") || !material["name"].is_string()) continue;
            // Raylib reserves material slot 0 for its fallback material; glTF
            // material n is therefore stored in the model at slot n + 1.
            if (index + 1 < static_cast<std::size_t>(baseCharacter_.materialCount))
                baseCharacterMaterials_[material["name"].get<std::string>()] = static_cast<int>(index + 1);
        }
    }
    baseCharacterBounds_ = GetModelBoundingBox(baseCharacter_);
    const float height = baseCharacterBounds_.max.y - baseCharacterBounds_.min.y;
    if (!(height > 0.0001f) || !std::isfinite(height)) {
        UnloadModel(baseCharacter_);
        baseCharacter_ = Model{};
        baseCharacterMaterials_.clear();
        return false;
    }
    baseCharacterScale_ = kAvatarTargetHeight / height;
    if (lightingShader.id != 0) {
        for (int i = 0; i < baseCharacter_.materialCount; ++i)
            baseCharacter_.materials[i].shader = lightingShader;
    }
    TraceLog(LOG_INFO, "Loaded Blubox R6 character (%d meshes, %.2f units tall).",
             baseCharacter_.meshCount, height * baseCharacterScale_);
    return true;
#else
    (void)lightingShader;
    return false;
#endif
}

bool AvatarAssetCache::DrawBaseCharacter(Vector3 feetPosition,
                                         const NetworkPlayerSnapshot& avatar,
                                         float facingYaw) {
    if (!baseCharacter_.meshCount || baseCharacterScale_ <= 0.0f) return false;
    const Color skin = ParseColor(avatar.bodyColor);
    const Color shirt = ParseColor(avatar.shirtColor);
    const Color pants = ParseColor(avatar.pantsColor);
    for (const auto& [name, index] : baseCharacterMaterials_) {
        if (index < 0 || index >= baseCharacter_.materialCount) continue;
        Color tint = skin;
        if (name == "ShirtMaterial") tint = shirt;
        else if (name == "PantsMaterial") tint = pants;
        baseCharacter_.materials[index].maps[MATERIAL_MAP_DIFFUSE].color = tint;
    }

    rlPushMatrix();
    rlTranslatef(feetPosition.x, feetPosition.y, feetPosition.z);
    // The Blender rig's authored front is +Z; the game camera at yaw 0 faces -Z.
    rlRotatef((PI - facingYaw) * RAD2DEG, 0.0f, 1.0f, 0.0f);
    const Vector3 modelOrigin{
        -(baseCharacterBounds_.min.x + baseCharacterBounds_.max.x) * 0.5f * baseCharacterScale_,
        -baseCharacterBounds_.min.y * baseCharacterScale_,
        -(baseCharacterBounds_.min.z + baseCharacterBounds_.max.z) * 0.5f * baseCharacterScale_};
    DrawModelEx(baseCharacter_, modelOrigin, Vector3{0.0f, 1.0f, 0.0f}, 0.0f,
                Vector3{baseCharacterScale_, baseCharacterScale_, baseCharacterScale_}, WHITE);
    Draw(avatar, false);
    rlPopMatrix();
    return true;
}

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

AvatarAssetCache::TextureEntry& AvatarAssetCache::itemTexture(const std::string& url) {
    auto& entry = textures_[url];
    if (entry.attempted) return entry;
    entry.attempted = true;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxTextureBytes, bytes)) return entry;
    Image source = LoadImageFromMemory(".png", bytes.data(), static_cast<int>(bytes.size()));
    if (!source.data || source.width <= 0 || source.height <= 0) {
        if (source.data) UnloadImage(source);
        TraceLog(LOG_WARNING, "Avatar accessory texture was not a valid PNG.");
        return entry;
    }
    entry.texture = LoadTextureFromImage(source);
    UnloadImage(source);
#endif
    return entry;
}

AvatarAssetCache::ModelEntry& AvatarAssetCache::model(const std::string& url, const std::string& textureUrl) {
    const std::string modelKey = url + "\n" + textureUrl;
    auto& entry = models_[modelKey];
    if (entry.attempted) return entry;
    entry.attempted = true;
#if defined(_WIN32)
    std::vector<unsigned char> bytes;
    if (!DownloadAsset(url, kMaxModelBytes, bytes) || bytes.size() < 12 ||
        bytes[0] != 'g' || bytes[1] != 'l' || bytes[2] != 'T' || bytes[3] != 'F') return entry;
    wchar_t tempRoot[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempRoot)) return entry;
    const auto fileHash = std::hash<std::string>{}(modelKey);
    const std::filesystem::path path = std::filesystem::path(tempRoot) /
        (L"blubox-avatar-" + std::to_wstring(fileHash) + L".glb");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return entry;
    }
    entry.model = LoadModel(path.string().c_str());
    if (entry.model.meshCount > 0) {
        entry.bounds = GetModelBoundingBox(entry.model);
        // DrawModelEx selects the shader stored on each Model material. The
        // outer world BeginShaderMode does not override those material shaders,
        // so without this assignment uploaded accessories stayed unlit.
        if (lightingShader_.id != 0 && entry.model.materials) {
            for (int i = 0; i < entry.model.materialCount; ++i)
                entry.model.materials[i].shader = lightingShader_;
        }
    }
    if (entry.model.meshCount > 0 && !textureUrl.empty()) {
        const Texture2D itemMap = itemTexture(textureUrl).texture;
        if (itemMap.id) {
            for (int i = 0; i < entry.model.materialCount; ++i)
                SetMaterialTexture(&entry.model.materials[i], MATERIAL_MAP_DIFFUSE, itemMap);
            entry.hasExternalTexture = true;
        }
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
#endif
    return entry;
}

void AvatarAssetCache::Draw(const NetworkPlayerSnapshot& avatar, bool drawClothingOverlays) {
    if (drawClothingOverlays && !avatar.shirtAssetUrl.empty()) {
        const Texture2D shirtTexture = texture(avatar.shirtAssetUrl).texture;
        DrawOverlay(shirtTexture, Rectangle{231, 128, 116, 128}, 0.0f, 1.25f, 0.76f, 0.88f, 0.214f, ParseColor(avatar.shirtColor));
    }
    if (drawClothingOverlays && !avatar.pantsAssetUrl.empty()) {
        const Texture2D pantsTexture = texture(avatar.pantsAssetUrl).texture;
        DrawOverlay(pantsTexture, Rectangle{231, 256, 58, 128}, -0.23f, 0.43f, 0.31f, 0.72f, 0.171f, ParseColor(avatar.pantsColor));
        DrawOverlay(pantsTexture, Rectangle{347, 256, 58, 128}, 0.23f, 0.43f, 0.31f, 0.72f, 0.171f, ParseColor(avatar.pantsColor));
    }
    for (const auto& accessory : avatar.accessoryAssets) {
        ModelEntry& entry = model(accessory.assetUrl, accessory.textureAssetUrl);
        Model& asset = entry.model;
        if (!asset.meshCount) continue;

        // These sockets match AvatarPreview.fitAccessory() and its feet-origin
        // rig. Keep offsets in avatar-local space so the saved editor fit is
        // identical in the published client and follows the character yaw.
        const AccessoryFit fit = FitAccessory(entry.bounds, accessory);
        if (fit.scale <= 0.0f) continue;
        DrawModelEx(asset, fit.position, Vector3{0.0f, 1.0f, 0.0f}, 0.0f,
                    Vector3{fit.scale, fit.scale, fit.scale}, WHITE);
    }
}

void AvatarAssetCache::DrawGroundShadow(Vector3 groundPoint, Vector3 feetPosition,
                                        const NetworkPlayerSnapshot& avatar, float facingYaw,
                                        Vector3 sunToLight) {
    if (!shadowTexture_.id) {
        Image image = GenImageColor(64, 64, BLANK);
        if (!image.data) return;
        auto* pixels = static_cast<Color*>(image.data);
        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const float dx = (x + 0.5f - image.width * 0.5f) / (image.width * 0.5f);
                const float dy = (y + 0.5f - image.height * 0.5f) / (image.height * 0.5f);
                const float edge = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                const auto alpha = static_cast<unsigned char>(std::lround(190.0f * edge * edge));
                pixels[y * image.width + x] = Color{12, 17, 25, alpha};
            }
        }
        shadowTexture_ = LoadTextureFromImage(image);
        UnloadImage(image);
        if (shadowTexture_.id) SetTextureFilter(shadowTexture_, TEXTURE_FILTER_BILINEAR);
    }
    if (!shadowTexture_.id) return;

    const float lightY = std::max(0.2f, std::abs(sunToLight.y));
    const float direction = sunToLight.y >= 0.0f ? -1.0f : 1.0f;
    const float projectX = direction * sunToLight.x / lightY;
    const float projectZ = direction * sunToLight.z / lightY;
    const float bodyHeight = std::max(0.0f, feetPosition.y + 0.85f - groundPoint.y);
    Vector3 bodyCenter{feetPosition.x + projectX * bodyHeight * 0.45f,
                       groundPoint.y,
                       feetPosition.z + projectZ * bodyHeight * 0.45f};
    const float bodyYaw = PI - facingYaw;
    DrawShadowQuad(shadowTexture_, groundPoint, bodyCenter, 0.39f, 0.25f,
                   bodyYaw, 112);

    // Each fitted accessory gets its own soft projected footprint. Reuse the
    // exact fit transform used for rendering so the shadow follows saved size,
    // attachment offsets, and character facing.
    for (const auto& accessory : avatar.accessoryAssets) {
        const auto it = models_.find(accessory.assetUrl + "\n" + accessory.textureAssetUrl);
        if (it == models_.end() || it->second.model.meshCount <= 0) continue;
        const AccessoryFit fit = FitAccessory(it->second.bounds, accessory);
        if (fit.scale <= 0.0f) continue;
        const Vector3 localCenter = RotateAvatarLocal(fit.center, facingYaw);
        const float height = std::max(0.0f, feetPosition.y + localCenter.y - groundPoint.y);
        Vector3 projected{feetPosition.x + localCenter.x + projectX * height,
                          groundPoint.y,
                          feetPosition.z + localCenter.z + projectZ * height};
        const Vector3 half = fit.halfSize;
        const float radiusX = std::clamp(std::abs(half.x) + std::abs(half.z), 0.10f, 0.75f);
        const float radiusZ = std::clamp(std::abs(half.z) + std::abs(half.x) * 0.45f, 0.09f, 0.60f);
        DrawShadowQuad(shadowTexture_, groundPoint, projected, radiusX, radiusZ,
                       bodyYaw, 120);
    }
}

void AvatarAssetCache::Unload() noexcept {
    if (baseCharacter_.meshCount > 0) UnloadModel(baseCharacter_);
    baseCharacter_ = Model{};
    baseCharacterMaterials_.clear();
    baseCharacterLoaded_ = false;
    for (auto& [_, entry] : textures_) if (entry.texture.id) UnloadTexture(entry.texture);
    for (auto& [_, entry] : models_) {
        if (!entry.model.meshCount) continue;
        // External textures are owned by the shared texture cache, not by the model.
        if (entry.hasExternalTexture)
            for (int i = 0; i < entry.model.materialCount; ++i)
                entry.model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture.id = 0;
        UnloadModel(entry.model);
    }
    textures_.clear();
    models_.clear();
    if (shadowTexture_.id) UnloadTexture(shadowTexture_);
    shadowTexture_ = Texture2D{};
    lightingShader_ = Shader{};
}
