#include "PhysicsWorld.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/EPhysicsUpdateError.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace {

constexpr JPH::ObjectLayer kNonMovingLayer = 0;
constexpr JPH::ObjectLayer kMovingLayer = 1;
constexpr JPH::BroadPhaseLayer kNonMovingBroadPhaseLayer(0);
constexpr JPH::BroadPhaseLayer kMovingBroadPhaseLayer(1);
constexpr JPH::uint kMaxBodies = 16384;
constexpr JPH::uint kMaxBodyPairs = 16384;
constexpr JPH::uint kMaxContactConstraints = 16384;
constexpr float kFixedTimeStep = 1.0f / 120.0f;
constexpr int kMaxStepsPerFrame = 8;

JPH::uint GetWorkerThreadCount() {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const unsigned int availableWorkers = hardwareThreads > 1 ? hardwareThreads - 1 : 1;
    return static_cast<JPH::uint>(std::clamp(availableWorkers, 1u, 4u));
}

} // namespace

PhysicsWorld::PhysicsWorld() {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    joltRegistered_ = true;

    tempAllocator_ = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
    jobSystem_ = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, GetWorkerThreadCount());
    physicsSystem_ = std::make_unique<JPH::PhysicsSystem>();
    physicsSystem_->Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints,
                         broadPhaseLayers_, objectVsBroadPhaseFilter_, objectLayerPairFilter_);
    physicsSystem_->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));
}

PhysicsWorld::~PhysicsWorld() {
    if (physicsSystem_ != nullptr) {
        JPH::BodyInterface &bodyInterface = physicsSystem_->GetBodyInterface();
        for (const JPH::BodyID bodyId : ownedBodies_) {
            bodyInterface.RemoveBody(bodyId);
            bodyInterface.DestroyBody(bodyId);
        }
    }

    physicsSystem_.reset();
    jobSystem_.reset();
    tempAllocator_.reset();

    if (joltRegistered_) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

JPH::uint PhysicsWorld::BroadPhaseLayers::GetNumBroadPhaseLayers() const {
    return 2;
}

JPH::BroadPhaseLayer PhysicsWorld::BroadPhaseLayers::GetBroadPhaseLayer(JPH::ObjectLayer layer) const {
    return layer == kNonMovingLayer ? kNonMovingBroadPhaseLayer : kMovingBroadPhaseLayer;
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char *PhysicsWorld::BroadPhaseLayers::GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const {
    return layer == kNonMovingBroadPhaseLayer ? "NON_MOVING" : "MOVING";
}
#endif

bool PhysicsWorld::ObjectVsBroadPhaseFilter::ShouldCollide(
    JPH::ObjectLayer objectLayer, JPH::BroadPhaseLayer broadPhaseLayer) const {
    if (objectLayer == kNonMovingLayer) {
        return broadPhaseLayer == kMovingBroadPhaseLayer;
    }
    return true;
}

bool PhysicsWorld::ObjectLayerPairFilter::ShouldCollide(
    JPH::ObjectLayer first, JPH::ObjectLayer second) const {
    return first == kMovingLayer || second == kMovingLayer;
}

JPH::BodyID PhysicsWorld::CreateStaticBox(const JPH::RVec3 &position, const JPH::Vec3 &halfExtents) {
    JPH::Ref<JPH::BoxShape> shape = new JPH::BoxShape(halfExtents);
    JPH::BodyCreationSettings settings(shape, position, JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Static, kNonMovingLayer);
    JPH::BodyInterface &bodyInterface = physicsSystem_->GetBodyInterface();
    const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (bodyId.IsInvalid()) {
        throw std::runtime_error("Jolt could not create the static ground body.");
    }
    ownedBodies_.push_back(bodyId);
    return bodyId;
}

JPH::BodyID PhysicsWorld::CreateDynamicCapsule(const JPH::RVec3 &position,
                                                float halfHeightOfCylinder,
                                                float radius, float mass) {
    JPH::Ref<JPH::CapsuleShape> shape = new JPH::CapsuleShape(halfHeightOfCylinder, radius);
    JPH::BodyCreationSettings settings(shape, position, JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Dynamic, kMovingLayer);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;
    // Keep the temporary player upright while leaving all three translations dynamic.
    settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                            JPH::EAllowedDOFs::TranslationY |
                            JPH::EAllowedDOFs::TranslationZ;

    JPH::BodyInterface &bodyInterface = physicsSystem_->GetBodyInterface();
    const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (bodyId.IsInvalid()) {
        throw std::runtime_error("Jolt could not create the player's dynamic body.");
    }
    ownedBodies_.push_back(bodyId);
    return bodyId;
}

JPH::BodyID PhysicsWorld::CreateKinematicCapsule(const JPH::RVec3 &position,
                                                  float halfHeightOfCylinder,
                                                  float radius) {
    JPH::Ref<JPH::CapsuleShape> shape = new JPH::CapsuleShape(halfHeightOfCylinder, radius);
    JPH::BodyCreationSettings settings(shape, position, JPH::Quat::sIdentity(),
                                       JPH::EMotionType::Kinematic, kMovingLayer);
    settings.mFriction = 0.0f;
    settings.mRestitution = 0.0f;

    JPH::BodyInterface &bodyInterface = physicsSystem_->GetBodyInterface();
    const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (bodyId.IsInvalid()) {
        throw std::runtime_error("Jolt could not create a remote player's collision body.");
    }
    ownedBodies_.push_back(bodyId);
    return bodyId;
}

void PhysicsWorld::MoveKinematicBody(JPH::BodyID bodyId, const JPH::RVec3 &position,
                                     float deltaSeconds) {
    const float safeDelta = std::clamp(deltaSeconds, 0.001f, 0.1f);
    physicsSystem_->GetBodyInterface().MoveKinematic(
        bodyId, position, JPH::Quat::sIdentity(), safeDelta);
}

void PhysicsWorld::DestroyBody(JPH::BodyID bodyId) {
    if (bodyId.IsInvalid()) return;

    JPH::BodyInterface &bodyInterface = physicsSystem_->GetBodyInterface();
    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
    std::erase(ownedBodies_, bodyId);
}

JPH::Vec3 PhysicsWorld::GetPosition(JPH::BodyID bodyId) {
    const JPH::RVec3 position = physicsSystem_->GetBodyInterface().GetPosition(bodyId);
    return JPH::Vec3(static_cast<float>(position.GetX()),
                     static_cast<float>(position.GetY()),
                     static_cast<float>(position.GetZ()));
}

JPH::Vec3 PhysicsWorld::GetLinearVelocity(JPH::BodyID bodyId) {
    return physicsSystem_->GetBodyInterface().GetLinearVelocity(bodyId);
}

bool PhysicsWorld::RaycastGround(JPH::BodyID bodyId, float capsuleHalfHeight,
                                 float maxDrop, JPH::RVec3& point) const {
    if (!physicsSystem_ || bodyId.IsInvalid() || !std::isfinite(capsuleHalfHeight) ||
        !std::isfinite(maxDrop) || capsuleHalfHeight <= 0.0f || maxDrop <= 0.0f) return false;

    const JPH::RVec3 center = physicsSystem_->GetBodyInterface().GetPosition(bodyId);
    constexpr float kFootProbeLift = 0.06f;
    const JPH::RVec3 origin(center.GetX(), center.GetY() - capsuleHalfHeight + kFootProbeLift,
                            center.GetZ());
    const JPH::RRayCast ray(origin, JPH::Vec3(0.0f, -maxDrop, 0.0f));
    JPH::RayCastResult hit;
    const JPH::IgnoreSingleBodyFilter ignorePlayer(bodyId);
    const auto broadPhase = physicsSystem_->GetDefaultBroadPhaseLayerFilter(kMovingLayer);
    const auto objectLayers = physicsSystem_->GetDefaultLayerFilter(kMovingLayer);
    if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, hit, broadPhase, objectLayers,
                                                       ignorePlayer)) return false;
    point = ray.GetPointOnRay(hit.mFraction);
    return true;
}

bool PhysicsWorld::RaycastGroundAt(const JPH::RVec3& feetPosition, float maxDrop,
                                  JPH::RVec3& point) const {
    if (!physicsSystem_ || !std::isfinite(maxDrop) || maxDrop <= 0.0f) return false;
    constexpr float kFootProbeLift = 0.04f;
    const JPH::RVec3 origin(feetPosition.GetX(), feetPosition.GetY() + kFootProbeLift,
                            feetPosition.GetZ());
    const JPH::RRayCast ray(origin, JPH::Vec3(0.0f, -(maxDrop + kFootProbeLift), 0.0f));
    JPH::RayCastResult hit;
    const auto broadPhase = physicsSystem_->GetDefaultBroadPhaseLayerFilter(kMovingLayer);
    const auto objectLayers = physicsSystem_->GetDefaultLayerFilter(kMovingLayer);
    if (!physicsSystem_->GetNarrowPhaseQuery().CastRay(ray, hit, broadPhase, objectLayers))
        return false;
    point = ray.GetPointOnRay(hit.mFraction);
    return true;
}

void PhysicsWorld::SetLinearVelocity(JPH::BodyID bodyId, const JPH::Vec3 &velocity) {
    physicsSystem_->GetBodyInterface().SetLinearVelocity(bodyId, velocity);
}

void PhysicsWorld::ActivateBody(JPH::BodyID bodyId) {
    physicsSystem_->GetBodyInterface().ActivateBody(bodyId);
}

void PhysicsWorld::Step(float frameDeltaSeconds) {
    const float clampedFrameTime = std::clamp(frameDeltaSeconds, 0.0f, 0.1f);
    accumulatedTime_ = std::min(accumulatedTime_ + clampedFrameTime,
                                kFixedTimeStep * static_cast<float>(kMaxStepsPerFrame));

    int steps = 0;
    while (accumulatedTime_ >= kFixedTimeStep && steps < kMaxStepsPerFrame) {
        const JPH::EPhysicsUpdateError errors =
            physicsSystem_->Update(kFixedTimeStep, 1, tempAllocator_.get(), jobSystem_.get());
        if (errors != JPH::EPhysicsUpdateError::None) {
            throw std::runtime_error("Jolt ran out of contact or body-pair capacity.");
        }
        accumulatedTime_ -= kFixedTimeStep;
        ++steps;
    }
}
