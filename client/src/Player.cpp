#include "Player.hpp"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMoveSpeed = 7.0f;
constexpr float kHorizontalAcceleration = 20.0f;
constexpr float kJumpBufferDuration = 0.18f;
constexpr float kCoyoteDuration = 0.12f;
constexpr float kGroundTolerance = 0.24f;

} // namespace

Player::Player(PhysicsWorld &physicsWorld, const JPH::RVec3 &spawnPosition) : physicsWorld_(physicsWorld) {
    bodyId_ = physicsWorld_.CreateDynamicCapsule(spawnPosition,
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
    // Buffer a short Space press and allow a brief coyote window so input still
    // registers if the key event lands between physics steps or just after takeoff.
    if (IsKeyPressed(KEY_SPACE)) jumpBufferSeconds_ = kJumpBufferDuration;
    else jumpBufferSeconds_ = std::max(0.0f, jumpBufferSeconds_ - deltaSeconds);

    const float capsuleHalfHeight = kHalfHeightOfCylinder + kRadius;
    float velocityY = currentVelocity.GetY();
    JPH::RVec3 groundPoint;
    // Studio maps can place their baseplate and SpawnLocation at any height.
    // Test the actual collision world below the capsule instead of assuming
    // every game has a floor at world Y=0.
    const bool grounded = velocityY <= 1.0f &&
        physicsWorld_.RaycastGround(bodyId_, capsuleHalfHeight,
                                    kGroundTolerance + 0.10f, groundPoint);
    if (grounded) coyoteSeconds_ = kCoyoteDuration;
    else coyoteSeconds_ = std::max(0.0f, coyoteSeconds_ - deltaSeconds);

    if (jumpBufferSeconds_ > 0.0f && coyoteSeconds_ > 0.0f) {
        velocityY = kJumpSpeed;
        jumpBufferSeconds_ = 0.0f;
        coyoteSeconds_ = 0.0f;
        physicsWorld_.ActivateBody(bodyId_);
    }

    // Preserve Jolt's vertical velocity except when starting a grounded jump.
    physicsWorld_.SetLinearVelocity(bodyId_, JPH::Vec3(velocityX, velocityY, velocityZ));
}

JPH::Vec3 Player::Position() {
    return physicsWorld_.GetPosition(bodyId_);
}
