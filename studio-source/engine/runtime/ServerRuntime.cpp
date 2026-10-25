#include "ServerRuntime.hpp"
#include "../play/SpawnService.hpp"
#include "../network/BinaryReader.hpp"
#include "../../studio/Diagnostics.hpp"
#include "HumanoidController.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
namespace blubox::engine::runtime {
bool ServerRuntime::initialize(const play::RuntimeSnapshot& snapshot, const play::PlayConfig& config,
                               std::string credential, std::string& error) {
  remote_.setLog([](std::string_view message) {
    std::cerr << "[local server ENet] " << message << '\n';
    studio::LogDiagnostic("ENET", std::string("[local server] ") + std::string(message));
  });
  if (!snapshot.instantiateWorld(document_, error)) return false;
  if (config.mode != play::PlayModeType::Run) {
    auto spawn = play::SpawnService::resolve(document_);
    if (config.mode == play::PlayModeType::PlayHere) {
      bool valid = true;
      for (float value : config.requestedSpawn) valid &= std::isfinite(value) && std::abs(value) <= 100000.0f;
      if (valid) {
        spawn.position = config.requestedSpawn;
        const float fx = config.requestedForward[0], fz = config.requestedForward[2];
        if (std::isfinite(fx) && std::isfinite(fz) && std::abs(fx) + std::abs(fz) > 0.0001f)
          spawn.rotation[1] = std::atan2(fx, fz);
      }
    }
    auto& character = document_.AddEntity("model");
    character.name = config.playerDisplayName.empty() ? "Player" : config.playerDisplayName;
    character.transform.position = spawn.position;
    character.transform.rotation = spawn.rotation;
    character.properties = {{"RuntimeOnly", true}, {"ClassName", "Model"},
                            {"RuntimeLocalPlayerCharacter", true}, {"WalkSpeed", 16.0}, {"JumpPower", 50.0}};
    const std::string characterId = character.id;
    const auto addBodyPart = [&](std::string kind, std::string name, std::array<float, 3> position,
                                 std::array<float, 3> scale, std::array<unsigned char, 4> color) {
      auto& part = document_.AddEntity(std::move(kind));
      part.name = std::move(name);
      part.parent = characterId;
      part.transform.position = position;
      part.transform.scale = scale;
      part.color = color;
      part.properties["RuntimeOnly"] = true;
      part.properties["RuntimeVisualOnly"] = true;
    };
    addBodyPart("block", "Torso", {0.0f, 0.35f, 0.0f}, {0.78f, 0.9f, 0.4f}, {34, 184, 207, 255});
    addBodyPart("sphere", "Head", {0.0f, 1.03f, 0.0f}, {0.54f, 0.54f, 0.54f}, {220, 167, 123, 255});
    addBodyPart("block", "LeftArm", {-0.51f, 0.35f, 0.0f}, {0.22f, 0.68f, 0.24f}, {220, 167, 123, 255});
    addBodyPart("block", "RightArm", {0.51f, 0.35f, 0.0f}, {0.22f, 0.68f, 0.24f}, {220, 167, 123, 255});
    addBodyPart("block", "LeftLeg", {-0.2f, -0.39f, 0.0f}, {0.31f, 0.72f, 0.32f}, {51, 75, 120, 255});
    addBodyPart("block", "RightLeg", {0.2f, -0.39f, 0.0f}, {0.31f, 0.72f, 0.32f}, {51, 75, 120, 255});
    auto& shirt = document_.AddEntity("shirt");
    shirt.name = "Shirt";
    shirt.parent = characterId;
    shirt.properties = {{"ShirtId", "shirt-cyan-hoodie"}, {"Color", "#22b8cf"}, {"RuntimeOnly", true}};
    auto& pants = document_.AddEntity("pants");
    pants.name = "Pants";
    pants.parent = characterId;
    pants.properties = {{"PantsId", "pants-dark-denim"}, {"Color", "#334b78"}, {"RuntimeOnly", true}};
  }
  try { scene_ = std::make_unique<SceneRuntime>(document_); }
  catch (const std::exception& exception) { error = exception.what(); return false; }
  for (const auto& entity : document_.entities)
    if (entity.kind == "remoteevent") remote_.registry().ensureEvent(entity.name);
    else if (entity.kind == "remotefunction") remote_.registry().ensureFunction(entity.name);
  if (config.enableNetworking) {
    if (!remote_.startHost("127.0.0.1", config.serverPort, config.maxClients,
        [credential = std::move(credential)](std::string_view presented) -> std::optional<network::PlayerId> {
          return presented == credential ? std::optional<network::PlayerId>(1) : std::nullopt;
        })) { error = "Could not bind the local ENet server to 127.0.0.1."; return false; }
  }
  scripts_ = std::make_unique<script::ScriptRuntime>(script::ScriptContext::Server, scene_->instances());
  scripts_->setRemoteManager(remote_);
  if (!scripts_->initialize(error) || !scripts_->load(snapshot.serverScripts, error) ||
      !scripts_->load(snapshot.sharedModules, error)) return false;
  physics_ = std::make_unique<PhysicsWorld>(scene_->instances());
  for (const auto& entity : document_.entities) {
    if (entity.properties.value("RuntimeLocalPlayerCharacter", false)) {
      localCharacter_ = scene_->find(entity.id);
      continue;
    }
    if (entity.properties.value("RuntimeVisualOnly", false)) continue;
    if (entity.kind != "block" && entity.kind != "spawnlocation" && entity.kind != "sphere" &&
        entity.kind != "wedgepart" && entity.kind != "cornerwedgepart" && entity.kind != "meshpart") continue;
    bool anchored = true;
    if (entity.properties.is_object()) {
      if (entity.properties.contains("Anchored") && entity.properties["Anchored"].is_boolean()) anchored = entity.properties["Anchored"].get<bool>();
      else if (entity.properties.contains("anchored") && entity.properties["anchored"].is_boolean()) anchored = entity.properties["anchored"].get<bool>();
    }
    const auto handle = scene_->find(entity.id);
    if (!handle || !physics_->addBody(handle, 1.0f, anchored)) {
      error = "Could not register a Jolt collider for Workspace object '" + entity.name + "'.";
      studio::LogDiagnostic("PHYSICS", error);
      return false;
    }
  }
  studio::LogDiagnostic("PHYSICS", "Local server registered " + std::to_string(physics_->bodyCount()) + " scene colliders.");
  if (localCharacter_ && !physics_->addHumanoid(localCharacter_)) { error = "Could not create the server-authoritative character controller."; return false; }
  if (localCharacter_) {
    remote_.registry().ensureEvent("__BluboxInput");
    inputSinkToken_ = remote_.addEventSink([this](network::PlayerId player, std::string_view name, std::span<const std::byte> bytes) {
      if (player != 1 || name != "__BluboxInput" || !physics_ || !localCharacter_) return;
      network::BinaryReader reader(bytes);
      input::InputCommand command;
      if (!reader.u32(command.tick) || !reader.u8(command.buttons) || !reader.f32(command.yaw) || !reader.f32(command.pitch) ||
          !reader.f32(command.moveX) || !reader.f32(command.moveY) || !reader.f32(command.mouseDeltaX) ||
          !reader.f32(command.mouseDeltaY) || !reader.f32(command.mouseWheel) || reader.remaining() != 0) return;
      const float values[] = {command.yaw, command.pitch, command.moveX, command.moveY};
      for (float value : values) if (!std::isfinite(value) || std::abs(value) > 1000.0f) return;
      if (command.tick <= lastInputTick_) return;
      lastInputTick_ = command.tick;
      const Vec3 desired = HumanoidController::desiredVelocity(humanoid_, command);
      MoveInput movement;
      movement.x = desired.x; movement.z = desired.z;
      movement.jump = (command.buttons & input::Jump) != 0;
      movement.worldSpaceVelocity = true;
      physics_->setInput(localCharacter_, movement);
    });
  }
  initialized_ = true;
  error.clear();
  return true;
}
void ServerRuntime::tick(double dt) {
  if (!initialized_) return;
  // Poll ENet and run Luau without holding the scene/document snapshot lock.
  if(scripts_)scripts_->pollNetwork(remote_,1);else remote_.poll(1);
  if (scripts_) scripts_->tick(dt);
  std::vector<std::vector<std::byte>> packets;
  std::vector<TouchEvent> touchEvents;
  replication::PropertyDeltaEncoder::VelocityMap velocities;
  {
  std::lock_guard worldLock(documentMutex_);
  if (physics_) {
    physics_->step(dt);
    touchEvents = physics_->takeTouchEvents();
    scene_->syncToDocument();
  }
  replicationAccumulator_ += std::clamp(dt, 0.0, 0.25);
  if (replicationAccumulator_ >= 1.0/20.0) {
    replicationAccumulator_ = std::fmod(replicationAccumulator_, 1.0/20.0);
    if(remote_.authenticatedPeerCount()){
      for (const auto& entity : document_.entities) {
        const auto handle = scene_->find(entity.id);
        std::array<float, 3> value{};
        if (const auto* humanoid = physics_->humanoid(handle))
          value = {humanoid->velocity.x, humanoid->velocity.y, humanoid->velocity.z};
        else if (const auto* body = physics_->body(handle))
          value = {body->velocity.x, body->velocity.y, body->velocity.z};
        velocities.emplace(entity.id, value);
      }
      packets = replication_.encode(document_, velocities);
    }
  }
  }
  // Queue both directional RBXScriptSignal notifications after leaving the
  // world lock. ScriptRuntime fires them on its next VM tick.
  if (scripts_) {
    for (const TouchEvent& event : touchEvents) {
      scripts_->queueTouched(event.first, event.second);
      scripts_->queueTouched(event.second, event.first);
    }
  }
  // ENet send happens after releasing the world lock.
  for(const auto& packet:packets)remote_.broadcastPropertyDelta(packet);
}
studio::MapDocument ServerRuntime::documentSnapshot() const {
  std::lock_guard lock(documentMutex_);
  return document_;
}
void ServerRuntime::shutdown() {
  if (!initialized_ && !scene_) return;
  remote_.removeEventSink(inputSinkToken_); inputSinkToken_ = 0;
  remote_.shutdown();
  if (scripts_) scripts_->shutdown();
  scripts_.reset();
  physics_.reset();
  scene_.reset();
  initialized_ = false;
  localCharacter_ = {}; lastInputTick_ = 0;
  humanoid_ = {};
}
}
