#include "GameClient.hpp"
#include "BluboxBrandIcon.hpp"
#include "BluboxUiFont.hpp"
#include "RaylibCompat.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
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
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragNormal;
out vec3 fragPosition;
out vec4 fragColor;
uniform vec4 colDiffuse;
void main() {
    fragNormal = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    fragPosition = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fragColor = vertexColor * colDiffuse;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)GLSL";

constexpr const char *kLightingFragmentShader = R"GLSL(
#version 330
in vec3 fragNormal;
in vec3 fragPosition;
in vec4 fragColor;
uniform vec3 sunToLight;
uniform vec3 viewPosition;
uniform float ambientStrength;
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
    vec3 litColor = fragColor.rgb * lighting + vec3(specular);
    finalColor = vec4(litColor, fragColor.a);
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

float SmoothStep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void DrawLitCharacter(const Model *cylinder, const Model *sphere, Vector3 center,
                      float halfCylinderHeight, Color tint) {
    if (cylinder == nullptr || sphere == nullptr) return;

    // The cylinder mesh is generated from y=0 to y=height; sphere centers sit
    // at its ends, matching Jolt's capsule collider dimensions.
    const Vector3 cylinderBottom{center.x, center.y - halfCylinderHeight, center.z};
    const Vector3 lowerCap{center.x, center.y - halfCylinderHeight, center.z};
    const Vector3 upperCap{center.x, center.y + halfCylinderHeight, center.z};
    DrawModelSafe(cylinder, &cylinderBottom, 1.0f, &tint);
    DrawModelSafe(sphere, &lowerCap, 1.0f, &tint);
    DrawModelSafe(sphere, &upperCap, 1.0f, &tint);
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
                            const std::vector<std::uint64_t> &orderedSessions) {
    const auto &palette = PlayerPalette();
    if (sessionId == 0 || orderedSessions.empty()) return palette.front();
    const auto found = std::lower_bound(orderedSessions.begin(), orderedSessions.end(), sessionId);
    if (found == orderedSessions.end() || *found != sessionId) return palette.front();
    const std::size_t playerIndex = static_cast<std::size_t>(found - orderedSessions.begin());
    // Session IDs come from the authoritative server, so every client assigns
    // the same distinct palette slot to each player in a shared server.
    return palette[playerIndex % palette.size()];
}

} // namespace

GameClient::GameClient(LaunchOptions options)
    : options_(std::move(options)),
      session_(options_.protocolLaunch.has_value() ? &*options_.protocolLaunch : nullptr) {}

int GameClient::Run() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(kWindowWidth, kWindowHeight, "Blubox Client - Local Guest");
    if (!IsWindowReady()) {
        return 1;
    }
    Image appIcon = LoadImageFromMemory(".png", blubox::kBrandIconData, blubox::kBrandIconDataSize);
    if (appIcon.data != nullptr) {
        ImageFormat(&appIcon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        SetWindowIcon(appIcon);
        UnloadImage(appIcon);
    }

    if (options_.protocolLaunch.has_value() && options_.protocolLaunch->action == "join") {
        networkClient_.Connect(session_);
    }

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

    physicsWorld_.CreateStaticBox(JPH::RVec3(0.0, -kGroundThickness * 0.5, 0.0),
                                  JPH::Vec3(kGroundHalfExtent, kGroundThickness * 0.5,
                                            kGroundHalfExtent));
    TraceLog(LOG_INFO, "GameClient: Jolt ground created.");
    player_ = std::make_unique<Player>(physicsWorld_);
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
    DisableCursor();
    UpdateCamera();
    TraceLog(LOG_INFO, "GameClient: entering frame loop.");

    while (!WindowShouldClose() && running_) {
        const float deltaSeconds = GetFrameTime();
        Update(deltaSeconds);
        networkClient_.Poll();
        physicsWorld_.Step(deltaSeconds);
        const JPH::Vec3 networkPosition = player_->Position();
        networkClient_.SendPlayerPosition(networkPosition.GetX(), networkPosition.GetY(),
                                          networkPosition.GetZ());
        UpdateCamera();
        DrawFrame();
    }

    EnableCursor();
    if (customFontLoaded_) UnloadFont(uiFont_);
    for (const auto &[name, texture] : nameplateTextures_) {
        (void)name;
        if (texture.id != 0) UnloadTexture(texture);
    }
    nameplateTextures_.clear();
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
    if (IsKeyPressed(KEY_ESCAPE)) {
        running_ = false;
    }
    if (IsKeyPressed(KEY_F11)) {
        ToggleFullscreen();
    }
    if (IsKeyPressed(KEY_TAB)) {
        mouseCaptured_ = !mouseCaptured_;
        if (mouseCaptured_) DisableCursor();
        else EnableCursor();
    } else if (!mouseCaptured_ && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        mouseCaptured_ = true;
        DisableCursor();
    }

    if (mouseCaptured_) {
        const Vector2 mouseDelta = GetMouseDelta();
        cameraYaw_ += mouseDelta.x * kMouseSensitivity;
        cameraPitch_ = std::clamp(cameraPitch_ + mouseDelta.y * kMouseSensitivity,
                                  kMinimumCameraPitch, kMaximumCameraPitch);
    }

    player_->Update(deltaSeconds, cameraYaw_);
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

void GameClient::DrawFrame() {
    const Vector3 playerPosition = ToRaylibVector(player_->Position());
    std::string localDisplayName = session_.HasToken() ? session_.DisplayName() : "Guest";
    bool localVerified = networkClient_.UserVerified();
    std::vector<std::uint64_t> orderedSessions;
    orderedSessions.reserve(networkClient_.Players().size() + 1);
    if (networkClient_.SessionId() != 0) orderedSessions.push_back(networkClient_.SessionId());
    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (remote.sessionId != 0) orderedSessions.push_back(remote.sessionId);
    }
    std::sort(orderedSessions.begin(), orderedSessions.end());
    orderedSessions.erase(std::unique(orderedSessions.begin(), orderedSessions.end()),
                          orderedSessions.end());
    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (networkClient_.SessionId() != 0 && remote.sessionId == networkClient_.SessionId() &&
            !remote.displayName.empty()) {
            localDisplayName = remote.displayName;
            localVerified = remote.isVerified;
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

    // A sunlit blue-green baseplate keeps the scene readable against the sky.
    DrawCubeV(Vector3{0.0f, -kGroundThickness * 0.5f, 0.0f},
              Vector3{kGroundHalfExtent * 2.0f, kGroundThickness,
                      kGroundHalfExtent * 2.0f}, Color{126, 164, 139, 255});

    const Color characterTint = PlayerColorForSession(networkClient_.SessionId(), orderedSessions);
    if (characterModelsReady_) {
        DrawLitCharacter(&characterCylinderModel_, &characterSphereModel_, playerPosition,
                         Player::kHalfHeightOfCylinder, characterTint);
    } else {
        const Vector3 capsuleBottom{ playerPosition.x,
                                     playerPosition.y - Player::kHalfHeightOfCylinder,
                                     playerPosition.z };
        const Vector3 capsuleTop{ playerPosition.x,
                                  playerPosition.y + Player::kHalfHeightOfCylinder,
                                  playerPosition.z };
        DrawCapsule(capsuleBottom, capsuleTop, Player::kRadius, 20, 12, characterTint);
    }

    // Nameplates are textured billboards. Switch back to Raylib's default
    // texture shader after the lit world geometry so their glyph texture is sampled.
    if (lightingShader_.id != 0) EndShaderMode();

    const Vector3 localNamePosition{playerPosition.x,
        playerPosition.y + Player::kHalfHeightOfCylinder + Player::kRadius + 0.18f,
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
    drawNameplate(localDisplayName, localVerified, localNamePosition);

    for (const NetworkPlayerSnapshot &remote : networkClient_.Players()) {
        if (networkClient_.SessionId() != 0 && remote.sessionId == networkClient_.SessionId()) {
            continue;
        }

        const Vector3 remotePosition{remote.x, remote.y, remote.z};
        const Color remoteTint = PlayerColorForSession(remote.sessionId, orderedSessions);
        if (characterModelsReady_) {
            DrawLitCharacter(&characterCylinderModel_, &characterSphereModel_, remotePosition,
                             Player::kHalfHeightOfCylinder, remoteTint);
        } else {
            const Vector3 capsuleBottom{remote.x,
                remote.y - Player::kHalfHeightOfCylinder, remote.z};
            const Vector3 capsuleTop{remote.x,
                remote.y + Player::kHalfHeightOfCylinder, remote.z};
            DrawCapsule(capsuleBottom, capsuleTop, Player::kRadius, 20, 12, remoteTint);
        }

        const Vector3 labelPosition{remote.x,
            remote.y + Player::kHalfHeightOfCylinder + Player::kRadius + 0.18f,
            remote.z};
        drawNameplate(remote.displayName, remote.isVerified, labelPosition);
    }

    EndMode3D();

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
    EndDrawing();
}
