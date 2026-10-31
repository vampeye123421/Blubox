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
    void UpdateChatInput();
    void OpenChatInput();
    void CloseChatInput();
    void UpdateCamera();
    void SyncRemotePlayers(float deltaSeconds);
    void DrawChatOverlay(const Font *font);
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
    Texture2D verifiedBadgeTexture_{};
    Texture2D chatIconTexture_{};
    Font uiFont_{};
    std::unordered_map<std::string, Texture2D> nameplateTextures_;
    std::string chatInput_;
    std::unordered_map<std::uint64_t, std::size_t> playerColorSlots_;
    std::unordered_map<std::uint64_t, JPH::BodyID> remotePlayerBodies_;
    struct SmoothedPlayer {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float animationTimeSeconds = 0.0f;
        bool walking = false;
        bool airborne = false;
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
    float localAnimationTimeSeconds_ = 0.0f;
    bool localWalking_ = false;
    bool localAirborne_ = false;
    bool localAnimationInitialized_ = false;
    float cameraPitch_ = 0.28f;
    float cameraDistance_ = 7.0f;
    bool mouseCaptured_ = true;
    bool shiftLockEnabled_ = false;
    bool shiftLockTapPending_ = false;
    bool disconnectUiActive_ = false;
    bool chatInputActive_ = false;
    bool chatPanelVisible_ = true;
    bool chatReturnToShiftLock_ = false;
    bool chatReturnMouseCaptured_ = false;
    bool running_ = true;
};
