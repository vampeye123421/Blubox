#pragma once

#include "PublishedWorld.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

// Restricted, client-only Luau environment for published LocalScripts. This
// slice exposes player GUI instances and UI button signals; it deliberately
// has no server, filesystem, network, or arbitrary native bindings.
class LocalScriptRuntime {
public:
    LocalScriptRuntime() = default;
    ~LocalScriptRuntime();
    LocalScriptRuntime(const LocalScriptRuntime &) = delete;
    LocalScriptRuntime &operator=(const LocalScriptRuntime &) = delete;

    bool Start(PublishedWorld &world);
    void Tick(double deltaSeconds);
    void FireActivated(std::string_view guiId);
    void Stop();

private:
    struct Callback { std::string target; std::string event; int functionRef = -1; };
    struct Job {
        double wake = 0.0;
        double queuedAt = 0.0;
        std::uint64_t order = 0;
        int threadRef = -1;
        bool resumeWithDelta = false;
    };
    struct Later {
        bool operator()(const Job &a, const Job &b) const noexcept {
            return a.wake > b.wake || (a.wake == b.wake && a.order > b.order);
        }
    };
    struct LuaInstance { LocalScriptRuntime *runtime = nullptr; char id[128]{}; };
    struct LuaSignal { LocalScriptRuntime *runtime = nullptr; char id[128]{}; char event[24]{}; };
    struct LuaColor3 { float r = 1, g = 1, b = 1; };
    struct LuaUDim2 { float xScale = 0, xOffset = 0, yScale = 0, yOffset = 0; };
    struct MemoryState { std::size_t used = 0; std::size_t limit = 64u * 1024u * 1024u; };

    static LocalScriptRuntime *FromLua(lua_State *state);
    static void *Allocate(void *userData, void *pointer, std::size_t oldSize, std::size_t newSize);
    static void Interrupt(lua_State *state, int gc);
    static int InstanceIndex(lua_State *state);
    static int InstanceNewIndex(lua_State *state);
    static int InstanceNew(lua_State *state);
    static int FindFirstChild(lua_State *state);
    static int SignalConnect(lua_State *state);
    static int GetService(lua_State *state);
    static int ColorFromRGB(lua_State *state);
    static int UDim2New(lua_State *state);
    static int UDim2FromScale(lua_State *state);
    static int UDim2FromOffset(lua_State *state);
    static int TaskWait(lua_State *state);
    static int TaskSpawn(lua_State *state);
    static int TaskDelay(lua_State *state);
    static int TaskDefer(lua_State *state);
    static int Print(lua_State *state);

    bool InstallEnvironment(lua_State *thread, const PublishedWorld::LocalScript &script);
    LuaInstance *CheckInstance(lua_State *state, int index) const;
    void PushInstance(lua_State *state, std::string_view id);
    void PushColor(lua_State *state, Color value);
    void PushUDim2(lua_State *state, LuaUDim2 value);
    void Connect(lua_State *caller, int functionIndex, std::string target, std::string event);
    void QueueFunction(int functionRef, double wakeTime);
    void Resume(int threadRef, bool resumeWithDelta = false, double elapsed = 0.0);
    void LogError(std::string_view script, const char *message) const;

    lua_State *state_ = nullptr;
    PublishedWorld *world_ = nullptr;
    std::priority_queue<Job, std::vector<Job>, Later> jobs_;
    std::vector<Callback> callbacks_;
    std::vector<int> scriptThreadRefs_;
    std::uint64_t nextJobOrder_ = 1;
    double now_ = 0.0;
    std::string activeScript_;
    std::chrono::steady_clock::time_point deadline_ = std::chrono::steady_clock::time_point::max();
    std::chrono::steady_clock::time_point frameDeadline_ = std::chrono::steady_clock::time_point::max();
    MemoryState memory_{};
};
