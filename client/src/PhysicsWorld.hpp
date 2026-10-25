#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <memory>
#include <vector>

class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld &) = delete;
    PhysicsWorld &operator=(const PhysicsWorld &) = delete;

    JPH::BodyID CreateStaticBox(const JPH::RVec3 &position, const JPH::Vec3 &halfExtents);
    JPH::BodyID CreateDynamicCapsule(const JPH::RVec3 &position, float halfHeightOfCylinder,
                                     float radius, float mass);

    JPH::Vec3 GetPosition(JPH::BodyID bodyId);
    JPH::Vec3 GetLinearVelocity(JPH::BodyID bodyId);
    // Casts down from the capsule's feet, ignoring the player body itself.
    // `maxDrop` is the distance allowed below the capsule before declaring
    // that there is no supporting world surface.
    bool RaycastGround(JPH::BodyID bodyId, float capsuleHalfHeight, float maxDrop,
                       JPH::RVec3& point) const;
    bool RaycastGroundAt(const JPH::RVec3& feetPosition, float maxDrop,
                        JPH::RVec3& point) const;
    void SetLinearVelocity(JPH::BodyID bodyId, const JPH::Vec3 &velocity);
    void ActivateBody(JPH::BodyID bodyId);

    // Simulate at a fixed frequency while safely limiting catch-up work after a hitch.
    void Step(float frameDeltaSeconds);

private:
    class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override;
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override;
#endif
    };

    class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer objectLayer,
                           JPH::BroadPhaseLayer broadPhaseLayer) const override;
    };

    class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override;
    };

    BroadPhaseLayers broadPhaseLayers_;
    ObjectVsBroadPhaseFilter objectVsBroadPhaseFilter_;
    ObjectLayerPairFilter objectLayerPairFilter_;
    std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jobSystem_;
    std::unique_ptr<JPH::PhysicsSystem> physicsSystem_;
    std::vector<JPH::BodyID> ownedBodies_;
    float accumulatedTime_ = 0.0f;
    bool joltRegistered_ = false;
};
