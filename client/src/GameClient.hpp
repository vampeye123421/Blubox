#pragma once

#include "ClientSession.hpp"
#include "LaunchOptions.hpp"
#include "NetworkClient.hpp"
#include "PhysicsWorld.hpp"
#include "Player.hpp"

#include <raylib.h>

#include <memory>

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
    int sunToLightLocation_ = -1;
    int viewPositionLocation_ = -1;
    int skyboxEnvironmentLocation_ = -1;
    bool customFontLoaded_ = false;
    bool characterModelsReady_ = false;
    bool skyboxReady_ = false;
    float cameraYaw_ = 0.0f;
    float cameraPitch_ = 0.28f;
    float cameraDistance_ = 7.0f;
    bool mouseCaptured_ = true;
    bool running_ = true;
};
