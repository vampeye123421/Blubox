#include "PlayModeBridge.hpp"
#include "SpawnService.hpp"
#include "../runtime/ClientRuntime.hpp"
#include "../runtime/ServerRuntime.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>

namespace blubox::engine::play {
namespace {
std::string makeCredential() {
  std::random_device random;
  constexpr char digits[] = "0123456789abcdef";
  std::string value(64, '0');
  for (char& ch : value) ch = digits[random() & 15u];
  return value;
}
}
PlayModeBridge::PlayModeBridge(studio::MapDocument& editorWorld) : editorWorld_(editorWorld) {}
PlayModeBridge::~PlayModeBridge() { stop(); }
bool PlayModeBridge::start(const PlayConfig& config, PlayError& error) {
  if (state() != PlayModeState::Stopped) { error.message = "Play Mode is already active."; return false; }
  state_ = PlayModeState::Starting;
  config_ = config;
  paused_ = false;
  { std::lock_guard lock(errorMutex_); lastError_.clear(); }
  if (!RuntimeSnapshot::capture(editorWorld_, snapshot_, error.message)) {
    state_ = PlayModeState::Failed; setFailure(error.message); return false;
  }
  const std::string credential = makeCredential();
  server_ = std::make_unique<runtime::ServerRuntime>();
  if (!server_->initialize(snapshot_, config_, credential, error.message)) {
    server_->shutdown(); server_.reset(); state_ = PlayModeState::Failed; setFailure(error.message); return false;
  }
  if (config_.mode != PlayModeType::Run) {
    studio::MapDocument authoritativeWorld = server_->documentSnapshot();
    if (!RuntimeSnapshot::capture(authoritativeWorld, snapshot_, error.message)) {
      server_->shutdown(); server_.reset(); state_ = PlayModeState::Failed; setFailure(error.message); return false;
    }
    client_ = std::make_unique<runtime::ClientRuntime>();
    if (!client_->initialize(snapshot_, config_, server_->port(), credential, error.message)) {
      client_->shutdown(); client_.reset(); server_->shutdown(); server_.reset();
      state_ = PlayModeState::Failed; setFailure(error.message); return false;
    }
  }
  serverThread_ = std::jthread([this](std::stop_token stopToken) { runServer(stopToken); });
  const auto deadline = std::chrono::steady_clock::now() + config_.clientConnectTimeout;
  while (client_ && config_.enableNetworking && !client_->authenticated() && std::chrono::steady_clock::now() < deadline) {
    if (state() == PlayModeState::Failed) break;
    client_->tick(0.001);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (client_ && config_.enableNetworking && !client_->authenticated()) {
    error.message = state() == PlayModeState::Failed ? lastError() : "Local client did not complete the ENet Welcome handshake before timeout.";
    stop(); state_ = PlayModeState::Failed; setFailure(error.message); return false;
  }
  state_ = PlayModeState::Running;
  error.message.clear();
  return true;
}
void PlayModeBridge::runServer(std::stop_token stopToken) {
  using clock = std::chrono::steady_clock;
  const auto step = std::max(std::chrono::milliseconds(1), config_.fixedStep);
  auto next = clock::now();
  try {
    while (!stopToken.stop_requested()) {
      if (!paused_.load()) server_->tick(std::chrono::duration<double>(step).count());
      next += step;
      std::this_thread::sleep_until(next);
      if (clock::now() - next > step * 4) next = clock::now();
    }
    server_->shutdown();
  } catch (const std::exception& exception) {
    setFailure(std::string("Local server runtime failed: ") + exception.what());
    server_->shutdown();
  } catch (...) {
    setFailure("Local server runtime failed with an unknown error.");
    server_->shutdown();
  }
}
void PlayModeBridge::tick(double dt) {
  if (state() != PlayModeState::Running || paused_.load()) return;
  if (client_) {
    client_->tick(dt);
    if (config_.enableNetworking && !client_->authenticated()) {
      const auto& detail = client_->networkDiagnostic();
      setFailure(detail.empty() ? "Local server connection was lost."
                                : "Local server connection was lost: " + detail);
    }
  }
}
bool PlayModeBridge::sendInput(const input::InputCommand& command) { return client_ && client_->sendInput(command); }
void PlayModeBridge::stop() {
  const auto current = state();
  if (current == PlayModeState::Stopped) return;
  state_ = PlayModeState::Stopping;
  paused_ = true;
  if (client_) {
    client_->disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    client_->shutdown();
    client_.reset();
  }
  if (serverThread_.joinable()) {
    serverThread_.request_stop();
    serverThread_.join();
  } else if (server_) server_->shutdown();
  server_.reset();
  snapshot_ = {};
  state_ = PlayModeState::Stopped;
}
void PlayModeBridge::pause() {
  if (state() != PlayModeState::Running) return;
  paused_ = true;
  state_ = PlayModeState::Paused;
}
void PlayModeBridge::resume() {
  if (state() != PlayModeState::Paused) return;
  paused_ = false;
  state_ = PlayModeState::Running;
}
bool PlayModeBridge::clientAuthenticated() const noexcept { return client_ && client_->authenticated(); }
const studio::MapDocument* PlayModeBridge::clientWorld() const noexcept {
  return client_ ? &client_->document() : nullptr;
}
std::optional<studio::MapDocument> PlayModeBridge::serverWorldSnapshot() const {
  return server_ ? std::optional<studio::MapDocument>(server_->documentSnapshot()) : std::nullopt;
}
void PlayModeBridge::setFailure(std::string message) {
  { std::lock_guard lock(errorMutex_); lastError_ = std::move(message); }
  state_ = PlayModeState::Failed;
}
std::string PlayModeBridge::lastError() const { std::lock_guard lock(errorMutex_); return lastError_; }
}


