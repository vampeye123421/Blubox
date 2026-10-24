#include "TaskScheduler.hpp"
#include <lua.h>
#include <lualib.h>
#include <algorithm>
#include <cmath>

namespace blubox::engine {
TaskScheduler::TaskScheduler(lua_State* s) : main_(s) {}
TaskScheduler::~TaskScheduler() {
  while (!jobs_.empty()) {
    lua_unref(main_, jobs_.top().threadRef);
    jobs_.pop();
  }
  for(auto& [_,job]:suspendedJobs_)lua_unref(main_,job.threadRef);
  suspendedJobs_.clear();
  for (auto& [name, refs] : eventCallbacks_) {
    (void)name;
    for (int ref : refs) lua_unref(main_, ref);
  }
}
std::uint64_t TaskScheduler::queueFunction(lua_State* caller, int index, double wake, int firstArgument, int argumentCount) {
  if (!main_ || !lua_isfunction(caller, index)) return 0;
  index = lua_absindex(caller, index);
  if (argumentCount < 0 || argumentCount > 16) return 0;
  if (argumentCount > 0) firstArgument = lua_absindex(caller, firstArgument);
  lua_State* thread = lua_newthread(caller);
  luaL_sandboxthread(thread);
  if (caller != main_) lua_xmove(caller, main_, 1);
  const int ref = lua_ref(main_, -1);
  lua_pop(main_, 1);
  lua_pushvalue(caller, index);
  lua_xmove(caller, thread, 1);
  for (int i = 0; i < argumentCount; ++i) {
    lua_pushvalue(caller, firstArgument + i);
    lua_xmove(caller, thread, 1);
  }
  const auto id = nextId_++;
  jobs_.push({std::max(now_, wake), id, ref, now_, false, argumentCount});
  return id;
}
std::uint64_t TaskScheduler::spawn(lua_State* L, int i) { return queueFunction(L, i, now_); }
std::uint64_t TaskScheduler::spawnWithArguments(lua_State* L, int functionIndex, int firstArgument, int argumentCount) {
  return queueFunction(L, functionIndex, now_, firstArgument, argumentCount);
}
std::uint64_t TaskScheduler::defer(lua_State* L, int i) { return queueFunction(L, i, now_ + 1.0 / 60.0); }
void TaskScheduler::startThread(lua_State* thread) {
  if (!main_ || !thread) return;
  lua_pushthread(thread);
  lua_xmove(thread, main_, 1);
  const int ref = lua_ref(main_, -1);
  lua_pop(main_, 1);
  Job job{now_, nextId_++, ref, now_, false};
  auto* previous = activeThread_;
  activeThread_ = thread;
  const int status = lua_resume(thread, main_, 0);
  activeThread_ = previous;
  finishResume(job, thread, status);
}
std::uint64_t TaskScheduler::delay(lua_State* L, double seconds, int i) {
  if (!std::isfinite(seconds)) return 0;
  return queueFunction(L, i, now_ + std::clamp(seconds, 0.0, 86400.0));
}
void TaskScheduler::cancel(std::uint64_t id) {
  if (id) cancelled_.insert(id);
}
std::uint64_t TaskScheduler::markExternalWait(lua_State* caller) {
  if (!caller || caller != activeThread_) return 0;
  auto token=nextExternalToken_++;if(!token)token=nextExternalToken_++;
  waitingThreads_[caller]=token;return token;
}
void TaskScheduler::cancelExternalWait(lua_State* caller) { waitingThreads_.erase(caller); }
lua_State* TaskScheduler::externalThread(std::uint64_t token) {
  auto i=suspendedJobs_.find(token);if(i==suspendedJobs_.end())return nullptr;
  lua_getref(main_,i->second.threadRef);auto* thread=lua_tothread(main_,-1);lua_pop(main_,1);return thread;
}
bool TaskScheduler::resumeExternal(std::uint64_t token,int argumentCount) {
  auto i=suspendedJobs_.find(token);if(i==suspendedJobs_.end())return false;
  Job job=i->second;suspendedJobs_.erase(i);
  lua_getref(main_,job.threadRef);auto* thread=lua_tothread(main_,-1);lua_pop(main_,1);if(!thread){lua_unref(main_,job.threadRef);return false;}
  auto* previous=activeThread_;activeThread_=thread;const int status=lua_resume(thread,main_,argumentCount);activeThread_=previous;
  finishResume(job,thread,status);return true;
}
void TaskScheduler::finishResume(Job job,lua_State* thread,int status) {
  if(status==LUA_YIELD){
    auto waiting=waitingThreads_.find(thread);
    if(waiting!=waitingThreads_.end()){suspendedJobs_[waiting->second]=job;waitingThreads_.erase(waiting);return;}
    double seconds=0;if(lua_gettop(thread)>0&&lua_isnumber(thread,-1))seconds=lua_tonumber(thread,-1);
    lua_settop(thread,0);jobs_.push({now_+std::clamp(seconds,0.0,86400.0),job.id,job.threadRef,now_,true});return;
  }
  if(status!=LUA_OK&&errorHandler_){const char* message=lua_tostring(thread,-1);errorHandler_(message?message:"Luau scheduled task failed.");}
  lua_settop(thread,0);lua_unref(main_,job.threadRef);
}
bool TaskScheduler::connectEvent(lua_State* caller, const std::string& event, int index) {
  if (event != "Stepped" && event != "Heartbeat" && event != "RenderStepped" && event != "PreSimulation") return false;
  if (!lua_isfunction(caller, index)) return false;
  index = lua_absindex(caller, index);
  lua_pushvalue(caller, index);
  int ref = lua_ref(caller, -1);
  lua_pop(caller, 1);
  eventCallbacks_[event].push_back(ref);
  return true;
}
void TaskScheduler::fireEvent(const std::string& event, double delta) {
  auto i = eventCallbacks_.find(event);
  if (i == eventCallbacks_.end()) return;
  const auto refs = i->second;
  std::unordered_set<int> failed;
  for (int ref : refs) {
    lua_getref(main_, ref);
    lua_pushnumber(main_, delta);
    if (lua_pcall(main_, 1, 0, 0) != LUA_OK) {
      const char* message = lua_tostring(main_, -1);
      if (errorHandler_) errorHandler_(message ? message : "RunService callback failed.");
      lua_pop(main_, 1);
      failed.insert(ref);
    }
  }
  if (!failed.empty()) {
    auto& live = eventCallbacks_[event];
    for (int ref : failed) {
      lua_unref(main_, ref);
      live.erase(std::remove(live.begin(), live.end(), ref), live.end());
    }
  }
}
void TaskScheduler::tick(double t) {
  now_ = std::max(now_, t);
  while (!jobs_.empty() && jobs_.top().wake <= now_) {
    auto job = jobs_.top();
    jobs_.pop();
    if (cancelled_.erase(job.id)) {
      lua_unref(main_, job.threadRef);
      continue;
    }
    lua_getref(main_, job.threadRef);
    auto* thread = lua_tothread(main_, -1);
    lua_pop(main_, 1);
    if (thread) {
      if (job.resumeWithDelta) lua_pushnumber(thread, std::max(0.0, now_ - job.queuedAt));
      auto* previous=activeThread_;activeThread_=thread;
      const int status=lua_resume(thread,main_,job.resumeWithDelta?1:job.initialArguments);
      activeThread_=previous;
      finishResume(job,thread,status);
    } else
      lua_unref(main_, job.threadRef);
  }
}
TaskScheduler* TaskScheduler::fromLua(lua_State* L) {
  return static_cast<TaskScheduler*>(lua_touserdata(L, lua_upvalueindex(1)));
}
int TaskScheduler::luaSpawn(lua_State* L) {
  auto* s = fromLua(L);
  if (!s || !lua_isfunction(L, 1)) {
    luaL_error(L, "task.spawn expects a function");
    return 0;
  }
  lua_pushnumber(L, static_cast<double>(s->spawn(L, 1)));
  return 1;
}
int TaskScheduler::luaDelay(lua_State* L) {
  auto* s = fromLua(L);
  if (!s || !lua_isfunction(L, 2)) {
    luaL_error(L, "task.delay expects seconds and a function");
    return 0;
  }
  lua_pushnumber(L, static_cast<double>(s->delay(L, luaL_checknumber(L, 1), 2)));
  return 1;
}
int TaskScheduler::luaDefer(lua_State* L) {
  auto* s = fromLua(L);
  if (!s || !lua_isfunction(L, 1)) {
    luaL_error(L, "task.defer expects a function");
    return 0;
  }
  lua_pushnumber(L, static_cast<double>(s->defer(L, 1)));
  return 1;
}
int TaskScheduler::luaWait(lua_State* L) {
  double seconds = 0;
  if (lua_gettop(L) > 0) {
    if (!lua_isnumber(L, 1)) {
      luaL_error(L, "task.wait expects a number");
      return 0;
    }
    seconds = lua_tonumber(L, 1);
  }
  if (!std::isfinite(seconds)) {
    luaL_error(L, "task.wait delay must be finite");
    return 0;
  }
  lua_pushnumber(L, std::clamp(seconds, 0.0, 86400.0));
  return lua_yield(L, 1);
}
int TaskScheduler::luaCancel(lua_State* L) {
  auto* s = fromLua(L);
  if (!s || !lua_isnumber(L, 1)) {
    luaL_error(L, "task.cancel expects a task id");
    return 0;
  }
  s->cancel(static_cast<std::uint64_t>(lua_tonumber(L, 1)));
  return 0;
}
int TaskScheduler::luaConnect(lua_State* L) {
  auto* s = fromLua(L);
  if (!s || !lua_isstring(L, 1) || !lua_isfunction(L, 2)) {
    luaL_error(L, "RunService.Connect expects event name and callback");
    return 0;
  }
  if (!s->connectEvent(L, lua_tostring(L, 1), 2)) {
    luaL_error(L, "Unknown RunService event");
    return 0;
  }
  return 0;
}
void TaskScheduler::install() {
  if (!main_) return;
  lua_newtable(main_);
  const struct {
    const char* name;
    lua_CFunction fn;
  } f[] = {{"spawn", luaSpawn}, {"delay", luaDelay}, {"defer", luaDefer}, {"wait", luaWait}, {"cancel", luaCancel}};
  for (auto& x : f) {
    lua_pushlightuserdata(main_, this);
    lua_pushcclosure(main_, x.fn, x.name, 1);
    lua_setfield(main_, -2, x.name);
  }
  lua_setglobal(main_, "task");
  lua_getglobal(main_, "task");
  for (const char* alias : {"wait", "spawn", "delay"}) {
    lua_getfield(main_, -1, alias);
    lua_setglobal(main_, alias);
  }
  lua_pop(main_, 1);
  lua_newtable(main_);
  lua_pushlightuserdata(main_, this);
  lua_pushcclosure(main_, luaConnect, "RunService.Connect", 1);
  lua_setfield(main_, -2, "Connect");
  lua_setglobal(main_, "RunService");
}
}  // namespace blubox::engine
