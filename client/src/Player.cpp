#include "Player.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMoveSpeed = 7.0f;
constexpr float kHorizontalAcceleration = 20.0f;

} // namespace

Player::Player(PhysicsWorld &physicsWorld) : physicsWorld_(physicsWorld) {
    bodyId_ = physicsWorld_.CreateDynamicCapsule(JPH::RVec3(0.0, 3.0, 0.0),
                                                  kHalfHeightOfCylinder, kRadius, 75.0f);
}

void Player::Update(float deltaSeconds, float cameraYaw) {
    float forwardInput = 0.0f;
    float rightInput = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) forwardInput += 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) forwardInput -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) rightInput += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) rightInput -= 1.0f;

    const float inputLength = std::sqrt(forwardInput * forwardInput + rightInput * rightInput);
    if (inputLength > 1.0f) {
        forwardInput /= inputLength;
        rightInput /= inputLength;
    }

    const float forwardX = std::sin(cameraYaw);
    const float forwardZ = -std::cos(cameraYaw);
    const float rightX = std::cos(cameraYaw);
    const float rightZ = std::sin(cameraYaw);
    const float targetX = (forwardX * forwardInput + rightX * rightInput) * kMoveSpeed;
    const float targetZ = (forwardZ * forwardInput + rightZ * rightInput) * kMoveSpeed;

    const JPH::Vec3 currentVelocity = physicsWorld_.GetLinearVelocity(bodyId_);
    const float blend = std::min(1.0f, kHorizontalAcceleration * std::max(0.0f, deltaSeconds));
    const float velocityX = currentVelocity.GetX() + (targetX - currentVelocity.GetX()) * blend;
    const float velocityZ = currentVelocity.GetZ() + (targetZ - currentVelocity.GetZ()) * blend;
    // The floor's top is y=0, so the capsule center is grounded at its half-height.
    // Require downward/near-zero vertical velocity to prevent air-jumps.
    const JPH::Vec3 currentPosition = physicsWorld_.GetPosition(bodyId_);
    const float capsuleHalfHeight = kHalfHeightOfCylinder + kRadius;
    float velocityY = currentVelocity.GetY();
    const bool grounded = velocityY <= 0.5f &&
        currentPosition.GetY() <= capsuleHalfHeight + 0.08f;
    if (grounded && IsKeyPressed(KEY_SPACE)) velocityY = kJumpSpeed;

    // Preserve Jolt's vertical velocity except when starting a grounded jump.
    physicsWorld_.SetLinearVelocity(bodyId_, JPH::Vec3(velocityX, velocityY, velocityZ));
}

JPH::Vec3 Player::Position() {
    return physicsWorld_.GetPosition(bodyId_);
}
