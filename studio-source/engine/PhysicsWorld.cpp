#include "PhysicsWorld.hpp"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace blubox::engine {
namespace {
namespace Layers {
constexpr JPH::ObjectLayer Static = 0, Dynamic = 1, Count = 2;
}
namespace Broad {
constexpr JPH::BroadPhaseLayer Static(0), Dynamic(1);
constexpr unsigned Count = 2;
}  // namespace Broad
class BroadPhaseMap final : public JPH::BroadPhaseLayerInterface {
 public:
  JPH::uint GetNumBroadPhaseLayers() const override { return Broad::Count; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return layer == Layers::Static ? Broad::Static : Broad::Dynamic;
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    return layer == Broad::Static ? "Static" : "Dynamic";
  }
#endif
};
class ObjectVsBroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
    return object == Layers::Dynamic || broad == Broad::Dynamic;
  }
};
class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    return a == Layers::Dynamic || b == Layers::Dynamic;
  }
};
void initializeJolt() {
  static std::once_flag once;
  std::call_once(once, [] {
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  });
}
JPH::Vec3 toJolt(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 fromJolt(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Quat toJoltRotation(Vec3 r) {
  const float cx=std::cos(r.x*0.5f), sx=std::sin(r.x*0.5f), cy=std::cos(r.y*0.5f), sy=std::sin(r.y*0.5f), cz=std::cos(r.z*0.5f), sz=std::sin(r.z*0.5f);
  return {sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz, cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz};
}
Vec3 fromJoltRotation(JPH::QuatArg q) {
  const float sinr=2.0f*(q.GetW()*q.GetX()+q.GetY()*q.GetZ());
  const float cosr=1.0f-2.0f*(q.GetX()*q.GetX()+q.GetY()*q.GetY());
  const float sinp=2.0f*(q.GetW()*q.GetY()-q.GetZ()*q.GetX());
  const float siny=2.0f*(q.GetW()*q.GetZ()+q.GetX()*q.GetY());
  const float cosy=1.0f-2.0f*(q.GetY()*q.GetY()+q.GetZ()*q.GetZ());
  return {std::atan2(sinr,cosr), std::asin(std::clamp(sinp,-1.0f,1.0f)), std::atan2(siny,cosy)};
}

struct HandlePair {
  InstanceHandle first{};
  InstanceHandle second{};
  friend bool operator==(const HandlePair&, const HandlePair&) = default;
};
struct HandlePairHash {
  std::size_t operator()(const HandlePair& pair) const noexcept {
    const std::uint64_t a = (static_cast<std::uint64_t>(pair.first.generation) << 32) | pair.first.id;
    const std::uint64_t b = (static_cast<std::uint64_t>(pair.second.generation) << 32) | pair.second.id;
    const std::uint64_t mixed = a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
    return static_cast<std::size_t>(mixed);
  }
};
HandlePair orderedPair(InstanceHandle a, InstanceHandle b) {
  if (a.id > b.id || (a.id == b.id && a.generation > b.generation)) std::swap(a, b);
  return {a, b};
}

// Contact callbacks can run concurrently on Jolt worker threads. This queue
// coalesces multiple manifold points into one begin event per instance pair.
class TouchEventQueue {
 public:
  void begin(InstanceHandle first, InstanceHandle second) {
    if (!first || !second || first == second) return;
    const HandlePair pair = orderedPair(first, second);
    std::lock_guard lock(mutex_);
    if (active_.insert(pair).second) pending_.push_back({pair.first, pair.second});
  }
  void end(InstanceHandle first, InstanceHandle second) {
    if (!first || !second || first == second) return;
    std::lock_guard lock(mutex_);
    active_.erase(orderedPair(first, second));
  }
  void forget(InstanceHandle instance) {
    std::lock_guard lock(mutex_);
    std::erase_if(active_, [instance](const HandlePair& pair) {
      return pair.first == instance || pair.second == instance;
    });
    std::erase_if(pending_, [instance](const TouchEvent& event) {
      return event.first == instance || event.second == instance;
    });
  }
  std::vector<TouchEvent> drain() {
    std::lock_guard lock(mutex_);
    std::vector<TouchEvent> result;
    result.swap(pending_);
    return result;
  }
 private:
  std::mutex mutex_;
  std::unordered_set<HandlePair, HandlePairHash> active_;
  std::vector<TouchEvent> pending_;
};

using BodyInstanceMap = std::unordered_map<std::uint32_t, InstanceHandle>;
InstanceHandle mappedInstance(const BodyInstanceMap& mapping, const JPH::BodyID& id) {
  const auto found = mapping.find(id.GetIndexAndSequenceNumber());
  return found == mapping.end() ? InstanceHandle{} : found->second;
}
std::uint64_t packHandle(InstanceHandle handle) {
  return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.id;
}
InstanceHandle unpackHandle(std::uint64_t value) {
  return {static_cast<InstanceId>(value), static_cast<Generation>(value >> 32)};
}

class BluboxContactListener final : public JPH::ContactListener {
 public:
  BluboxContactListener(const BodyInstanceMap& mapping, TouchEventQueue& events)
      : mapping_(mapping), events_(events) {}
  void OnContactAdded(const JPH::Body& bodyA, const JPH::Body& bodyB,
                      const JPH::ContactManifold&, JPH::ContactSettings&) override {
    events_.begin(mappedInstance(mapping_, bodyA.GetID()), mappedInstance(mapping_, bodyB.GetID()));
  }
  void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
    events_.end(mappedInstance(mapping_, pair.GetBody1ID()), mappedInstance(mapping_, pair.GetBody2ID()));
  }
 private:
  const BodyInstanceMap& mapping_;
  TouchEventQueue& events_;
};

class BluboxCharacterContactListener final : public JPH::CharacterContactListener {
 public:
  BluboxCharacterContactListener(const BodyInstanceMap& mapping, TouchEventQueue& events)
      : mapping_(mapping), events_(events) {}
  void OnContactAdded(const JPH::CharacterVirtual* character, const JPH::BodyID& otherBody,
                      const JPH::SubShapeID&, JPH::RVec3Arg, JPH::Vec3Arg,
                      JPH::CharacterContactSettings&) override {
    if (character) events_.begin(unpackHandle(character->GetUserData()), mappedInstance(mapping_, otherBody));
  }
  void OnContactRemoved(const JPH::CharacterVirtual* character, const JPH::BodyID& otherBody,
                        const JPH::SubShapeID&) override {
    if (character) events_.end(unpackHandle(character->GetUserData()), mappedInstance(mapping_, otherBody));
  }
  void OnCharacterContactAdded(const JPH::CharacterVirtual* character,
                               const JPH::CharacterVirtual* otherCharacter,
                               const JPH::SubShapeID&, JPH::RVec3Arg, JPH::Vec3Arg,
                               JPH::CharacterContactSettings&) override {
    if (character && otherCharacter)
      events_.begin(unpackHandle(character->GetUserData()), unpackHandle(otherCharacter->GetUserData()));
  }
 private:
  const BodyInstanceMap& mapping_;
  TouchEventQueue& events_;
};
}  // namespace

struct PhysicsWorld::Impl {
  bool allocatorInitialized;
  BroadPhaseMap broad;
  ObjectVsBroadFilter objectVsBroad;
  ObjectPairFilter objectPair;
  JPH::PhysicsSystem system;
  // CharacterVirtual controllers are not registered in PhysicsSystem, so
  // explicitly include every active controller in each character sweep.
  JPH::CharacterVsCharacterCollisionSimple characterVsCharacter;
  JPH::TempAllocatorImpl temporary{10 * 1024 * 1024};
  JPH::JobSystemThreadPool jobs{
      JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
      static_cast<int>(
          std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u))};
  std::unordered_map<InstanceId, JPH::BodyID> bodyIds;
  BodyInstanceMap bodyToInstance;
  TouchEventQueue touchEvents;
  BluboxContactListener contactListener{bodyToInstance, touchEvents};
  BluboxCharacterContactListener characterContactListener{bodyToInstance, touchEvents};
  struct Character {
    JPH::Ref<JPH::CharacterVirtual> value;
    InstanceHandle owner;
  };
  std::unordered_map<InstanceId, Character> characters;
  Impl() : allocatorInitialized((initializeJolt(), true)) {
    system.Init(65536, 0, 65536, 10240, broad, objectVsBroad, objectPair);
    system.SetGravity({0, -19.62f, 0});
    system.SetContactListener(&contactListener);
  }
  ~Impl() {
    system.SetContactListener(nullptr);
    for (auto& [id, character] : characters) {
      (void)id;
      if (character.value) character.value->SetListener(nullptr);
    }
  }
};

PhysicsWorld::PhysicsWorld(InstanceRegistry& r) : registry_(r), impl_(std::make_unique<Impl>()) {}
std::size_t PhysicsWorld::bodyCount() const noexcept { return impl_->bodyIds.size(); }
PhysicsWorld::~PhysicsWorld() {
  for (auto& [id, body] : impl_->bodyIds) {
    (void)id;
    impl_->system.GetBodyInterface().RemoveBody(body);
    impl_->system.GetBodyInterface().DestroyBody(body);
  }
}
bool PhysicsWorld::addBody(InstanceHandle h, float mass, bool anchored) {
  auto* r = registry_.get(h);
  if (!r || !std::isfinite(mass) || mass <= 0) return false;
  if (auto existing = impl_->bodyIds.find(h.id); existing != impl_->bodyIds.end())
    return bodies_.contains(h.id) && bodies_.at(h.id).handle == h &&
           impl_->system.GetBodyInterface().IsAdded(existing->second);
  auto& bi = impl_->system.GetBodyInterface();
  const float sizeX = std::max(0.005f, std::abs(r->size.x * r->local.scale.x));
  const float sizeY = std::max(0.005f, std::abs(r->size.y * r->local.scale.y));
  const float sizeZ = std::max(0.005f, std::abs(r->size.z * r->local.scale.z));
  JPH::Shape* shape = nullptr;
  if (r->classId == ClassId::Sphere) {
    shape = new JPH::SphereShape(0.5f * std::max({sizeX, sizeY, sizeZ}));
  } else {
    // Part, SpawnLocation, wedges, and MeshPart use a conservative box collider.
    // MeshPart can switch to a triangle/convex mesh when runtime mesh resources
    // are available to PhysicsWorld; the fallback is never a missing collider.
    shape = new JPH::BoxShape(JPH::Vec3(sizeX * 0.5f, sizeY * 0.5f, sizeZ * 0.5f));
  }
  JPH::BodyCreationSettings settings(
      shape, JPH::RVec3(r->local.position.x, r->local.position.y, r->local.position.z), toJoltRotation(r->local.rotation),
      anchored ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic, anchored ? Layers::Static : Layers::Dynamic);
  if (!anchored) {
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = mass;
  }
  JPH::Body* body = bi.CreateBody(settings);
  if (!body) return false;
  const JPH::BodyID id = body->GetID();
  bi.AddBody(id, anchored ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
  if (!bi.IsAdded(id)) {
    bi.DestroyBody(id);
    return false;
  }
  impl_->bodyIds[h.id] = id;
  impl_->bodyToInstance[id.GetIndexAndSequenceNumber()] = h;
  lastPhysicsPositions_[h.id] = r->local.position;
  bodies_[h.id] = {h, BodyState{{}, {}, {}, mass, anchored, true}};
  r->anchored = anchored;
  r->componentMask |= HasPhysics;
  return true;
}
void PhysicsWorld::removeBody(InstanceHandle h) {
  impl_->touchEvents.forget(h);
  auto i = impl_->bodyIds.find(h.id);
  if (i != impl_->bodyIds.end()) {
    auto& bi = impl_->system.GetBodyInterface();
    impl_->bodyToInstance.erase(i->second.GetIndexAndSequenceNumber());
    bi.RemoveBody(i->second);
    bi.DestroyBody(i->second);
    impl_->bodyIds.erase(i);
    lastPhysicsPositions_.erase(h.id);
  }
  if (auto character = impl_->characters.find(h.id); character != impl_->characters.end()) {
    impl_->characterVsCharacter.Remove(character->second.value);
    character->second.value->SetListener(nullptr);
    impl_->characters.erase(character);
  }
  if (auto* record = registry_.get(h)) record->componentMask &= ~HasPhysics;
  bodies_.erase(h.id);
  humanoids_.erase(h.id);
  inputs_.erase(h.id);
  previousJumpInputs_.erase(h.id);
  jumpBufferRemaining_.erase(h.id);
}
bool PhysicsWorld::applyForce(InstanceHandle h, Vec3 f) {
  auto i = impl_->bodyIds.find(h.id);
  auto b = bodies_.find(h.id);
  if (i == impl_->bodyIds.end() || b == bodies_.end() || b->second.handle != h || b->second.body.anchored) return false;
  impl_->system.GetBodyInterface().AddForce(i->second, toJolt(f));
  return true;
}
bool PhysicsWorld::applyImpulse(InstanceHandle h, Vec3 v) {
  auto i = impl_->bodyIds.find(h.id);
  auto b = bodies_.find(h.id);
  if (i == impl_->bodyIds.end() || b == bodies_.end() || b->second.handle != h || b->second.body.anchored) return false;
  impl_->system.GetBodyInterface().AddImpulse(i->second, toJolt(v));
  return true;
}
bool PhysicsWorld::addHumanoid(InstanceHandle h, float speed, float jump, float gravity) {
  auto* r = registry_.get(h);
  if (!r || speed <= 0 || jump <= 0 || gravity < 0) return false;
  if (auto existing = impl_->characters.find(h.id); existing != impl_->characters.end()) {
    impl_->characterVsCharacter.Remove(existing->second.value);
    existing->second.value->SetListener(nullptr);
    impl_->characters.erase(existing);
  }
  JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
  settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
  settings->mShape = new JPH::CapsuleShape(0.9f, 0.5f);
  settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -0.5f);
  auto* character =
      new JPH::CharacterVirtual(settings, JPH::RVec3(r->local.position.x, r->local.position.y, r->local.position.z),
                                JPH::Quat::sIdentity(), packHandle(h), &impl_->system);
  character->SetMaxSlopeAngle(JPH::DegreesToRadians(50.0f));
  character->SetListener(&impl_->characterContactListener);
  character->SetCharacterVsCharacterCollision(&impl_->characterVsCharacter);
  impl_->characterVsCharacter.Add(character);
  impl_->characters[h.id] = {character, h};
  humanoids_[h.id] = {speed, jump, gravity, {}, false, HumanoidState::State::Falling};
  return true;
}
void PhysicsWorld::setInput(InstanceHandle h, MoveInput v) {
  if (humanoids_.contains(h.id)) {
    const bool wasJumping = previousJumpInputs_[h.id];
    if (v.jump && !wasJumping) jumpBufferRemaining_[h.id] = 0.18f;
    previousJumpInputs_[h.id] = v.jump;
    const float n = std::sqrt(v.x * v.x + v.z * v.z);
    if (n > 1) {
      v.x /= n;
      v.z /= n;
    }
    inputs_[h.id] = v;
  }
}
void PhysicsWorld::setGravity(Vec3 g) {
  gravity_ = g;
  impl_->system.SetGravity(toJolt(g));
}
const BodyState* PhysicsWorld::body(InstanceHandle h) const {
  auto i = bodies_.find(h.id);
  return i != bodies_.end() && i->second.handle == h ? &i->second.body : nullptr;
}
const HumanoidState* PhysicsWorld::humanoid(InstanceHandle h) const {
  auto i = humanoids_.find(h.id);
  return i == humanoids_.end() ? nullptr : &i->second;
}
void PhysicsWorld::step(double dt) {
  accumulator_ += std::clamp(dt, 0.0, 0.25);
  while (accumulator_ >= kFixedStep) {
    const float step = static_cast<float>(kFixedStep);
    for (auto& [id, entry] : impl_->characters) {
      auto* r = registry_.get(entry.owner);
      auto state = humanoids_.find(id);
      if (!r || state == humanoids_.end()) continue;
      auto& ch = *entry.value;
      auto& hs = state->second;
      const auto in = inputs_[id];
      auto velocity = ch.GetLinearVelocity();
      const float magnitude = std::sqrt(in.x * in.x + in.z * in.z);
      const float scale = magnitude > 1.0f ? 1.0f / magnitude : 1.0f;
      const float axisX = in.x * scale, axisY = in.z * scale;
      const float speed = in.sprint ? hs.walkSpeed * 2.0f : hs.walkSpeed;
      const float sine = std::sin(in.yaw), cosine = std::cos(in.yaw);
      const float targetX = in.worldSpaceVelocity ? in.x : (axisX * cosine + axisY * sine) * speed;
      // Local forward is -Z at yaw 0, matching the client and server input basis.
      const float targetZ = in.worldSpaceVelocity ? in.z : (axisX * sine - axisY * cosine) * speed;
      const float blend = std::clamp((ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround ? 60.0f : 20.0f) * step, 0.0f, 1.0f);
      velocity.SetX(velocity.GetX() + (targetX - velocity.GetX()) * blend);
      velocity.SetZ(velocity.GetZ() + (targetZ - velocity.GetZ()) * blend);
      float& jumpBuffer = jumpBufferRemaining_[id];
      jumpBuffer = std::max(0.0f, jumpBuffer - step);
      if (jumpBuffer > 0.0f && ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround) {
        velocity.SetY(hs.jumpSpeed);
        jumpBuffer = 0.0f;
        hs.grounded = false;
        hs.state = HumanoidState::State::Jumping;
      }
      // CharacterVirtual is moved by velocity; unlike PhysicsSystem::Update it
      // does not integrate gravity into the character velocity for us.
      // Apply gravity before the swept collision query so falling characters
      // are stopped by static and dynamic bodies instead of hovering.
      velocity += JPH::Vec3(0.0f, -hs.gravity * step, 0.0f);
      ch.SetLinearVelocity(velocity);
      JPH::CharacterVirtual::ExtendedUpdateSettings settings;
      ch.ExtendedUpdate(step, JPH::Vec3(0, -hs.gravity, 0), settings,
                        impl_->system.GetDefaultBroadPhaseLayerFilter(Layers::Dynamic),
                        impl_->system.GetDefaultLayerFilter(Layers::Dynamic), {}, {}, impl_->temporary);
      const auto pos = ch.GetPosition();
      auto finalVelocity = ch.GetLinearVelocity();
      if (finalVelocity.GetY() < -60.0f) {
        finalVelocity.SetY(-60.0f);
        ch.SetLinearVelocity(finalVelocity);
      }
      hs.velocity = fromJolt(finalVelocity);
      r->local.position = {static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()),
                           static_cast<float>(pos.GetZ())};
      hs.grounded = ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
      hs.state = ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnSteepGround
          ? HumanoidState::State::SlopeSliding
          : hs.grounded ? (magnitude > 0 ? HumanoidState::State::Running : HumanoidState::State::Idle)
          : (ch.GetLinearVelocity().GetY() > 0 ? HumanoidState::State::Jumping : HumanoidState::State::Falling);
      registry_.markTransformDirty(entry.owner);
    }
    // Apply explicit Luau/editor transform edits before stepping. All BodyInterface
    // access stays outside PhysicsSystem::Update's locked section.
    auto& preStepBodies = impl_->system.GetBodyInterface();
    for (const auto& [id, bodyId] : impl_->bodyIds) {
      auto body = bodies_.find(id);
      if (body == bodies_.end()) continue;
      auto* record = registry_.get(body->second.handle);
      auto previous = lastPhysicsPositions_.find(id);
      if (!record || previous == lastPhysicsPositions_.end()) continue;
      const auto a=record->local.position, b=previous->second;
      const auto rotation=preStepBodies.GetRotation(bodyId);
      const auto requested=toJoltRotation(record->local.rotation);
      const bool rotationChanged=std::abs(rotation.Dot(requested))<0.99999f;
      if (std::abs(a.x-b.x)>1e-4f || std::abs(a.y-b.y)>1e-4f || std::abs(a.z-b.z)>1e-4f || rotationChanged) {
        preStepBodies.SetPositionAndRotation(bodyId, JPH::RVec3(a.x,a.y,a.z), toJoltRotation(record->local.rotation), JPH::EActivation::Activate);
        lastPhysicsPositions_[id]=a;
      }
    }
    impl_->system.Update(step, 1, &impl_->temporary, &impl_->jobs);
    auto& bi = impl_->system.GetBodyInterface();
    for (auto i = impl_->bodyIds.begin(); i != impl_->bodyIds.end();) {
      const auto id = i->first;
      const auto bodyId = i->second;
      auto b = bodies_.find(id);
      if (b == bodies_.end() || !registry_.alive(b->second.handle)) {
        const auto mapped = impl_->bodyToInstance.find(bodyId.GetIndexAndSequenceNumber());
        if (mapped != impl_->bodyToInstance.end()) impl_->touchEvents.forget(mapped->second);
        impl_->bodyToInstance.erase(bodyId.GetIndexAndSequenceNumber());
        bi.RemoveBody(bodyId);
        bi.DestroyBody(bodyId);
        bodies_.erase(id);
        i = impl_->bodyIds.erase(i);
        continue;
      }
      ++i;
      if (b->second.body.anchored) continue;
      auto p = bi.GetPosition(bodyId);
      auto v = bi.GetLinearVelocity(bodyId);
      auto q = bi.GetRotation(bodyId);
      b->second.body.velocity = fromJolt(v);
      if (auto* r = registry_.get(b->second.handle)) {
        r->local.position = {p.GetX(), p.GetY(), p.GetZ()};
        r->local.rotation = fromJoltRotation(q);
        lastPhysicsPositions_[id]=r->local.position;
        registry_.markTransformDirty(b->second.handle);
      }
    }
    for (auto i = impl_->characters.begin(); i != impl_->characters.end();) {
      if (!registry_.alive(i->second.owner)) {
        impl_->touchEvents.forget(i->second.owner);
        impl_->characterVsCharacter.Remove(i->second.value);
        i->second.value->SetListener(nullptr);
        humanoids_.erase(i->first);
        inputs_.erase(i->first);
        previousJumpInputs_.erase(i->first);
        jumpBufferRemaining_.erase(i->first);
        i = impl_->characters.erase(i);
      } else {
        ++i;
      }
    }
    registry_.flushTransforms();
    accumulator_ -= kFixedStep;
  }
}
std::vector<TouchEvent> PhysicsWorld::takeTouchEvents() { return impl_->touchEvents.drain(); }
}  // namespace blubox::engine
