#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lua_State;
namespace blubox::engine {

class TaskScheduler {
 public:
  explicit TaskScheduler(lua_State* mainState);
  ~TaskScheduler();
  TaskScheduler(const TaskScheduler&) = delete;
  TaskScheduler& operator=(const TaskScheduler&) = delete;
  std::uint64_t spawn(lua_State* caller, int functionIndex);
  std::uint64_t spawnWithArguments(lua_State* caller, int functionIndex, int firstArgument, int argumentCount);
  std::uint64_t delay(lua_State* caller, double seconds, int functionIndex);
  std::uint64_t defer(lua_State* caller, int functionIndex);
  // Starts an already-loaded script coroutine and schedules it again after task.wait.
  void startThread(lua_State* thread);
  void cancel(std::uint64_t id);
  std::uint64_t markExternalWait(lua_State* caller);
  void cancelExternalWait(lua_State* caller);
  lua_State* externalThread(std::uint64_t token);
  bool resumeExternal(std::uint64_t token, int argumentCount);
  void tick(double nowSeconds);
  void fireEvent(const std::string& event, double deltaSeconds);
  void setErrorHandler(std::function<void(std::string)> handler) { errorHandler_ = std::move(handler); }
  // Installs a sandbox-friendly global `task` table; `task.wait` yields the current coroutine.
  void install();
  double now() const noexcept { return now_; }

 private:
  struct Job {
    double wake = 0;
    std::uint64_t id = 0;
    int threadRef = -1;
    double queuedAt = 0;
    bool resumeWithDelta = false;
    int initialArguments = 0;
  };
  struct Later {
    bool operator()(const Job& a, const Job& b) const { return a.wake > b.wake || (a.wake == b.wake && a.id > b.id); }
  };
  static TaskScheduler* fromLua(lua_State* L);
  static int luaSpawn(lua_State* L);
  static int luaDelay(lua_State* L);
  static int luaDefer(lua_State* L);
  static int luaWait(lua_State* L);
  static int luaCancel(lua_State* L);
  static int luaConnect(lua_State* L);
  bool connectEvent(lua_State* caller, const std::string& event, int functionIndex);
  std::uint64_t queueFunction(lua_State* caller, int index, double wake, int firstArgument = 0, int argumentCount = 0);
  void finishResume(Job job, lua_State* thread, int status);
  lua_State* main_ = nullptr;
  std::priority_queue<Job, std::vector<Job>, Later> jobs_;
  std::unordered_set<std::uint64_t> cancelled_;
  std::unordered_map<std::string, std::vector<int>> eventCallbacks_;
  std::uint64_t nextId_ = 1;
  std::uint64_t nextExternalToken_ = 1;
  lua_State* activeThread_ = nullptr;
  std::unordered_map<lua_State*,std::uint64_t> waitingThreads_;
  std::unordered_map<std::uint64_t,Job> suspendedJobs_;
  double now_ = 0;
  std::function<void(std::string)> errorHandler_;
};

}  // namespace blubox::engine
