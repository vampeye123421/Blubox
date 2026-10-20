#pragma once

#include "PhysicsWorld.hpp"

class Player {
public:
    explicit Player(PhysicsWorld &physicsWorld);

    void Update(float deltaSeconds, float cameraYaw);
    JPH::Vec3 Position();
    JPH::BodyID BodyId() const { return bodyId_; }

    static constexpr float kRadius = 0.45f;
    static constexpr float kHalfHeightOfCylinder = 0.45f;
    static constexpr float kJumpSpeed = 8.0f;

private:
    PhysicsWorld &physicsWorld_;
    JPH::BodyID bodyId_;
    float jumpBufferSeconds_ = 0.0f;
    float coyoteSeconds_ = 0.0f;
};
