#pragma once

#include "MapDocument.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

struct lua_State;

namespace blubox::studio {

class ScriptEngine {
public:
    explicit ScriptEngine(MapDocument& map);
    ~ScriptEngine();
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    bool RunEntity(const std::string& entityId, std::string& error);
    void StopEntity(const std::string& entityId);
    void Tick(double deltaSeconds);
    void DispatchTouch(const std::string& entityId, const std::string& otherEntityId, bool touching);
    void SetBroadcastHandler(std::function<void(std::string)> handler);
    void SetOutputHandler(std::function<void(std::string, bool)> handler);
    const std::string& lastRuntimeError() const { return lastRuntimeError_; }

private:
    struct MemoryState { std::size_t used = 0; std::size_t limit = 64 * 1024 * 1024; };
    struct Runtime {
        lua_State* thread = nullptr;
        int threadReference = -1;
        int tickReference = -1;
        int touchReference = -1;
        int untouchReference = -1;
        std::string source;
        std::string hostEntityId;
    };

    enum class CallbackKind { Tick, Touch, Untouch };
    static void* Allocate(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize);
    static void Interrupt(lua_State* state, int gc);
    static ScriptEngine* FromLua(lua_State* state);
    static int ScriptPrint(lua_State* state);
    static int ScriptWarn(lua_State* state);
    static int Broadcast(lua_State* state);
    static int GetEntity(lua_State* state);
    static int SpawnEntity(lua_State* state);
    static int DestroyEntity(lua_State* state);
    static int GetTime(lua_State* state);
    static int FindEntitiesInRadius(lua_State* state);
    static int CreateVector3(lua_State* state);
    static int Distance(lua_State* state);
    static int Lerp(lua_State* state);
    static int Clamp(lua_State* state);
    static int RandomRange(lua_State* state);
    static int RandomInt(lua_State* state);
    static int BodyGetName(lua_State* state);
    static int BodyGetPosition(lua_State* state);
    static int BodySetPosition(lua_State* state);
    static int BodyGetRotation(lua_State* state);
    static int BodySetRotation(lua_State* state);
    static int BodySetColor(lua_State* state);
    static int BodySetVisible(lua_State* state);
    static int BodyApplyForce(lua_State* state);
    static int BodyApplyImpulse(lua_State* state);
    static int BodyOnTouch(lua_State* state);
    static int BodyOnUntouch(lua_State* state);
    static int BodyOnTick(lua_State* state);

    void RegisterBindings();
    Entity* ActiveEntity();
    Entity* FindEntity(const std::string& idOrName);
    void RegisterCallback(lua_State* state, CallbackKind kind);
    void RemoveRuntime(const std::string& entityId);
    void SetPhysicsVector(const char* field, lua_State* state);
    void PushEntity(lua_State* state, const Entity& entity) const;
    void PushVector3(lua_State* state, const std::array<float, 3>& vector) const;
    static std::array<float, 3> ReadVector3(lua_State* state, int index);

    MapDocument& map_;
    MemoryState memory_;
    lua_State* state_ = nullptr;
    std::unordered_map<std::string, Runtime> runtimes_;
    std::string activeEntityId_;
    std::string activeScriptId_;
    std::string lastRuntimeError_;
    std::chrono::steady_clock::time_point startedAt_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point executionDeadline_ = std::chrono::steady_clock::time_point::max();
    std::function<void(std::string)> broadcastHandler_;
    std::function<void(std::string, bool)> outputHandler_;
};

} // namespace blubox::studio
