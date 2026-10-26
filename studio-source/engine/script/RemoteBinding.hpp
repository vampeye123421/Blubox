#pragma once
#include "ScriptContext.hpp"
#include "../TaskScheduler.hpp"
#include "../network/RemoteManager.hpp"
#include "../EngineCore.hpp"
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
struct lua_State;
namespace blubox::engine::script {
class RemoteBinding {
 public:
  RemoteBinding(network::RemoteManager& manager, InstanceRegistry& instances, TaskScheduler& scheduler, ScriptContext context);
  ~RemoteBinding();
  void install(lua_State*, const std::vector<std::pair<std::string,bool>>& remotes, ScriptContext scriptContext);
  void process();
 private:
  struct Callback { network::RemoteId id; bool serverEvent; int reference; };
  struct Queued { network::RemoteId id; bool serverEvent; network::PlayerId player; std::vector<std::byte> payload; };
  struct Completion { std::uint64_t waitToken; bool ok; std::vector<std::byte> payload; };
  static int index(lua_State*); static int newIndex(lua_State*); static int fire(lua_State*);
  static int connect(lua_State*); static int invoke(lua_State*); static int getRemote(lua_State*);
  int handleIndex(lua_State*); int handleNewIndex(lua_State*); int handleFire(lua_State*);
  int handleConnect(lua_State*); int handleInvoke(lua_State*);
  bool server() const noexcept { return context_ == ScriptContext::Server; }
  network::RemoteManager& manager_; InstanceRegistry& instances_; TaskScheduler& scheduler_; ScriptContext context_;
  lua_State* main_ = nullptr;
  std::vector<Callback> callbacks_; std::deque<Queued> queued_; std::deque<Completion> completed_;
  std::unordered_map<network::RemoteId,int> serverInvokeRefs_;
  std::unordered_map<std::string,network::RemoteId> idsByName_;
  network::RemoteManager::EventSinkToken eventSinkToken_ = 0;
};
}
