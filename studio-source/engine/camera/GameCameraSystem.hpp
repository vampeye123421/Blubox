#pragma once
#include "GameCameraComponent.hpp"
#include "../input/InputCommand.hpp"

namespace blubox::engine::camera {
class GameCameraSystem {
 public:
  static void update(GameCameraComponent& camera, Vec3 characterPosition, Vec3 characterVelocity,
                     const input::InputCommand& input, float deltaSeconds);
};
}
