#pragma once
#include "../Scene.hpp"
#include "../PhysicsWorld.hpp"
#include "../replication/PropertyDeltaEncoder.hpp"
#include "../network/RemoteManager.hpp"
#include "../play/PlayConfig.hpp"
#include "../play/RuntimeSnapshot.hpp"
#include "../script/ScriptRuntime.hpp"
#include "../input/InputCommand.hpp"
#include "HumanoidComponent.hpp"
#include <memory>
#include <mutex>
#include <string>
namespace blubox::engine::runtime {
class ServerRuntime {
 public:
  bool initialize(const play::RuntimeSnapshot& snapshot, const play::PlayConfig& config,
                  std::string credential, std::string& error);
  void tick(double fixedDelta);
  void shutdown();
  std::uint16_t port() const noexcept { return remote_.boundPort(); }
  network::RemoteManager& remoteManager() noexcept { return remote_; }
  const SceneRuntime* scene() const noexcept { return scene_.get(); }
  studio::MapDocument documentSnapshot() const;
 private:
  studio::MapDocument document_;
  std::unique_ptr<SceneRuntime> scene_;
  std::unique_ptr<script::ScriptRuntime> scripts_;
  std::unique_ptr<PhysicsWorld> physics_;
  replication::PropertyDeltaEncoder replication_;
  double replicationAccumulator_ = 0;
  network::RemoteManager remote_;
  bool initialized_ = false;
  mutable std::mutex documentMutex_;
  InstanceHandle localCharacter_{};
  network::RemoteManager::EventSinkToken inputSinkToken_ = 0;
  std::uint32_t lastInputTick_ = 0;
  HumanoidComponent humanoid_{};
};
}
