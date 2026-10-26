#include "ScriptRuntime.hpp"
#include "../LuaInstanceBinding.hpp"
#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace blubox::engine::script {
namespace {
constexpr std::size_t kMaxScriptBytes = 256 * 1024;
constexpr std::array<std::string_view, 7> kServerServices{
  "Workspace", "Players", "ReplicatedStorage", "ServerScriptService", "ServerStorage", "RunService", "Lighting"};
constexpr std::array<std::string_view, 6> kClientServices{
  "Workspace", "Players", "ReplicatedStorage", "StarterGui", "StarterPlayer", "RunService"};
constexpr std::array<std::string_view, 4> kSharedServices{
  "Workspace", "Players", "ReplicatedStorage", "RunService"};
}
ScriptRuntime::ScriptRuntime(ScriptContext context, InstanceRegistry& instances,
                             std::function<void(std::string)> onError)
    : runtimeContext_(context), instances_(instances), onError_(std::move(onError)) {}
ScriptRuntime::~ScriptRuntime() { shutdown(); }
bool ScriptRuntime::initialize(std::string& error) {
  if (state_) { error.clear(); return true; }
  state_ = lua_newstate(&ScriptRuntime::allocate, &memory_);
  if (!state_) { error = "Could not allocate the Play Mode Luau VM."; return false; }
  luaL_openlibs(state_);
  lua_callbacks(state_)->userdata = this;
  lua_callbacks(state_)->interrupt = &ScriptRuntime::interrupt;
  for (const char* unsafe : {"io", "os", "debug", "dofile", "loadfile", "loadstring", "require"}) {
    lua_pushnil(state_); lua_setglobal(state_, unsafe);
  }
  LuaInstanceBinding::install(state_, instances_);
  scheduler_ = std::make_unique<TaskScheduler>(state_);
  LuaInstanceBinding::setScheduler(state_, *scheduler_);
  if (remoteManager_) remoteBinding_ = std::make_unique<RemoteBinding>(*remoteManager_, instances_, *scheduler_, runtimeContext_);
  scheduler_->setErrorHandler([this](std::string message) {
    if (onError_) onError_(activeScript_.empty() ? message : activeScript_ + ": " + message);
  });
  scheduler_->install();
  luaL_sandbox(state_);
  error.clear();
  return true;
}
void* ScriptRuntime::allocate(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize) {
  auto& memory = *static_cast<MemoryState*>(userData);
  if (newSize == 0) {
    if (pointer) memory.used = oldSize > memory.used ? 0 : memory.used - oldSize;
    std::free(pointer);
    return nullptr;
  }
  const std::size_t previous = pointer ? oldSize : 0;
  const std::size_t current = previous > memory.used ? 0 : memory.used - previous;
  if (newSize > memory.limit || current > memory.limit - newSize) return nullptr;
  void* result = std::realloc(pointer, newSize);
  if (result) memory.used = current + newSize;
  return result;
}
bool ScriptRuntime::allowedService(ScriptContext context, std::string_view service) const {
  const auto contains = [service](const auto& whitelist) {
    return std::find(whitelist.begin(), whitelist.end(), service) != whitelist.end();
  };
  if (context == ScriptContext::Server) return contains(kServerServices);
  if (context == ScriptContext::Client) return contains(kClientServices);
  return contains(kSharedServices);
}
int ScriptRuntime::getService(lua_State* state) {
  auto* runtime = static_cast<ScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
  const auto context = static_cast<ScriptContext>(lua_tointeger(state, lua_upvalueindex(2)));
  const char* requested = luaL_checkstring(state, 2);
  if (!runtime || !runtime->allowedService(context, requested)) {
    luaL_error(state, "Service '%s' is not available to this script context", requested);
    return 0;
  }
  const auto handle = runtime->instances_.service(requested);
  if (!handle) { lua_pushnil(state); return 1; }
  LuaInstanceBinding::push(state, handle);
  return 1;
}
void ScriptRuntime::installGameApi(lua_State* thread, ScriptContext context) {
  lua_newtable(thread);
  lua_pushlightuserdata(thread, this);
  lua_pushinteger(thread, static_cast<int>(context));
  lua_pushcclosure(thread, getService, "game.GetService", 2);
  lua_setfield(thread, -2, "GetService");
  constexpr std::string_view services[]={"Workspace","Players","ReplicatedStorage","ServerScriptService","ServerStorage","StarterPlayer","StarterGui","Lighting","RunService"};
  for(auto name:services){
    if(!allowedService(context,name))continue;
    auto handle=instances_.service(name);
    if(!handle)continue;
    LuaInstanceBinding::push(thread,handle);
    lua_setfield(thread,-2,std::string(name).c_str());
  }
  lua_setglobal(thread, "game");
  auto workspace=instances_.service("Workspace");
  if(workspace)LuaInstanceBinding::push(thread,workspace);else lua_pushnil(thread);
  lua_setglobal(thread,"workspace");
  const char* name = context == ScriptContext::Server ? "Server" : context == ScriptContext::Client ? "Client" : "Shared";
  lua_pushstring(thread, name); lua_setglobal(thread, "scriptContext");
}
void ScriptRuntime::interrupt(lua_State* state, int gc) {
  (void)gc;
  auto* runtime = static_cast<ScriptRuntime*>(lua_callbacks(state)->userdata);
  if (runtime && std::chrono::steady_clock::now() > runtime->deadline_)
    luaL_error(state, "Play Mode script exceeded its 20 ms execution budget");
}
bool ScriptRuntime::load(const std::vector<ScriptAsset>& assets, std::string& error) {
  if (!state_ && !initialize(error)) return false;
  for (const auto& asset : assets) {
    if (asset.source.empty()) continue;
    if (asset.source.size() > kMaxScriptBytes) { error = asset.name + ": script exceeds 256 KiB"; return false; }
    lua_State* thread = lua_newthread(state_);
    const int ref = lua_ref(state_, -1);
    lua_pop(state_, 1);
    luaL_sandboxthread(thread);
    installGameApi(thread, asset.context);
    const auto owner = instances_.findBySourceId(asset.id);
    if (owner) LuaInstanceBinding::push(thread, owner);
    else lua_pushnil(thread);
    lua_setglobal(thread, "script");
    if (remoteBinding_) {
      std::vector<std::pair<std::string,bool>> remotes;
      std::vector<InstanceHandle> pending;
      constexpr std::string_view services[]={"Workspace","ServerScriptService","ServerStorage","ReplicatedStorage","StarterPlayer","StarterGui","Lighting"};
      for(auto service:services){auto root=instances_.service(service);if(root)pending.push_back(root);}
      while(!pending.empty()){
        auto parent=pending.back();pending.pop_back();
        for(auto child:instances_.children(parent)){
          const auto* record=instances_.get(child);if(!record)continue;
          if(record->classId==ClassId::RemoteEvent)remotes.emplace_back(record->name,false);
          else if(record->classId==ClassId::RemoteFunction)remotes.emplace_back(record->name,true);
          pending.push_back(child);
        }
      }
      remoteBinding_->install(thread,remotes,asset.context);
    }
    const std::string bytecode = Luau::compile(asset.source);
    const std::string chunk = "@play/" + asset.name;
    if (luau_load(thread, chunk.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
      error = asset.name + ": " + (lua_tostring(thread, -1) ? lua_tostring(thread, -1) : "Luau load failed");
      lua_unref(state_, ref);
      return false;
    }
    activeScript_ = asset.name;
    deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    scheduler_->startThread(thread);
    deadline_ = std::chrono::steady_clock::time_point::max();
    activeScript_.clear();
    scriptRefs_.push_back(ref);
  }
  error.clear();
  return true;
}
void ScriptRuntime::tick(double dt) {
  if (!state_ || !scheduler_) return;
  deadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
  // Contact callbacks only enqueue handles. The Luau VM and its registry refs
  // are touched here, on the runtime's owning thread, before scheduled tasks.
  std::vector<std::pair<InstanceHandle, InstanceHandle>> touches;
  {
    std::lock_guard lock(touchMutex_);
    touches.swap(pendingTouches_);
  }
  for (const auto& [part, otherPart] : touches) {
    if (instances_.alive(part) && instances_.alive(otherPart))
      LuaInstanceBinding::fireTouched(state_, part, otherPart);
  }
  if (remoteBinding_) remoteBinding_->process();
  scheduler_->fireEvent("Stepped", dt);
  scheduler_->tick(scheduler_->now() + std::clamp(dt, 0.0, 0.25));
  scheduler_->fireEvent("Heartbeat", dt);
  deadline_ = std::chrono::steady_clock::time_point::max();
}
void ScriptRuntime::queueTouched(InstanceHandle part, InstanceHandle otherPart) {
  if (!part || !otherPart || part == otherPart) return;
  std::lock_guard lock(touchMutex_);
  pendingTouches_.emplace_back(part, otherPart);
}
void ScriptRuntime::pollNetwork(network::RemoteManager& manager,std::uint32_t timeoutMs) {
  deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(20);
  manager.poll(timeoutMs);
  deadline_=std::chrono::steady_clock::time_point::max();
}
void ScriptRuntime::shutdown() {
  if (!state_) return;
  remoteBinding_.reset();
  scheduler_.reset();
  {
    std::lock_guard lock(touchMutex_);
    pendingTouches_.clear();
  }
  for (int reference : scriptRefs_) lua_unref(state_, reference);
  scriptRefs_.clear();
  lua_close(state_);
  state_ = nullptr;
}
}
