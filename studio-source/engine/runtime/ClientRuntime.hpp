#pragma once
#include "../Scene.hpp"
#include "../PhysicsWorld.hpp"
#include "../replication/InterpolationSystem.hpp"
#include "../network/RemoteManager.hpp"
#include "../play/PlayConfig.hpp"
#include "../play/RuntimeSnapshot.hpp"
#include "../input/InputCommand.hpp"
#include "../script/ScriptRuntime.hpp"
#include <memory>
#include <string>
namespace blubox::engine::runtime {
class ClientRuntime {
 public:
  bool initialize(const play::RuntimeSnapshot& snapshot, const play::PlayConfig& config,
                  std::uint16_t serverPort, std::string credential, std::string& error);
  void tick(double deltaSeconds);
  bool sendInput(const input::InputCommand& command);
  void disconnect();
  void shutdown();
  bool authenticated() const noexcept { return remote_.authenticated(); }
  [[nodiscard]] const std::string& networkDiagnostic() const noexcept { return remote_.lastDiagnostic(); }
  network::RemoteManager& remoteManager() noexcept { return remote_; }
  const SceneRuntime* scene() const noexcept { return scene_.get(); }
  const studio::MapDocument& document() const noexcept { return document_; }
 private:
  bool transmitInput(const input::InputCommand& command);
  studio::MapDocument document_;
  std::unique_ptr<SceneRuntime> scene_;
  std::unique_ptr<script::ScriptRuntime> scripts_;
  network::RemoteManager remote_;
  replication::InterpolationSystem interpolation_;
  std::unique_ptr<PhysicsWorld> predictionPhysics_;
  InstanceHandle localCharacter_{};
  replication::NetworkId localCharacterNetworkId_ = 0;
  input::InputCommand latestInput_{};
  double inputSendAccumulator_ = 0.0;
  bool initialized_ = false;
};
}
