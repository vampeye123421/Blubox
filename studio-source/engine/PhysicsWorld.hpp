#pragma once
#include "EngineCore.hpp"
#include <unordered_map>
#include <memory>
#include <vector>
namespace blubox::engine {
struct TouchEvent {
  InstanceHandle first{};
  InstanceHandle second{};
};
struct BodyState {
  Vec3 velocity{}, force{}, impulse{};
  float mass = 1;
  bool anchored = true, canCollide = true;
};
struct HumanoidState {
  float walkSpeed = 6, jumpSpeed = 8, gravity = 19.62f;
  Vec3 velocity{};
  bool grounded = false;
  enum class State { Idle, Running, Jumping, Falling, SlopeSliding } state = State::Idle;
};
struct MoveInput {
  float x = 0, z = 0;
  float yaw = 0.0f;
  bool jump = false;
  bool sprint = false;
  bool worldSpaceVelocity = false;
};
class PhysicsWorld {
 public:
  explicit PhysicsWorld(InstanceRegistry& registry);
  ~PhysicsWorld();
  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;
  bool addBody(InstanceHandle handle, float mass = 1, bool anchored = true);
  std::size_t bodyCount() const noexcept;
  void removeBody(InstanceHandle handle);
  bool applyForce(InstanceHandle handle, Vec3 force);
  bool applyImpulse(InstanceHandle handle, Vec3 impulse);
  bool addHumanoid(InstanceHandle root, float walkSpeed = 6, float jumpSpeed = 8, float gravity = 19.62f);
  void setInput(InstanceHandle root, MoveInput input);
  void setGravity(Vec3 gravity);
  void step(double deltaSeconds);
  // Drains contact transitions collected by Jolt's worker callbacks.
  // Call on the simulation/VM owner thread after step().
  std::vector<TouchEvent> takeTouchEvents();
  const BodyState* body(InstanceHandle h) const;
  const HumanoidState* humanoid(InstanceHandle h) const;

 private:
  struct Impl;
  struct Entry {
    InstanceHandle handle;
    BodyState body;
  };
  InstanceRegistry& registry_;
  std::unique_ptr<Impl> impl_;
  std::unordered_map<InstanceId, Entry> bodies_;
  std::unordered_map<InstanceId, Vec3> lastPhysicsPositions_;
  std::unordered_map<InstanceId, HumanoidState> humanoids_;
  std::unordered_map<InstanceId, MoveInput> inputs_;
  Vec3 gravity_{0, -19.62f, 0};
  double accumulator_ = 0;
  static constexpr double kFixedStep = 1.0 / 60.0;
};
}  // namespace blubox::engine
