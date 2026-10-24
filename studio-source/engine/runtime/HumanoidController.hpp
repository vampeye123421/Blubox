#pragma once
#include "HumanoidComponent.hpp"
#include "../input/InputCommand.hpp"

namespace blubox::engine::runtime {
class HumanoidController {
 public:
  // Computes a bounded, camera-relative command. CharacterVirtual performs the
  // collision movement in PhysicsWorld's fixed tick.
  static Vec3 desiredVelocity(HumanoidComponent& humanoid, const input::InputCommand& command);
};
}
