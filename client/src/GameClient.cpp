#include "GameClient.hpp"
#include "BluboxBrandIcon.hpp"
#include "BluboxChatIcon.hpp"
#include "BluboxUiFont.hpp"
#include "BluboxVerifiedBadge.hpp"
#include "RaylibCompat.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rlgl.h>

namespace {

constexpr int kWindowWidth = 1920;
constexpr int kWindowHeight = 1080;
constexpr float kMouseSensitivity = 0.003f;
constexpr float kMinimumCameraPitch = -0.15f;
constexpr float kMaximumCameraPitch = 0.85f;
constexpr float kGroundHalfExtent = 50.0f;
constexpr float kGroundThickness = 1.0f;
constexpr int kSkyFaceSize = 160;
constexpr std::size_t kMaxChatMessageBytes = 256;
constexpr float kChatButtonX = 18.0f;
constexpr float kChatButtonY = 18.0f;
constexpr float kChatButtonWidth = 100.0f;
constexpr float kChatButtonHeight = 44.0f;
constexpr float kChatPanelWidth = 440.0f;
constexpr float kChatPanelHeight = 330.0f;
constexpr float kChatPanelGap = 10.0f;
// Keep direct light on the player's camera-facing side so character shading is
// visible from the normal third-person view. The artwork sun remains in front
// of the camera, independent from this lighting direction.
constexpr Vector3 kSunToLight{ -0.36f, 0.74f, 0.56f };
constexpr Vector3 kVisibleSunDirection{ -0.36f, 0.74f, -0.56f };
constexpr Color kHudColor{ 232, 241, 250, 220 };

struct NoisePoint {
    float x;
    float y;
    float z;
};

constexpr const char *kLightingVertexShader = R"GLSL(
#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragNormal;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
uniform vec4 colDiffuse;
void main() {
    fragNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    fragPosition = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor * colDiffuse;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

constexpr const char *kLightingFragmentShader = R"GLSL(
#version 330
in vec3 fragNormal;
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
uniform vec3 sunToLight;
uniform vec3 viewPosition;
uniform float ambientStrength;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(sunToLight);
    vec3 V = normalize(viewPosition - fragPosition);
    vec3 H = normalize(L + V);

    float diffuse = max(dot(N, L), 0.0);
    float specular = (diffuse > 0.0)
        ? pow(max(dot(N, H), 0.0), 48.0) * 0.10
        : 0.0;
    vec3 lighting = vec3(ambientStrength) + vec3(1.15 * diffuse);
    vec4 texColor = texture(texture0, fragTexCoord) * fragColor;
    vec3 litColor = texColor.rgb * lighting + vec3(specular);
    finalColor = vec4(litColor, texColor.a);
}
)GLSL";

constexpr const char *kSkyboxVertexShader = R"GLSL(
#version 330
in vec3 vertexPosition;
uniform mat4 matProjection;
uniform mat4 matView;
out vec3 fragDirection;
void main() {
    fragDirection = vertexPosition;
    mat4 rotationOnlyView = mat4(mat3(matView));
    vec4 clip = matProjection * rotationOnlyView * vec4(vertexPosition, 1.0);
    gl_Position = clip.xyww;
}
)GLSL";

constexpr const char *kSkyboxFragmentShader = R"GLSL(
#version 330
in vec3 fragDirection;
uniform samplerCube environmentMap;
out vec4 finalColor;
void main() {
    finalColor = vec4(texture(environmentMap, normalize(fragDirection)).rgb, 1.0);
}
)GLSL";

void AppendUtf8(std::string &text, int codepoint) {
    if (codepoint < 0x80) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint < 0x10000 &&
               !(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        text.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

std::string FitChatLine(const Font *font, std::string_view text, float maxWidth,
                        float fontSize, float spacing) {
    if (MeasureTextExSafe(font, std::string(text).c_str(), fontSize, spacing).x <= maxWidth)
        return std::string(text);

    std::size_t lastFit = 0;
    for (std::size_t end = 0; end < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[end]);
        const std::size_t codepointBytes = lead < 0x80 ? 1 :
            (lead & 0xE0) == 0xC0 ? 2 :
            (lead & 0xF0) == 0xE0 ? 3 :
            (lead & 0xF8) == 0xF0 ? 4 : 1;
        end = std::min(text.size(), end + codepointBytes);
        const std::string candidate(text.substr(0, end));
        if (MeasureTextExSafe(font, candidate.c_str(), fontSize, spacing).x > maxWidth)
            break;
        lastFit = end;
    }
    if (lastFit == 0) return "...";
    return std::string(text.substr(0, lastFit)) + "...";
}

Rectangle ChatButtonBounds() {
    return Rectangle{kChatButtonX, kChatButtonY, kChatButtonWidth, kChatButtonHeight};
}

Rectangle ChatPanelBounds() {
    return Rectangle{kChatButtonX,
                     kChatButtonY + kChatButtonHeight + kChatPanelGap,
                     kChatPanelWidth, kChatPanelHeight};
}

Rectangle ChatInputBounds() {
    const Rectangle panel = ChatPanelBounds();
    return Rectangle{panel.x + 14.0f, panel.y + panel.height - 58.0f,
                     panel.width - 28.0f, 38.0f};
}

float SmoothStep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float LerpAngle(float from, float to, float t) {
    return from + std::remainder(to - from, 2.0f * PI) * t;
}

Color AvatarColor(std::string_view value, Color fallback) {
    if (value.size() != 7 || value.front() != '#') return fallback;
    unsigned int rgb = 0;
    const auto result = std::from_chars(value.data() + 1, value.data() + value.size(), rgb, 16);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return fallback;
    return Color{static_cast<unsigned char>((rgb >> 16) & 0xff),
                 static_cast<unsigned char>((rgb >> 8) & 0xff),
                 static_cast<unsigned char>(rgb & 0xff), 255};
}

void DrawAvatar(Vector3 center, const NetworkPlayerSnapshot& appearance, Color identityTint,
                AvatarAssetCache& avatarAssets,
                const Model* headSphereModel,
                float facingYaw, bool walking, bool airborne, float animationTimeSeconds) {
    const Color skin = AvatarColor(appearance.bodyColor, identityTint);
    const Color shirt = AvatarColor(appearance.shirtColor, Color{34, 184, 207, 255});
    const Color pants = AvatarColor(appearance.pantsColor, Color{51, 75, 120, 255});
    // The website avatar editor and the client share a feet-origin rig. Jolt's
    // player position is the capsule center, so move the visual root down by
    // the capsule half-height before drawing the matching proportions.
    constexpr float kCharacterHalfHeight = Player::kHalfHeightOfCylinder + Player::kRadius;
    const Vector3 feetPosition{center.x, center.y - kCharacterHalfHeight, center.z};
    const auto animation = airborne ? AvatarAssetCache::CharacterAnimation::Jump
        : walking ? AvatarAssetCache::CharacterAnimation::Walk
                  : AvatarAssetCache::CharacterAnimation::Idle;
    if (avatarAssets.DrawBaseCharacter(feetPosition, appearance, facingYaw, animation,
                                       animationTimeSeconds)) return;

    rlPushMatrix();
    rlTranslatef(center.x, center.y - kCharacterHalfHeight, center.z);
    // Rotate the whole avatar around its feet so its front follows the view yaw.
    // The model's authored front is +Z, while yaw 0 looks toward -Z.
    // The camera/player forward vector is (sin(yaw), 0, -cos(yaw));
    // rotating the authored +Z-facing avatar by PI-yaw matches that vector.
    rlRotatef((PI - facingYaw) * RAD2DEG, 0.0f, 1.0f, 0.0f);
    DrawCubeV({0.0f, 1.25f, 0.0f}, {0.82f, 0.90f, 0.42f}, shirt);
    if (headSphereModel && headSphereModel->meshCount > 0) {
        const Vector3 headPosition{0.0f, 1.93f, 0.0f};
        DrawModelSafe(headSphereModel, &headPosition, 0.31f / Player::kRadius, &skin);
    } else {
        DrawSphereEx({0.0f, 1.93f, 0.0f}, 0.31f, 32, 32, skin);
    }
    DrawCubeV({-0.57f, 1.28f, 0.0f}, {0.23f, 0.70f, 0.25f}, skin);
    DrawCubeV({0.57f, 1.28f, 0.0f}, {0.23f, 0.70f, 0.25f}, skin);
    DrawCubeV({-0.23f, 0.43f, 0.0f}, {0.34f, 0.75f, 0.34f}, pants);
    DrawCubeV({0.23f, 0.43f, 0.0f}, {0.34f, 0.75f, 0.34f}, pants);
    // Render only published accessory assets. Legacy demo shapes are not
    // avatar inventory and must never appear as fabricated equipped items.
    avatarAssets.Draw(appearance);
    rlPopMatrix();
}

struct DisconnectDialogLayout {
    Rectangle panel{};
    Rectangle leave{};
};

DisconnectDialogLayout GetDisconnectDialogLayout() {
    const float width = std::min(440.0f, static_cast<float>(GetScreenWidth()) - 32.0f);
    constexpr float height = 248.0f;
    const float x = (static_cast<float>(GetScreenWidth()) - width) * 0.5f;
    const float y = (static_cast<float>(GetScreenHeight()) - height) * 0.5f;
    return {
        Rectangle{x, y, width, height},
        Rectangle{x + 24.0f, y + height - 62.0f, width - 48.0f, 42.0f}
    };
}

void DrawCenteredText(const Font *font, const char *text, float centerX, float y,
                      float size, Color color) {
    const Vector2 measured = MeasureTextExSafe(font, text, size, 0.0f);
    DrawTextExSafe(font, text, Vector2{centerX - measured.x * 0.5f, y}, size, 0.0f, color);
}

void DrawDisconnectDialog(const Font *font, DisconnectKind reason) {
    const DisconnectDialogLayout layout = GetDisconnectDialogLayout();
    const float centerX = layout.panel.x + layout.panel.width * 0.5f;
    const Vector2 mouse = GetMousePosition();
    const bool leaveHover = CheckCollisionPointRec(mouse, layout.leave);

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Color{8, 11, 17, 205});
    DrawRectangleRounded(layout.panel, 0.075f, 16, Color{19, 19, 26, 255});
    DrawRectangleRounded(Rectangle{layout.panel.x, layout.panel.y, layout.panel.width, 5.0f},
                         0.2f, 8, Color{0, 210, 255, 255});
    DrawLineEx(Vector2{layout.panel.x + 22.0f, layout.panel.y + 58.0f},
               Vector2{layout.panel.x + layout.panel.width - 22.0f, layout.panel.y + 58.0f},
               1.0f, Color{55, 63, 76, 255});

    const char *message = "Connection to the Blubox server was interrupted.";
    const char *code = "BLX-1001 | Connection lost";
    if (reason == DisconnectKind::Kicked) {
        message = "The server ended your session.";
        code = "BLX-1002 | Session ended";
    } else if (reason == DisconnectKind::JoinRejected) {
        message = "Blubox couldn't validate your join request.";
        code = "BLX-1003 | Join not accepted";
    } else if (reason == DisconnectKind::ConnectionFailed) {
        message = "Couldn't reach this Blubox server.";
        code = "BLX-1004 | Server unavailable";
    }

    DrawCenteredText(font, "Connection interrupted", centerX, layout.panel.y + 20.0f, 21.0f,
                     Color{246, 248, 252, 255});
    DrawCenteredText(font, message, centerX, layout.panel.y + 88.0f, 16.0f,
                     Color{190, 198, 212, 255});
    DrawCenteredText(font, code, centerX, layout.panel.y + 118.0f, 14.0f,
                     Color{0, 210, 255, 255});

    DrawRectangleRounded(layout.leave, 0.18f, 8,
                         leaveHover ? Color{56, 224, 255, 255} : Color{0, 210, 255, 255});
    DrawCenteredText(font, "Leave server", centerX, layout.leave.y + 10.0f, 16.0f,
                     Color{19, 19, 26, 255});
}

float HashNoise(int x, int y, int z) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u +
                      static_cast<std::uint32_t>(y) * 668265263u +
                      static_cast<std::uint32_t>(z) * 2246822519u + 3266489917u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
}

float ValueNoise(NoisePoint p) {
    const int x0 = static_cast<int>(std::floor(p.x));
    const int y0 = static_cast<int>(std::floor(p.y));
    const int z0 = static_cast<int>(std::floor(p.z));
    const float fx = p.x - static_cast<float>(x0);
    const float fy = p.y - static_cast<float>(y0);
    const float fz = p.z - static_cast<float>(z0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float sz = fz * fz * (3.0f - 2.0f * fz);
    const float x00 = Lerp(HashNoise(x0, y0, z0), HashNoise(x0 + 1, y0, z0), sx);
    const float x10 = Lerp(HashNoise(x0, y0 + 1, z0), HashNoise(x0 + 1, y0 + 1, z0), sx);
    const float x01 = Lerp(HashNoise(x0, y0, z0 + 1), HashNoise(x0 + 1, y0, z0 + 1), sx);
    const float x11 = Lerp(HashNoise(x0, y0 + 1, z0 + 1), HashNoise(x0 + 1, y0 + 1, z0 + 1), sx);
    return Lerp(Lerp(x00, x10, sy), Lerp(x01, x11, sy), sz);
}

Vector3 CubemapDirection(int face, float u, float v) {
    // Raylib's vertical cubemap layout order is +X, -X, +Y, -Y, +Z, -Z.
    switch (face) {
    case 0: return Vector3{ 1.0f, v, -u };
    case 1: return Vector3{ -1.0f, v, u };
    case 2: return Vector3{ u, 1.0f, v };
    case 3: return Vector3{ u, -1.0f, -v };
    case 4: return Vector3{ u, v, 1.0f };
    default: return Vector3{ -u, v, -1.0f };
    }
}

Color CreateSkyColor(Vector3 direction) {
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                   direction.z * direction.z);
    direction.x /= length;
    direction.y /= length;
    direction.z /= length;

    const float horizon = SmoothStep(-0.25f, 0.75f, direction.y);
    const Color low{ 116, 169, 202, 255 };
    const Color high{ 48, 116, 195, 255 };
    Color sky{
        static_cast<unsigned char>(Lerp(low.r, high.r, horizon)),
        static_cast<unsigned char>(Lerp(low.g, high.g, horizon)),
        static_cast<unsigned char>(Lerp(low.b, high.b, horizon)),
        255
    };

    const NoisePoint p{ direction.x * 6.0f, direction.y * 6.0f, direction.z * 6.0f };
    const float cloudNoise = ValueNoise(p) * 0.56f +
        ValueNoise({ p.x * 1.93f + 17.0f, p.y * 1.93f, p.z * 1.93f - 9.0f }) * 0.29f +
        ValueNoise({ p.x * 3.81f - 4.0f, p.y * 3.81f + 2.0f, p.z * 3.81f }) * 0.15f;
    const float cloudCoverage = SmoothStep(0.53f, 0.68f, cloudNoise) *
                                SmoothStep(-0.12f, 0.18f, direction.y) * 0.82f;
    sky.r = static_cast<unsigned char>(Lerp(sky.r, 247.0f, cloudCoverage));
    sky.g = static_cast<unsigned char>(Lerp(sky.g, 250.0f, cloudCoverage));
    sky.b = static_cast<unsigned char>(Lerp(sky.b, 255.0f, cloudCoverage));
    return sky;
}

TextureCubemap CreateProceduralSkyCubemap() {
    Image image = GenImageColor(kSkyFaceSize, kSkyFaceSize * 6, Color{ 32, 64, 96, 255 });
    if (image.data == nullptr) return TextureCubemap{};

    auto *pixels = static_cast<Color *>(image.data);
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < kSkyFaceSize; ++y) {
            for (int x = 0; x < kSkyFaceSize; ++x) {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / kSkyFaceSize - 1.0f;
                const float v = 1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / kSkyFaceSize;
                const Vector3 direction = CubemapDirection(face, u, v);
                pixels[face * kSkyFaceSize * kSkyFaceSize + y * kSkyFaceSize + x] =
                    CreateSkyColor(direction);
            }
        }
    }

    TextureCubemap result = LoadTextureCubemap(image, CUBEMAP_LAYOUT_LINE_VERTICAL);
    UnloadImage(image);
    if (result.id != 0) SetTextureFilter(result, TEXTURE_FILTER_BILINEAR);
    return result;
}

Texture2D CreateSunTexture() {
    constexpr int size = 96;
    Image image = GenImageColor(size, size, BLANK);
    if (image.data == nullptr) return Texture2D{};
    auto *pixels = static_cast<Color *>(image.data);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = (2.0f * (x + 0.5f) / size) - 1.0f;
            const float dy = (2.0f * (y + 0.5f) / size) - 1.0f;
            const float radius = std::sqrt(dx * dx + dy * dy);
            const float alpha = std::clamp((1.0f - radius) * 2.5f, 0.0f, 1.0f);
            const float core = 1.0f - SmoothStep(0.34f, 0.52f, radius);
            pixels[y * size + x] = Color{
                255,
                static_cast<unsigned char>(Lerp(154.0f, 238.0f, core)),
                static_cast<unsigned char>(Lerp(68.0f, 205.0f, core)),
                static_cast<unsigned char>(255.0f * alpha)
            };
        }
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

Vector3 ToRaylibVector(const JPH::Vec3 &value) {
    return Vector3{value.GetX(), value.GetY(), value.GetZ()};
}

Vector3 NormalizeVector(Vector3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001f) return Vector3{ 0.0f, 1.0f, 0.0f };
    return Vector3{ value.x / length, value.y / length, value.z / length };
}

const std::array<Color, 128> &PlayerPalette() {
    // Use a cyan-to-blue palette only. Varying hue, saturation and brightness
    // gives the server's 128-player limit a unique, repeatable color slot.
    static const std::array<Color, 128> palette = [] {
        std::array<Color, 128> colors{};
        constexpr std::array<float, 8> hues{183.0f, 189.0f, 195.0f, 201.0f,
                                           207.0f, 213.0f, 219.0f, 225.0f};
        constexpr std::array<float, 4> saturations{0.72f, 0.82f, 0.91f, 0.98f};
        constexpr std::array<float, 4> brightness{0.78f, 0.86f, 0.94f, 1.0f};
        std::size_t index = 0;
        for (const float value : brightness) {
            for (const float saturation : saturations) {
                for (const float hue : hues) {
                    colors[index++] = ColorFromHSV(hue, saturation, value);
                }
            }
        }
        return colors;
    }();
    return palette;
}

Color PlayerColorForSession(std::uint64_t sessionId,
                            const std::unordered_map<std::uint64_t, std::size_t> &slots) {
    const auto &palette = PlayerPalette();
    if (sessionId == 0) return palette.front();
    const auto found = slots.find(sessionId);
    return found == slots.end() ? palette.front() : palette[found->second % palette.size()];
}

} // namespace

GameClient::GameClient(LaunchOptions options)
    : options_(std::move(options)),
      session_(options_.protocolLaunch.has_value() ? &*options_.protocolLaunch : nullptr) {}

int GameClient::Run() {
    // Do not require an MSAA pixel format from WGL. Some Windows drivers and
    // remote display adapters expose OpenGL but no 4x multisample format; GLFW
    // then rejects every available pixel format before the client can start.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(kWindowWidth, kWindowHeight, "Blubox Client - Local Guest");
    if (!IsWindowReady()) {
        throw std::runtime_error("Blubox Client couldn't create its OpenGL window. Update your graphics driver and try again.");
    }
    ClearWindowState(FLAG_WINDOW_UNDECORATED);
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    Image appIcon = LoadImageFromMemory(".png", blubox::kBrandIconData, blubox::kBrandIconDataSize);
    if (appIcon.data != nullptr) {
        ImageFormat(&appIcon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(appIcon);
        UnloadImage(appIcon);
    }
    Image verifiedBadgeImage = LoadImageFromMemory(
        ".png", blubox::kVerifiedBadgeData, blubox::kVerifiedBadgeDataSize);
    if (verifiedBadgeImage.data != nullptr) {
        verifiedBadgeTexture_ = LoadTextureFromImage(verifiedBadgeImage);
        UnloadImage(verifiedBadgeImage);
    }
    if (verifiedBadgeTexture_.id == 0) {
        TraceLog(LOG_WARNING, "Verified chat badge could not be loaded.");
    }
    Image chatIconImage = LoadImageFromMemory(
        ".png", blubox::kChatIconData, blubox::kChatIconDataSize);
    if (chatIconImage.data != nullptr) {
        chatIconTexture_ = LoadTextureFromImage(chatIconImage);
        UnloadImage(chatIconImage);
    }
    if (chatIconTexture_.id == 0) {
        TraceLog(LOG_WARNING, "Chat icon could not be loaded.");
    }

    if (options_.protocolLaunch.has_value() && options_.protocolLaunch->action == "join") {
        networkClient_.Connect(session_);
    } else {
        TraceLog(LOG_INFO, "GameClient: starting a local guest session without server connection.");
    }

    bool publishedWorldLoaded = false;
    if (session_.HasWorldId()) {
        std::string worldError;
        publishedWorldLoaded = publishedWorld_.Load(session_.WorldId(), worldError);
        if (publishedWorldLoaded) {
            TraceLog(LOG_INFO, "GameClient: loaded published world '%s' (%d parts, %d GUI elements).",
                     session_.WorldId().c_str(), static_cast<int>(publishedWorld_.Parts().size()),
                     publishedWorld_.HasGui() ? 1 : 0);
        } else {
            TraceLog(LOG_WARNING, "GameClient: published map unavailable: %s", worldError.c_str());
        }
    }
    if (publishedWorldLoaded && !publishedWorld_.LocalScripts().empty())
        localScriptRuntime_.Start(publishedWorld_);

    SetWindowMinSize(960, 540);
    SetTargetFPS(180);
    SetExitKey(KEY_NULL);
    if (options_.startFullscreen) {
        ToggleFullscreen();
    }

    lightingShader_ = LoadShaderFromMemory(kLightingVertexShader, kLightingFragmentShader);
    if (lightingShader_.id != 0) {
        lightingShader_.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(lightingShader_, "mvp");
        lightingShader_.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(lightingShader_, "matModel");
        lightingShader_.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(lightingShader_, "matNormal");
        lightingShader_.locs[SHADER_LOC_COLOR_DIFFUSE] = GetShaderLocation(lightingShader_, "colDiffuse");
        lightingShader_.locs[SHADER_LOC_MAP_DIFFUSE] = GetShaderLocation(lightingShader_, "texture0");
        sunToLightLocation_ = GetShaderLocation(lightingShader_, "sunToLight");
        viewPositionLocation_ = GetShaderLocation(lightingShader_, "viewPosition");
        const int ambientLocation = GetShaderLocation(lightingShader_, "ambientStrength");
        const float ambient = 0.18f;
        if (sunToLightLocation_ < 0 || viewPositionLocation_ < 0 || ambientLocation < 0 ||
            lightingShader_.locs[SHADER_LOC_MATRIX_MODEL] < 0 ||
            lightingShader_.locs[SHADER_LOC_MATRIX_NORMAL] < 0 ||
            lightingShader_.locs[SHADER_LOC_COLOR_DIFFUSE] < 0) {
        TraceLog(LOG_WARNING, "Lighting shader is incomplete; using the standard Raylib renderer.");
            UnloadShader(lightingShader_);
            lightingShader_ = Shader{};
        } else {
            SetShaderValue(lightingShader_, ambientLocation, &ambient, SHADER_UNIFORM_FLOAT);
        }
    } else {
        TraceLog(LOG_WARNING, "Lighting shader unavailable; using the standard renderer.");
    }

    CreateCapsuleCharacterModelsSafe(&characterCylinderModel_, &characterSphereModel_,
                                     Player::kRadius, Player::kHalfHeightOfCylinder * 2.0f,
                                     24, 16);
    characterModelsReady_ = characterCylinderModel_.meshCount > 0 &&
                            characterCylinderModel_.materialCount > 0 &&
                            characterSphereModel_.meshCount > 0 &&
                            characterSphereModel_.materialCount > 0;
    if (characterModelsReady_) {
        if (lightingShader_.id != 0) {
            characterCylinderModel_.materials[0].shader = lightingShader_;
            characterSphereModel_.materials[0].shader = lightingShader_;
        }
    } else {
        TraceLog(LOG_WARNING, "Player mesh unavailable; using the Raylib capsule.");
    }
    avatarAssets_.LoadBaseCharacter(lightingShader_);

    skyboxShader_ = LoadShaderFromMemory(kSkyboxVertexShader, kSkyboxFragmentShader);
    if (skyboxShader_.id != 0) {
        skyboxShader_.locs[SHADER_LOC_MATRIX_PROJECTION] =
            GetShaderLocation(skyboxShader_, "matProjection");
        skyboxShader_.locs[SHADER_LOC_MATRIX_VIEW] = GetShaderLocation(skyboxShader_, "matView");
        skyboxEnvironmentLocation_ = GetShaderLocation(skyboxShader_, "environmentMap");
        // DrawMesh binds material maps by their Raylib shader-location slots.
        // The custom sampler name must therefore be registered for cubemaps.
        skyboxShader_.locs[SHADER_LOC_MAP_CUBEMAP] = skyboxEnvironmentLocation_;
        skyboxCubemap_ = CreateProceduralSkyCubemap();
        CreateCubeModelSafe(&skyboxModel_, 1.0f, 1.0f, 1.0f);

        if (skyboxCubemap_.id != 0 && skyboxModel_.materialCount > 0 &&
            skyboxEnvironmentLocation_ >= 0) {
            skyboxModel_.materials[0].shader = skyboxShader_;
            skyboxModel_.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = skyboxCubemap_;
            const int cubemapMap = MATERIAL_MAP_CUBEMAP;
            SetShaderValue(skyboxShader_, skyboxEnvironmentLocation_, &cubemapMap,
                           SHADER_UNIFORM_INT);
            skyboxReady_ = true;
        } else {
            TraceLog(LOG_WARNING, "Cubemap skybox unavailable; using the background color.");
        }
    }
    sunTexture_ = CreateSunTexture();

    // Prefer LoadFontEx for the source asset. Packaged launcher installs only
    // the client executable, so keep an embedded copy as a reliable fallback.
    constexpr const char *fontPaths[] = {
        "assets/fonts/Lato-Regular.ttf",
        "../assets/fonts/Lato-Regular.ttf"
    };
    for (const char *fontPath : fontPaths) {
        if (FileExists(fontPath)) {
            customFontLoaded_ = LoadFontExSafe(&uiFont_, fontPath, 32);
            if (customFontLoaded_) break;
        }
    }
    if (!customFontLoaded_) {
        customFontLoaded_ = LoadFontFromMemorySafe(&uiFont_, ".ttf", blubox::kUiFontData,
                                                   blubox::kUiFontDataSize, 32);
    }
    if (customFontLoaded_) SetTextureFilter(uiFont_.texture, TEXTURE_FILTER_BILINEAR);
    else TraceLog(LOG_WARNING, "Embedded UI font could not be loaded; using the Raylib font.");
    TraceLog(LOG_INFO, "GameClient: lighting, cubemap sky and UI font initialized.");

    if (publishedWorldLoaded && publishedWorld_.HasParts()) {
        std::size_t collisionBodies = 0;
        for (const auto &part : publishedWorld_.Parts()) {
            if (!part.collidable || collisionBodies >= 8192) continue;
            const float hx = std::max(0.05f, part.size.x * 0.5f);
            const float hy = std::max(0.05f, part.size.y * 0.5f);
            const float hz = std::max(0.05f, part.size.z * 0.5f);
            physicsWorld_.CreateStaticBox(JPH::RVec3(part.position.x, part.position.y, part.position.z),
                                          JPH::Vec3(hx, hy, hz));
            ++collisionBodies;
        }
        TraceLog(LOG_INFO, "GameClient: created %d static collision bodies from the published map.",
                 static_cast<int>(collisionBodies));
    } else {
        physicsWorld_.CreateStaticBox(JPH::RVec3(0.0, -kGroundThickness * 0.5, 0.0),
                                      JPH::Vec3(kGroundHalfExtent, kGroundThickness * 0.5,
                                                kGroundHalfExtent));
        TraceLog(LOG_INFO, "GameClient: default Jolt ground created.");
    }
    const Vector3 initialSpawn = publishedWorldLoaded ? publishedWorld_.SpawnPosition() : Vector3{0.0f, 3.0f, 0.0f};
    player_ = std::make_unique<Player>(physicsWorld_, JPH::RVec3(initialSpawn.x, initialSpawn.y, initialSpawn.z));
    TraceLog(LOG_INFO, "GameClient: player created.");

    if (session_.HasToken()) {
        TraceLog(LOG_INFO, "Authentication session ready; token kept in memory only.");
    } else if (options_.protocolLaunch.has_value() && options_.protocolLaunch->action == "join") {
        TraceLog(LOG_WARNING, "Join requested without a token; the session is not authenticated.");
    }
    if (options_.protocolLaunch.has_value() && options_.protocolLaunch->action == "join" &&
        !session_.HasWorldId()) {
        TraceLog(LOG_WARNING, "Join requested without a worldId.");
    }

    camera_.up = Vector3{0.0f, 1.0f, 0.0f};
    camera_.fovy = 60.0f;
    camera_.projection = CAMERA_PERSPECTIVE;
    mouseCaptured_ = !(publishedWorldLoaded && publishedWorld_.HasGui());
    if (mouseCaptured_) DisableCursor();
    else EnableCursor();
    UpdateCamera();
    TraceLog(LOG_INFO, "GameClient: entering frame loop.");

    while (!WindowShouldClose() && running_) {
        const float deltaSeconds = GetFrameTime();
        Update(deltaSeconds);
        if (!networkClient_.HasDisconnectNotice()) localScriptRuntime_.Tick(deltaSeconds);
        networkClient_.Poll();
        if (!networkClient_.HasDisconnectNotice()) SyncRemotePlayers(deltaSeconds);
        // The network notice can arrive after Update() has already run for this
        // frame. Release capture immediately so the disconnect dialog is
        // interactive on its very first rendered frame.
        if (networkClient_.HasDisconnectNotice() && !disconnectUiActive_) {
            mouseCaptured_ = false;
            EnableCursor();
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
            disconnectUiActive_ = true;
        }
        if (!networkClient_.HasDisconnectNotice()) physicsWorld_.Step(deltaSeconds);
        const float animationDelta = std::clamp(deltaSeconds, 0.0f, 0.1f);
        const JPH::Vec3 localVelocity = physicsWorld_.GetLinearVelocity(player_->BodyId());
        const float localHorizontalSpeedSquared =
            localVelocity.GetX() * localVelocity.GetX() +
            localVelocity.GetZ() * localVelocity.GetZ();
        JPH::RVec3 localGround;
        const bool localGrounded = localVelocity.GetY() <= 0.35f &&
            physicsWorld_.RaycastGround(player_->BodyId(),
                Player::kHalfHeightOfCylinder + Player::kRadius, 0.32f, localGround);
        const bool walking = localHorizontalSpeedSquared > 0.09f;
        const bool airborne = !localGrounded;
        if (!localAnimationInitialized_ || walking != localWalking_ ||
            airborne != localAirborne_) {
            localAnimationTimeSeconds_ = 0.0f;
            localAnimationInitialized_ = true;
        } else {
            localAnimationTimeSeconds_ += animationDelta;
        }
        localWalking_ = walking;
        localAirborne_ = airborne;
        const JPH::Vec3 networkPosition = player_->Position();
        networkClient_.SendPlayerPosition(networkPosition.GetX(), networkPosition.GetY(),
                                          networkPosition.GetZ(), playerFacingYaw_);
        UpdateCamera();
        DrawFrame();
    }

    EnableCursor();
    localScriptRuntime_.Stop();
    if (customFontLoaded_) UnloadFont(uiFont_);
    for (const auto &[name, texture] : nameplateTextures_) {
        (void)name;
        if (texture.id != 0) UnloadTexture(texture);
    }
    nameplateTextures_.clear();
    if (verifiedBadgeTexture_.id != 0) UnloadTexture(verifiedBadgeTexture_);
    if (chatIconTexture_.id != 0) UnloadTexture(chatIconTexture_);
    if (characterCylinderModel_.meshCount > 0) UnloadModel(characterCylinderModel_);
    if (characterSphereModel_.meshCount > 0) UnloadModel(characterSphereModel_);
    if (sunTexture_.id != 0) UnloadTexture(sunTexture_);
    if (skyboxModel_.meshCount > 0) {
        skyboxModel_.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture.id = 0;
        UnloadModel(skyboxModel_);
    }
    if (skyboxCubemap_.id != 0) UnloadTexture(skyboxCubemap_);
    if (skyboxShader_.id != 0) UnloadShader(skyboxShader_);
    if (lightingShader_.id != 0) UnloadShader(lightingShader_);
    CloseWindow();
    player_.reset();
    return 0;
}

void GameClient::Update(float deltaSeconds) {
    if (networkClient_.HasDisconnectNotice()) {
        // Keep the actual Raylib/GLFW cursor mode in sync with the gameplay
        // flag. A stale disabled-cursor mode reports the pointer at the center
        // of the window and makes the Leave button impossible to hit.
        mouseCaptured_ = false;
        if (IsCursorHidden()) EnableCursor();

        const auto layout = GetDisconnectDialogLayout();
        const Vector2 mouse = GetMousePosition();
        const bool leaveHovered = CheckCollisionPointRec(mouse, layout.leave);
        SetMouseCursor(leaveHovered ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && leaveHovered) {
            running_ = false;
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) running_ = false;
        return;
    }

    if (chatInputActive_) {
        UpdateChatInput();
        return;
    }
    const Vector2 mousePosition = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mousePosition, ChatButtonBounds())) {
        chatPanelVisible_ = !chatPanelVisible_;
        return;
    }
    if (IsKeyPressed(KEY_SLASH)) {
        OpenChatInput();
        return;
    }
    if (chatPanelVisible_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mousePosition, ChatInputBounds())) {
        OpenChatInput();
        return;
    }

    if (IsKeyPressed(KEY_LEFT_SHIFT) || IsKeyPressed(KEY_RIGHT_SHIFT)) {
        shiftLockEnabled_ = !shiftLockEnabled_;
        if (shiftLockEnabled_) {
            mouseCaptured_ = true;
            DisableCursor();
        } else {
            mouseCaptured_ = false;
            EnableCursor();
        }
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (shiftLockEnabled_) {
            shiftLockEnabled_ = false;
        }
        if (mouseCaptured_) {
            mouseCaptured_ = false;
            EnableCursor();
        }
    }
    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }
    if (IsKeyPressed(KEY_TAB)) {
        mouseCaptured_ = !mouseCaptured_;
        if (mouseCaptured_) DisableCursor();
        else EnableCursor();
    } else if (!shiftLockEnabled_) {
        std::string activatedId;
        const bool uiConsumed = publishedWorld_.HandleGuiPointer(
            GetMousePosition(), IsMouseButtonPressed(MOUSE_BUTTON_LEFT),
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT), activatedId);
        if (!activatedId.empty()) localScriptRuntime_.FireActivated(activatedId);
        if (!uiConsumed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            mouseCaptured_ = true;
            DisableCursor();
        } else if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            mouseCaptured_ = true;
            DisableCursor();
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            mouseCaptured_ = false;
            EnableCursor();
        }
    }

    if (shiftLockEnabled_ || mouseCaptured_) {
        const Vector2 mouseDelta = GetMouseDelta();
        cameraYaw_ += mouseDelta.x * kMouseSensitivity;
        cameraPitch_ = std::clamp(cameraPitch_ + mouseDelta.y * kMouseSensitivity,
                                  kMinimumCameraPitch, kMaximumCameraPitch);
    }

    float forwardInput = 0.0f;
    float rightInput = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) forwardInput += 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) forwardInput -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) rightInput += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) rightInput -= 1.0f;
    const float inputLength = std::sqrt(forwardInput * forwardInput + rightInput * rightInput);

    float targetFacingYaw = playerFacingYaw_;
    if (shiftLockEnabled_) {
        targetFacingYaw = cameraYaw_;
    } else if (inputLength > 0.01f) {
        const float moveAngle = std::atan2(rightInput, forwardInput);
        targetFacingYaw = cameraYaw_ + moveAngle;
    }
    const float facingBlend = 1.0f - std::exp(-14.0f * std::max(0.0f, deltaSeconds));
    playerFacingYaw_ = LerpAngle(playerFacingYaw_, targetFacingYaw, facingBlend);

    player_->Update(deltaSeconds, cameraYaw_);
}

void GameClient::UpdateChatInput() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        const Vector2 mousePosition = GetMousePosition();
        if (CheckCollisionPointRec(mousePosition, ChatButtonBounds())) {
            chatPanelVisible_ = !chatPanelVisible_;
            CloseChatInput();
            return;
        }
        if (!CheckCollisionPointRec(mousePosition, ChatInputBounds())) {
            CloseChatInput();
            return;
        }
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        chatInput_.clear();
        CloseChatInput();
        return;
    }
    if (IsKeyPressed(KEY_BACKSPACE) && !chatInput_.empty()) {
        std::size_t start = chatInput_.size() - 1;
        while (start > 0 &&
               (static_cast<unsigned char>(chatInput_[start]) & 0xC0) == 0x80) {
            --start;
        }
        chatInput_.erase(start);
    }

    int codepoint = 0;
    while ((codepoint = GetCharPressed()) > 0) {
        if (codepoint < 0x20 || codepoint == 0x7F ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
            continue;
        }
        std::string candidate = chatInput_;
        AppendUtf8(candidate, codepoint);
        if (candidate.size() <= kMaxChatMessageBytes) chatInput_ = std::move(candidate);
    }

    if (IsKeyPressed(KEY_ENTER)) {
        if (!chatInput_.empty() && networkClient_.SendChatMessage(chatInput_)) {
            chatInput_.clear();
        }
        CloseChatInput();
    }
}

void GameClient::OpenChatInput() {
    chatPanelVisible_ = true;
    chatInputActive_ = true;
    chatReturnToShiftLock_ = shiftLockEnabled_;
    chatReturnMouseCaptured_ = mouseCaptured_;
    shiftLockEnabled_ = false;
    mouseCaptured_ = false;
    EnableCursor();
    while (GetCharPressed() > 0) {}
}

void GameClient::CloseChatInput() {
    chatInputActive_ = false;
    if (chatReturnToShiftLock_) {
        shiftLockEnabled_ = true;
        mouseCaptured_ = true;
        DisableCursor();
    } else {
        shiftLockEnabled_ = false;
        mouseCaptured_ = chatReturnMouseCaptured_;
        if (mouseCaptured_) DisableCursor();
        else EnableCursor();
    }
}

void GameClient::UpdateCamera() {
    const Vector3 playerPosition = ToRaylibVector(player_->Position());
    const Vector3 focusPoint{playerPosition.x, playerPosition.y + 0.45f, playerPosition.z};
    const float horizontalDistance = cameraDistance_ * std::cos(cameraPitch_);

    const Vector3 forward{std::sin(cameraYaw_), 0.0f, -std::cos(cameraYaw_)};
    camera_.target = focusPoint;
    camera_.position = Vector3{
        focusPoint.x - forward.x * horizontalDistance,
        focusPoint.y + cameraDistance_ * std::sin(cameraPitch_),
        focusPoint.z - forward.z * horizontalDistance
    };
}

void GameClient::SyncRemotePlayers(float deltaSeconds) {
    const float remoteBlend =
        1.0f - std::exp(-18.0f * std::clamp(deltaSeconds, 0.0f, 0.1f));
    std::vector<std::uint64_t> activeSessions;
    activeSessions.reserve(networkClient_.Players().size());

    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (remote.sessionId == 0 ||
            (networkClient_.SessionId() != 0 && remote.sessionId == networkClient_.SessionId()) ||
            !std::isfinite(remote.x) || !std::isfinite(remote.y) || !std::isfinite(remote.z)) {
            continue;
        }

        activeSessions.push_back(remote.sessionId);
        SmoothedPlayer &smoothed = smoothedPlayers_[remote.sessionId];
        if (!smoothed.initialized) {
            smoothed.x = remote.x;
            smoothed.y = remote.y;
            smoothed.z = remote.z;
            smoothed.yaw = remote.yaw;
            smoothed.initialized = true;
        } else {
            smoothed.x = Lerp(smoothed.x, remote.x, remoteBlend);
            smoothed.y = Lerp(smoothed.y, remote.y, remoteBlend);
            smoothed.z = Lerp(smoothed.z, remote.z, remoteBlend);
            smoothed.yaw = LerpAngle(smoothed.yaw, remote.yaw, remoteBlend);
        }

        const JPH::RVec3 position(smoothed.x, smoothed.y, smoothed.z);
        const auto body = remotePlayerBodies_.find(remote.sessionId);
        if (body == remotePlayerBodies_.end()) {
            const JPH::BodyID bodyId = physicsWorld_.CreateKinematicCapsule(
                position, Player::kHalfHeightOfCylinder, Player::kRadius);
            remotePlayerBodies_.emplace(remote.sessionId, bodyId);
        } else {
            physicsWorld_.MoveKinematicBody(body->second, position, deltaSeconds);
        }

        const bool wasWalking = smoothed.walking;
        const bool wasAirborne = smoothed.airborne;
        const float animationDelta = std::clamp(deltaSeconds, 0.0f, 0.1f);
        const JPH::BodyID bodyId = remotePlayerBodies_.at(remote.sessionId);
        const JPH::Vec3 velocity = physicsWorld_.GetLinearVelocity(bodyId);
        const float horizontalSpeedSquared =
            velocity.GetX() * velocity.GetX() + velocity.GetZ() * velocity.GetZ();
        JPH::RVec3 groundPoint;
        const bool grounded = velocity.GetY() <= 0.35f &&
            physicsWorld_.RaycastGround(bodyId,
                Player::kHalfHeightOfCylinder + Player::kRadius, 0.32f, groundPoint);
        smoothed.walking = horizontalSpeedSquared > 0.09f;
        smoothed.airborne = !grounded;
        if (smoothed.walking != wasWalking || smoothed.airborne != wasAirborne) {
            smoothed.animationTimeSeconds = 0.0f;
        } else {
            smoothed.animationTimeSeconds += animationDelta;
        }
    }

    for (auto body = remotePlayerBodies_.begin(); body != remotePlayerBodies_.end();) {
        if (std::find(activeSessions.begin(), activeSessions.end(), body->first) ==
            activeSessions.end()) {
            physicsWorld_.DestroyBody(body->second);
            smoothedPlayers_.erase(body->first);
            body = remotePlayerBodies_.erase(body);
        } else {
            ++body;
        }
    }
}

void GameClient::DrawFrame() {
    const Vector3 playerPosition = ToRaylibVector(player_->Position());
    std::string localDisplayName = session_.HasToken() ? session_.DisplayName() : "Guest";
    bool localVerified = networkClient_.UserVerified();
    NetworkPlayerSnapshot localAppearance = networkClient_.LocalAvatar();
    std::vector<std::uint64_t> activeSessions;
    activeSessions.reserve(networkClient_.Players().size() + 1);
    if (networkClient_.SessionId() != 0) activeSessions.push_back(networkClient_.SessionId());
    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (remote.sessionId != 0) activeSessions.push_back(remote.sessionId);
    }
    std::sort(activeSessions.begin(), activeSessions.end());
    activeSessions.erase(std::unique(activeSessions.begin(), activeSessions.end()),
                         activeSessions.end());
    for (auto it = playerColorSlots_.begin(); it != playerColorSlots_.end();) {
        if (!std::binary_search(activeSessions.begin(), activeSessions.end(), it->first))
            it = playerColorSlots_.erase(it);
        else
            ++it;
    }
    std::array<bool, 128> occupiedColorSlots{};
    for (const auto &[sessionId, slot] : playerColorSlots_) {
        (void)sessionId;
        if (slot < occupiedColorSlots.size()) occupiedColorSlots[slot] = true;
    }
    for (const std::uint64_t sessionId : activeSessions) {
        if (playerColorSlots_.contains(sessionId)) continue;
        const auto freeSlot = std::find(occupiedColorSlots.begin(), occupiedColorSlots.end(), false);
        if (freeSlot == occupiedColorSlots.end()) break;
        const std::size_t slot = static_cast<std::size_t>(freeSlot - occupiedColorSlots.begin());
        playerColorSlots_.emplace(sessionId, slot);
        *freeSlot = true;
    }
    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (networkClient_.SessionId() != 0 && remote.sessionId == networkClient_.SessionId() &&
            !remote.displayName.empty()) {
            localDisplayName = remote.displayName;
            localVerified = remote.isVerified;
            localAppearance = remote;
            break;
        }
    }

    Font fallbackFont{};
    const Font *font = &uiFont_;
    if (!customFontLoaded_) {
        GetFontDefaultSafe(&fallbackFont);
        font = &fallbackFont;
    }

    BeginDrawing();
    ClearBackground(Color{ 120, 174, 213, 255 });
    BeginMode3DSafe(&camera_);

    if (skyboxReady_) {
        // The skybox shader removes camera translation and writes at far depth.
        // Draw inside the cube with depth writes/culling disabled, then restore state.
        rlDisableBackfaceCulling();
        rlDisableDepthMask();
        const Vector3 skyboxPosition{ 0.0f, 0.0f, 0.0f };
        const Color skyboxTint = WHITE;
        DrawModelSafe(&skyboxModel_, &skyboxPosition, 1.0f, &skyboxTint);
        rlEnableDepthMask();
        rlEnableBackfaceCulling();
    }

    if (sunTexture_.id != 0) {
        const Vector3 sunPosition{
            camera_.position.x + kVisibleSunDirection.x * 500.0f,
            camera_.position.y + kVisibleSunDirection.y * 500.0f,
            camera_.position.z + kVisibleSunDirection.z * 500.0f
        };
        DrawBillboardSafe(&camera_, sunTexture_, sunPosition, 28.0f, WHITE);
    }

    if (lightingShader_.id != 0) {
        BeginShaderMode(lightingShader_);
        const Vector3 sunToLight = NormalizeVector(kSunToLight);
        SetShaderValue(lightingShader_, sunToLightLocation_, &sunToLight, SHADER_UNIFORM_VEC3);
        SetShaderValue(lightingShader_, viewPositionLocation_, &camera_.position,
                       SHADER_UNIFORM_VEC3);
    }

    if (!publishedWorld_.HasParts()) {
        // Keep the guest and metadata-only test scene readable when no Studio
        // map has been published for this world id.
        DrawCubeV(Vector3{0.0f, -kGroundThickness * 0.5f, 0.0f},
                  Vector3{kGroundHalfExtent * 2.0f, kGroundThickness,
                          kGroundHalfExtent * 2.0f}, Color{126, 164, 139, 255});
    } else {
        for (const auto &part : publishedWorld_.Parts()) {
            if (!part.visible) continue;
            const Vector3 size{std::max(0.01f, part.size.x), std::max(0.01f, part.size.y),
                               std::max(0.01f, part.size.z)};
            if (part.kind == "sphere") {
                DrawSphere(part.position, std::max({size.x, size.y, size.z}) * 0.5f, part.color);
            } else {
                DrawCubeV(part.position, size, part.color);
            }
        }
    }

    const Color characterTint = PlayerColorForSession(networkClient_.SessionId(), playerColorSlots_);
    DrawAvatar(playerPosition, localAppearance, characterTint, avatarAssets_,
               characterModelsReady_ ? &characterSphereModel_ : nullptr, playerFacingYaw_,
               localWalking_, localAirborne_, localAnimationTimeSeconds_);

    // Nameplates are textured billboards. Switch back to Raylib's default
    // texture shader after the lit world geometry so their glyph texture is sampled.
    if (lightingShader_.id != 0) EndShaderMode();

    const float characterHalfHeight = Player::kHalfHeightOfCylinder + Player::kRadius;
    JPH::RVec3 shadowGround;
    const Vector3 localFeet{playerPosition.x, playerPosition.y - characterHalfHeight,
                            playerPosition.z};
    if (physicsWorld_.RaycastGroundAt(JPH::RVec3(localFeet.x, localFeet.y, localFeet.z),
                                      80.0f, shadowGround)) {
        avatarAssets_.DrawGroundShadow(
            Vector3{static_cast<float>(shadowGround.GetX()), static_cast<float>(shadowGround.GetY()),
                    static_cast<float>(shadowGround.GetZ())},
            localFeet, localAppearance, playerFacingYaw_, NormalizeVector(kSunToLight));
    }

    // The rendered head's top is at playerY + 1.29. Keep the billboard's
    // center high enough that its lower edge clears the head instead of
    // clipping through it from the camera's angle.
    const Vector3 localNamePosition{playerPosition.x, playerPosition.y + 1.62f,
                                    playerPosition.z};
    const auto drawNameplate = [this, font](const std::string &name, bool verified, Vector3 position) {
        if (name.empty()) return;
        const std::string textureKey = name + (verified ? "\x1fverified" : "\x1fplain");
        auto textureIt = nameplateTextures_.find(textureKey);
        if (textureIt == nameplateTextures_.end()) {
            Texture2D texture = CreateNameplateTextureSafe(font, name.c_str(), verified);
            textureIt = nameplateTextures_.emplace(textureKey, texture).first;
        }
        const Texture2D texture = textureIt->second;
        if (texture.id == 0 || texture.height <= 0) return;

        // Each label is a texture billboard positioned in world space, so it
        // stays above its player and faces the active 3D camera.
        constexpr float labelHeight = 0.56f;
        const float labelWidth = labelHeight * static_cast<float>(texture.width) /
                                 static_cast<float>(texture.height);
        const Rectangle source{0.0f, 0.0f, static_cast<float>(texture.width),
                               static_cast<float>(texture.height)};
        DrawBillboardProSafe(&camera_, texture, source, position,
                             Vector3{0.0f, 1.0f, 0.0f},
                             Vector2{labelWidth, labelHeight},
                             Vector2{labelWidth * 0.5f, labelHeight * 0.5f}, 0.0f, WHITE);
    };

    struct Nameplate {
        std::string name;
        bool verified;
        Vector3 position;
        float cameraDistanceSquared;
    };
    std::vector<Nameplate> nameplates;
    nameplates.reserve(networkClient_.Players().size() + 1);
    const auto distanceSquaredFromCamera = [this](Vector3 position) {
        const float x = position.x - camera_.position.x;
        const float y = position.y - camera_.position.y;
        const float z = position.z - camera_.position.z;
        return x * x + y * y + z * z;
    };
    nameplates.push_back(Nameplate{localDisplayName, localVerified, localNamePosition,
                                   distanceSquaredFromCamera(localNamePosition)});

    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (networkClient_.SessionId() != 0 && remote.sessionId == networkClient_.SessionId()) {
            continue;
        }

        const auto smoothedIt = smoothedPlayers_.find(remote.sessionId);
        if (smoothedIt == smoothedPlayers_.end() || !smoothedIt->second.initialized) continue;
        const SmoothedPlayer &smoothed = smoothedIt->second;
        NetworkPlayerSnapshot renderedRemote = remote;
        renderedRemote.x = smoothed.x;
        renderedRemote.y = smoothed.y;
        renderedRemote.z = smoothed.z;
        renderedRemote.yaw = smoothed.yaw;

        const Vector3 remotePosition{renderedRemote.x, renderedRemote.y, renderedRemote.z};
        const Color remoteTint = PlayerColorForSession(remote.sessionId, playerColorSlots_);
        DrawAvatar(remotePosition, renderedRemote, remoteTint, avatarAssets_,
                   characterModelsReady_ ? &characterSphereModel_ : nullptr,
                   renderedRemote.yaw, smoothed.walking, smoothed.airborne,
                   smoothed.animationTimeSeconds);

        const Vector3 remoteFeet{renderedRemote.x, renderedRemote.y - characterHalfHeight,
                                 renderedRemote.z};
        JPH::RVec3 remoteGround;
        if (physicsWorld_.RaycastGroundAt(JPH::RVec3(remoteFeet.x, remoteFeet.y, remoteFeet.z),
                                          80.0f, remoteGround)) {
            avatarAssets_.DrawGroundShadow(
                Vector3{static_cast<float>(remoteGround.GetX()),
                        static_cast<float>(remoteGround.GetY()),
                        static_cast<float>(remoteGround.GetZ())},
                remoteFeet, renderedRemote, renderedRemote.yaw,
                NormalizeVector(kSunToLight));
        }

        const Vector3 labelPosition{renderedRemote.x, renderedRemote.y + 1.62f,
                                    renderedRemote.z};
        nameplates.push_back(Nameplate{remote.displayName, remote.isVerified, labelPosition,
                                       distanceSquaredFromCamera(labelPosition)});
    }

    // Billboard textures include transparent pixels around the glyphs. Keep
    // depth testing against the scene, but don't let those transparent pixels
    // write depth and punch rectangular holes through characters or other
    // nameplates. Draw far labels first so nearby names remain visually on top.
    std::stable_sort(nameplates.begin(), nameplates.end(),
                     [](const Nameplate &a, const Nameplate &b) {
                         return a.cameraDistanceSquared > b.cameraDistanceSquared;
                     });
    rlDisableDepthMask();
    for (const Nameplate &nameplate : nameplates) {
        drawNameplate(nameplate.name, nameplate.verified, nameplate.position);
    }
    rlEnableDepthMask();

    EndMode3D();

    DrawChatOverlay(font);

    // Keep the HUD intentionally quiet: one small FPS number in the top-right.
    const char *fpsText = TextFormat("%d", GetFPS());
    constexpr float hudFontSize = 18.0f;
    constexpr float hudSpacing = 0.5f;
    const Vector2 fpsSize = MeasureTextExSafe(font, fpsText, hudFontSize, hudSpacing);
    const Vector2 fpsPosition{
        static_cast<float>(GetScreenWidth()) - 20.0f - fpsSize.x,
        16.0f
    };
    DrawTextExSafe(font, fpsText, fpsPosition, hudFontSize, hudSpacing, kHudColor);

    if (mouseCaptured_) {
        const int centerX = GetScreenWidth() / 2;
        const int centerY = GetScreenHeight() / 2;
        DrawCircleLines(centerX, centerY, 7.0f, Color{224, 238, 248, 150});
        DrawLine(centerX - 12, centerY, centerX - 5, centerY, Color{224, 238, 248, 150});
        DrawLine(centerX + 5, centerY, centerX + 12, centerY, Color{224, 238, 248, 150});
        DrawLine(centerX, centerY - 12, centerX, centerY - 5, Color{224, 238, 248, 150});
        DrawLine(centerX, centerY + 5, centerX, centerY + 12, Color{224, 238, 248, 150});
    }
    publishedWorld_.DrawGui(GetScreenWidth(), GetScreenHeight(), font);
    if (networkClient_.HasDisconnectNotice()) {
        DrawDisconnectDialog(font, networkClient_.DisconnectReason());
    }
    EndDrawing();
}

void GameClient::DrawChatOverlay(const Font *font) {
    const auto &messages = networkClient_.ChatMessages();
    const bool showStatus = networkClient_.ChatStatusVisible(GetTime());
    const Rectangle button = ChatButtonBounds();
    const bool buttonHovered = CheckCollisionPointRec(GetMousePosition(), button);
    DrawRectangleRounded(button, 0.16f, 6,
        buttonHovered ? Color{34, 43, 53, 242} : Color{18, 24, 31, 230});
    DrawRectangleRoundedLines(button, 0.16f, 6,
        buttonHovered ? Color{126, 157, 181, 255} : Color{77, 91, 104, 255});
    if (chatIconTexture_.id != 0) {
        const Rectangle source{0.0f, 0.0f, static_cast<float>(chatIconTexture_.width),
                               static_cast<float>(chatIconTexture_.height)};
        const Rectangle destination{button.x + 10.0f, button.y + 12.0f, 25.0f, 20.0f};
        DrawTexturePro(chatIconTexture_, source, destination, Vector2{0.0f, 0.0f},
                       0.0f, WHITE);
    }
    DrawTextExSafe(font, "Chat", Vector2{button.x + 43.0f, button.y + 12.0f},
                   16.0f, 0.3f, Color{241, 245, 248, 255});
    if (buttonHovered) {
        const char *tooltip = chatPanelVisible_ ? "Hide chat" : "Show chat";
        DrawTextExSafe(font, tooltip,
                       Vector2{button.x + button.width + 10.0f, button.y + 13.0f},
                       13.0f, 0.2f, Color{218, 226, 233, 255});
    }
    if (!chatPanelVisible_) return;

    const Rectangle panel = ChatPanelBounds();
    DrawRectangleRounded(panel, 0.045f, 8, Color{12, 17, 24, 224});
    DrawRectangleRoundedLines(panel, 0.045f, 8, Color{81, 99, 115, 230});
    DrawTextExSafe(font, "Chat", Vector2{panel.x + 16.0f, panel.y + 13.0f},
                   20.0f, 0.4f, Color{245, 248, 250, 255});

    const char *chatState = networkClient_.SessionId() != 0 ? "CONNECTED" : "OFFLINE";
    const Color stateColor = networkClient_.SessionId() != 0
        ? Color{122, 203, 151, 255} : Color{167, 178, 188, 255};
    const Vector2 stateSize = MeasureTextExSafe(font, chatState, 12.0f, 0.5f);
    DrawTextExSafe(font, chatState,
                   Vector2{panel.x + panel.width - 16.0f - stateSize.x, panel.y + 18.0f},
                   12.0f, 0.5f, stateColor);
    DrawLine(static_cast<int>(panel.x + 14.0f), static_cast<int>(panel.y + 42.0f),
             static_cast<int>(panel.x + panel.width - 14.0f),
             static_cast<int>(panel.y + 42.0f), Color{52, 63, 74, 255});

    constexpr float messageFontSize = 15.0f;
    constexpr float messageSpacing = 0.3f;
    constexpr float messageLineHeight = 25.0f;
    constexpr std::size_t visibleMessages = 8;
    const std::size_t firstMessage = messages.size() > visibleMessages
        ? messages.size() - visibleMessages : 0;
    float lineY = panel.y + 52.0f;
    if (firstMessage == messages.size()) {
        const char *emptyText = networkClient_.SessionId() != 0
            ? "No messages yet. Press / to open chat."
            : "Join a server to chat.";
        DrawTextExSafe(font, emptyText, Vector2{panel.x + 16.0f, lineY}, 14.0f, 0.3f,
                       Color{162, 174, 185, 255});
    } else {
        for (std::size_t index = firstMessage; index < messages.size(); ++index) {
            const ChatMessageSnapshot &message = messages[index];
            bool senderVerified = false;
            if (message.senderSessionId == networkClient_.SessionId()) {
                senderVerified = networkClient_.UserVerified();
            } else {
                const auto sender = std::find_if(
                    networkClient_.Players().begin(), networkClient_.Players().end(),
                    [&message](const NetworkPlayerSnapshot &player) {
                        return player.sessionId == message.senderSessionId;
                    });
                senderVerified = sender != networkClient_.Players().end() && sender->isVerified;
            }

            const float nameX = panel.x + 16.0f;
            const float maxNameWidth = std::min(150.0f, panel.width * 0.36f);
            const std::string fittedName = FitChatLine(
                font, message.displayName, maxNameWidth, messageFontSize, messageSpacing);
            DrawTextExSafe(font, fittedName.c_str(), Vector2{nameX, lineY},
                           messageFontSize, messageSpacing, Color{231, 238, 248, 240});
            float textX = nameX +
                MeasureTextExSafe(font, fittedName.c_str(), messageFontSize, messageSpacing).x + 4.0f;
            if (senderVerified && verifiedBadgeTexture_.id != 0) {
                constexpr float badgeSize = 14.0f;
                const Rectangle source{0.0f, 0.0f,
                    static_cast<float>(verifiedBadgeTexture_.width),
                    static_cast<float>(verifiedBadgeTexture_.height)};
                const Rectangle destination{textX, lineY + 1.0f, badgeSize, badgeSize};
                DrawTexturePro(verifiedBadgeTexture_, source, destination,
                               Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                textX += badgeSize + 4.0f;
            }
            const Vector2 colonSize = MeasureTextExSafe(font, ": ", messageFontSize,
                                                        messageSpacing);
            DrawTextExSafe(font, ": ", Vector2{textX, lineY}, messageFontSize,
                           messageSpacing, Color{231, 238, 248, 240});
            textX += colonSize.x;
            const std::string fittedText = FitChatLine(
                font, message.text, std::max(24.0f, panel.x + panel.width - 14.0f - textX),
                messageFontSize, messageSpacing);
            DrawTextExSafe(font, fittedText.c_str(), Vector2{textX, lineY},
                           messageFontSize, messageSpacing, Color{231, 238, 248, 240});
            lineY += messageLineHeight;
        }
    }

    if (showStatus) {
        DrawTextExSafe(font, networkClient_.ChatStatus().c_str(),
                       Vector2{panel.x + 16.0f, panel.y + panel.height - 78.0f},
                       14.0f, 0.3f, Color{255, 190, 126, 245});
    }
    const Rectangle input = ChatInputBounds();
    DrawRectangleRounded(input, 0.12f, 6,
        chatInputActive_ ? Color{20, 29, 39, 255} : Color{17, 24, 33, 240});
    DrawRectangleRoundedLines(input, 0.12f, 6,
        chatInputActive_ ? Color{106, 177, 210, 255} : Color{65, 78, 91, 220});
    const std::string inputLine = chatInputActive_
        ? "> " + chatInput_ + "_"
        : !chatInput_.empty() ? "Draft: " + chatInput_
        : networkClient_.SessionId() != 0 ? "Press / to chat"
                                          : "Join a server to chat";
    const std::string fittedInput = FitChatLine(
        font, inputLine, input.width - 24.0f, 16.0f, 0.3f);
    DrawTextExSafe(font, fittedInput.c_str(),
                   Vector2{input.x + 12.0f, input.y + 10.0f},
                   16.0f, 0.3f,
                   chatInputActive_ ? Color{129, 214, 255, 255}
                                    : Color{156, 176, 199, 220});
}
