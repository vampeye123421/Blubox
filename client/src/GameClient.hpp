#pragma once

#include "ClientSession.hpp"
#include "LaunchOptions.hpp"
#include "NetworkClient.hpp"
#include "PhysicsWorld.hpp"
#include "Player.hpp"
#include "PublishedWorld.hpp"
#include "LocalScriptRuntime.hpp"
#include "AvatarAssetCache.hpp"

#include <raylib.h>

#include <memory>
#include <string>
#include <unordered_map>

class GameClient {
public:
    explicit GameClient(LaunchOptions options);
    int Run();

private:
    void Update(float deltaSeconds);
    void UpdateCamera();
    void DrawFrame();

    LaunchOptions options_;
    ClientSession session_;
    NetworkClient networkClient_;
    PhysicsWorld physicsWorld_;
    PublishedWorld publishedWorld_;
    LocalScriptRuntime localScriptRuntime_;
    AvatarAssetCache avatarAssets_;
    std::unique_ptr<Player> player_;
    Camera3D camera_{};
    Shader lightingShader_{};
    Model characterCylinderModel_{};
    Model characterSphereModel_{};
    Shader skyboxShader_{};
    Model skyboxModel_{};
    TextureCubemap skyboxCubemap_{};
    Texture2D sunTexture_{};
    Font uiFont_{};
    std::unordered_map<std::string, Texture2D> nameplateTextures_;
    std::unordered_map<std::uint64_t, std::size_t> playerColorSlots_;
    struct SmoothedPlayer {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        bool initialized = false;
    };
    std::unordered_map<std::uint64_t, SmoothedPlayer> smoothedPlayers_;
    int sunToLightLocation_ = -1;
    int viewPositionLocation_ = -1;
    int skyboxEnvironmentLocation_ = -1;
    bool customFontLoaded_ = false;
    bool characterModelsReady_ = false;
    bool skyboxReady_ = false;
    float cameraYaw_ = 0.0f;
    float playerFacingYaw_ = 0.0f;
    float cameraPitch_ = 0.28f;
    float cameraDistance_ = 7.0f;
    bool mouseCaptured_ = true;
    bool shiftLockEnabled_ = false;
    bool disconnectUiActive_ = false;
    bool running_ = true;
};
