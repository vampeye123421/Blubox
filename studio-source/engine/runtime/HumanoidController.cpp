#include "HumanoidController.hpp"
#include <algorithm>
#include <cmath>

namespace blubox::engine::runtime {
Vec3 HumanoidController::desiredVelocity(HumanoidComponent& humanoid, const input::InputCommand& command) {
  const float magnitude = std::sqrt(command.moveX * command.moveX + command.moveY * command.moveY);
  const float inputScale = magnitude > 1.0f ? 1.0f / magnitude : 1.0f;
  const float x = command.moveX * inputScale, y = command.moveY * inputScale;
  const float sy = std::sin(command.yaw), cy = std::cos(command.yaw);
  // Match Player::Update in the standalone client. At yaw 0, W moves toward
  // -Z, D moves toward +X, and the camera and movement share the same basis.
  humanoid.desiredDirection = {x * cy + y * sy, 0.0f, x * sy - y * cy};
  const float speed = (command.buttons & input::Sprint) ? humanoid.runSpeed : humanoid.walkSpeed;
  return {humanoid.desiredDirection.x * speed, 0.0f, humanoid.desiredDirection.z * speed};
}
}
