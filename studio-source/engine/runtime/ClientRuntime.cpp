#include "ClientRuntime.hpp"
#include "../network/BinaryWriter.hpp"
#include "../../studio/Diagnostics.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
namespace blubox::engine::runtime {
bool ClientRuntime::initialize(const play::RuntimeSnapshot& snapshot, const play::PlayConfig& config,
                               std::uint16_t serverPort, std::string credential, std::string& error) {
  remote_.setLog([](std::string_view message) {
    std::cerr << "[local client ENet] " << message << '\n';
    studio::LogDiagnostic("ENET", std::string("[local client] ") + std::string(message));
  });
  if (!snapshot.instantiateWorld(document_, error)) return false;
  try { scene_ = std::make_unique<SceneRuntime>(document_); }
  catch (const std::exception& exception) { error = exception.what(); return false; }
  predictionPhysics_ = std::make_unique<PhysicsWorld>(scene_->instances());
  for (const auto& entity : document_.entities) {
    const auto handle = scene_->find(entity.id);
    if (entity.name == "LocalPlayerCharacter") {
      localCharacter_ = handle;
      localCharacterNetworkId_ = replication::networkIdForEntityId(entity.id);
      continue;
    }
    const bool supported = entity.kind == "block" || entity.kind == "spawnlocation" ||
        entity.kind == "sphere" || entity.kind == "wedgepart" || entity.kind == "cornerwedgepart" ||
        entity.kind == "meshpart";
    if (!supported) continue;
    bool anchored = true;
    if (entity.properties.is_object()) {
      if (entity.properties.contains("Anchored") && entity.properties["Anchored"].is_boolean())
        anchored = entity.properties["Anchored"].get<bool>();
      else if (entity.properties.contains("anchored") && entity.properties["anchored"].is_boolean())
        anchored = entity.properties["anchored"].get<bool>();
    }
    // The prediction world uses local anchored geometry as collision only. Dynamic
    // assemblies remain server-authoritative and are not simulated by the client.
    if (anchored && (!handle || !predictionPhysics_->addBody(handle, 1.0f, true))) {
      error = "Could not register a Jolt prediction collider for Workspace object '" + entity.name + "'.";
      return false;
    }
  }
  if (localCharacter_ && !predictionPhysics_->addHumanoid(localCharacter_)) {
    error = "Could not create the client prediction character controller.";
    return false;
  }
  for (const auto& entity : document_.entities)
    if (entity.kind == "remoteevent") remote_.registry().ensureEvent(entity.name);
    else if (entity.kind == "remotefunction") remote_.registry().ensureFunction(entity.name);
  remote_.registry().ensureEvent("__BluboxInput");
  remote_.setPropertyDeltaSink([this](std::span<const std::byte> bytes) {
    std::string error;
    interpolation_.receive(bytes, error);
  });
  scripts_ = std::make_unique<script::ScriptRuntime>(script::ScriptContext::Client, scene_->instances());
  scripts_->setRemoteManager(remote_);
  if (!scripts_->initialize(error) || !scripts_->load(snapshot.clientScripts, error) ||
      !scripts_->load(snapshot.sharedModules, error)) return false;
  if (config.enableNetworking && !remote_.connect("127.0.0.1", serverPort, std::move(credential))) {
    error = "Could not create the local ENet client connection.";
    return false;
  }
  initialized_ = true;
  error.clear();
  return true;
}
bool ClientRuntime::sendInput(const input::InputCommand& command) {
  latestInput_ = command;
  if (predictionPhysics_ && localCharacter_) {
    MoveInput movement;
    movement.x = command.moveX;
    movement.z = command.moveY;
    movement.yaw = command.yaw;
    movement.jump = (command.buttons & input::Jump) != 0;
    movement.sprint = (command.buttons & input::Sprint) != 0;
    predictionPhysics_->setInput(localCharacter_, movement);
  }
  return initialized_ && remote_.authenticated();
}
bool ClientRuntime::transmitInput(const input::InputCommand& command) {
  const auto id = remote_.registry().find("__BluboxInput");
  if (!id || !remote_.authenticated()) return false;
  std::vector<std::byte> bytes;
  network::BinaryWriter writer(bytes);
  writer.u32(command.tick); writer.u8(command.buttons);
  writer.f32(command.yaw); writer.f32(command.pitch); writer.f32(command.moveX); writer.f32(command.moveY);
  writer.f32(command.mouseDeltaX); writer.f32(command.mouseDeltaY); writer.f32(command.mouseWheel);
  return remote_.sendRemoteEvent(remote_.playerId(), *id, bytes, network::Channel::UnreliableSequenced);
}
void ClientRuntime::tick(double dt) {
  if (!initialized_) return;
  if(scripts_)scripts_->pollNetwork(remote_,0);else remote_.poll(0);
  if (scripts_) scripts_->tick(dt);
  inputSendAccumulator_ += std::clamp(dt, 0.0, 0.25);
  constexpr double inputInterval = 1.0 / 60.0;
  if (remote_.authenticated() && inputSendAccumulator_ >= inputInterval) {
    inputSendAccumulator_ = std::fmod(inputSendAccumulator_, inputInterval);
    transmitInput(latestInput_);
  }
  if (predictionPhysics_) predictionPhysics_->step(dt);
  if (scene_) scene_->syncToDocument();
  interpolation_.tick(document_, dt, localCharacterNetworkId_);
  if (scene_) scene_->syncFromDocument();
}
void ClientRuntime::disconnect() { remote_.disconnect(); remote_.flush(); }
void ClientRuntime::shutdown() {
  if (!initialized_ && !scene_) return;
  disconnect();
  remote_.shutdown();
  if (scripts_) scripts_->shutdown();
  scripts_.reset();
  predictionPhysics_.reset();
  scene_.reset();
  localCharacter_ = {};
  localCharacterNetworkId_ = 0;
  latestInput_ = {};
  inputSendAccumulator_ = 0.0;
  initialized_ = false;
}
}
