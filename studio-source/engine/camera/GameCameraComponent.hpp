#pragma once
#include "../EngineCore.hpp"
#include <cstdint>

namespace blubox::engine::camera {
enum class GameCameraMode : std::uint8_t { FirstPerson, ThirdPersonOrbit };
struct GameCameraComponent {
  GameCameraMode mode = GameCameraMode::ThirdPersonOrbit;
  Vec3 targetPosition{}, targetVelocity{}, position{}, forward{0,0,-1};
  // Match the standalone client: yaw 0 faces -Z, positive pitch raises the
  // orbit camera (and therefore looks down toward the character).
  float yaw = 0.0f, pitch = 0.28f;
  float distance = 7.0f, minimumDistance = 1.5f, maximumDistance = 20.0f;
  float fieldOfView = 60.0f, mouseSensitivity = 0.003f, zoomSpeed = 1.0f, followSharpness = 18.0f;
  bool active = false, initialized = false;
};
}
