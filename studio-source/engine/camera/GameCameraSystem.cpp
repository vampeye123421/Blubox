#include "GameCameraSystem.hpp"
#include <algorithm>
#include <cmath>

namespace blubox::engine::camera {
void GameCameraSystem::update(GameCameraComponent& camera, Vec3 characterPosition, Vec3 characterVelocity,
                              const input::InputCommand& input, float dt) {
  dt = std::clamp(dt, 0.0f, 0.1f);
  camera.active = true;
  camera.targetVelocity = characterVelocity;
  const float alpha = 1.0f - std::exp(-std::max(0.1f, camera.followSharpness) * dt);
  const Vec3 target{characterPosition.x, characterPosition.y + 0.45f, characterPosition.z};
  if (!camera.initialized) { camera.targetPosition = target; camera.yaw = input.yaw; camera.pitch = input.pitch; camera.initialized = true; }
  camera.targetPosition = {camera.targetPosition.x + (target.x-camera.targetPosition.x)*alpha,
                           camera.targetPosition.y + (target.y-camera.targetPosition.y)*alpha,
                           camera.targetPosition.z + (target.z-camera.targetPosition.z)*alpha};
  // Keep the same mouse convention as GameClient: horizontal motion rotates
  // the look direction positively, and moving the mouse down orbits above the
  // target so the view looks down.
  camera.yaw += input.mouseDeltaX * camera.mouseSensitivity;
  camera.pitch = std::clamp(camera.pitch + input.mouseDeltaY * camera.mouseSensitivity, -0.15f, 0.85f);
  camera.distance = std::clamp(camera.distance - input.mouseWheel * camera.zoomSpeed, camera.minimumDistance, camera.maximumDistance);
  const float cp = std::cos(camera.pitch), sp = std::sin(camera.pitch), sy = std::sin(camera.yaw), cy = std::cos(camera.yaw);
  if (camera.mode == GameCameraMode::FirstPerson) {
    camera.position = camera.targetPosition;
    camera.forward = {sy*cp, -sp, -cy*cp};
  } else {
    // Client convention: yaw 0 faces -Z, so the third-person camera sits
    // behind the character on +Z.
    camera.position = {camera.targetPosition.x - sy*cp*camera.distance,
                       camera.targetPosition.y + sp*camera.distance,
                       camera.targetPosition.z + cy*cp*camera.distance};
    const float dx = camera.targetPosition.x-camera.position.x, dy=camera.targetPosition.y-camera.position.y, dz=camera.targetPosition.z-camera.position.z;
    const float length = std::max(0.0001f, std::sqrt(dx*dx+dy*dy+dz*dz));
    camera.forward = {dx/length,dy/length,dz/length};
  }
}
}
