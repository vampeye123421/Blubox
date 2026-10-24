#pragma once
#include "ScriptAsset.hpp"
#include "RemoteBinding.hpp"
#include "../TaskScheduler.hpp"
#include "../EngineCore.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
struct lua_State;
namespace blubox::engine::script {
// One VM per runtime (server/client); each asset gets its own sandboxed Luau thread.
class ScriptRuntime {
 public:
  explicit ScriptRuntime(ScriptContext runtimeContext, InstanceRegistry& instances,
                         std::function<void(std::string)> onError = {});
  ~ScriptRuntime();
  ScriptRuntime(const ScriptRuntime&) = delete;
  ScriptRuntime& operator=(const ScriptRuntime&) = delete;
  bool initialize(std::string& error);
  void setRemoteManager(network::RemoteManager& manager) { remoteManager_ = &manager; }
  bool load(const std::vector<ScriptAsset>& assets, std::string& error);
  void tick(double deltaSeconds);
  void queueTouched(InstanceHandle part, InstanceHandle otherPart);
  void pollNetwork(network::RemoteManager& manager, std::uint32_t timeoutMs = 0);
  void shutdown();
  [[nodiscard]] std::size_t scriptCount() const noexcept { return scriptRefs_.size(); }
 private:
  struct MemoryState { std::size_t used = 0; std::size_t limit = 64u * 1024u * 1024u; };
  static void* allocate(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize);
  static int getService(lua_State* state);
  static void interrupt(lua_State* state, int gc);
  void installGameApi(lua_State* thread, ScriptContext context);
  bool allowedService(ScriptContext context, std::string_view service) const;
  ScriptContext runtimeContext_;
  InstanceRegistry& instances_;
  lua_State* state_ = nullptr;
  std::unique_ptr<TaskScheduler> scheduler_;
  network::RemoteManager* remoteManager_ = nullptr;
  std::unique_ptr<RemoteBinding> remoteBinding_;
  std::vector<int> scriptRefs_;
  std::mutex touchMutex_;
  std::vector<std::pair<InstanceHandle, InstanceHandle>> pendingTouches_;
  std::function<void(std::string)> onError_;
  MemoryState memory_{};
  std::chrono::steady_clock::time_point deadline_ = std::chrono::steady_clock::time_point::max();
  std::string activeScript_;
};
}
