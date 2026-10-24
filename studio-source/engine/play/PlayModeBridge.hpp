#pragma once
#include "PlayConfig.hpp"
#include "PlayError.hpp"
#include "PlayModeState.hpp"
#include "RuntimeSnapshot.hpp"
#include "../input/InputCommand.hpp"
#include "../../studio/MapDocument.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
namespace blubox::engine::play {
}
namespace blubox::engine::runtime { class ServerRuntime; class ClientRuntime; }
namespace blubox::engine::play {
class PlayModeBridge {
 public:
  explicit PlayModeBridge(studio::MapDocument& editorWorld);
  ~PlayModeBridge();
  PlayModeBridge(const PlayModeBridge&) = delete;
  PlayModeBridge& operator=(const PlayModeBridge&) = delete;
  bool start(const PlayConfig& config, PlayError& error);
  void tick(double deltaSeconds);
  bool sendInput(const input::InputCommand& command);
  void stop();
  void pause();
  void resume();
  [[nodiscard]] PlayModeState state() const noexcept { return state_.load(); }
  [[nodiscard]] bool clientAuthenticated() const noexcept;
  [[nodiscard]] PlayModeType mode() const noexcept { return config_.mode; }
  [[nodiscard]] const studio::MapDocument* clientWorld() const noexcept;
  [[nodiscard]] std::optional<studio::MapDocument> serverWorldSnapshot() const;
  [[nodiscard]] std::string lastError() const;
 private:
  void runServer(std::stop_token stopToken);
  void setFailure(std::string message);
  studio::MapDocument& editorWorld_;
  PlayConfig config_{};
  std::atomic<PlayModeState> state_{PlayModeState::Stopped};
  std::atomic<bool> paused_{false};
  RuntimeSnapshot snapshot_{};
  std::unique_ptr<runtime::ServerRuntime> server_;
  std::unique_ptr<runtime::ClientRuntime> client_;
  std::jthread serverThread_;
  mutable std::mutex errorMutex_;
  std::string lastError_;
};
}
