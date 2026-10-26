#pragma once
#include "../EngineCore.hpp"

namespace blubox::engine::runtime {
enum class HumanoidMotionState : std::uint8_t { Idle, Running, Jumping, Falling, SlopeSliding };
struct HumanoidComponent {
  InstanceHandle root{};
  float walkSpeed = 6.0f, runSpeed = 12.0f, jumpSpeed = 8.0f;
  float acceleration = 60.0f, airAcceleration = 20.0f, friction = 10.0f;
  float maxSlopeAngleDegrees = 45.0f, gravity = 19.62f, terminalVelocity = 60.0f;
  bool grounded = false, jumping = false;
  Vec3 velocity{}, desiredDirection{};
  HumanoidMotionState state = HumanoidMotionState::Falling;
};
}
