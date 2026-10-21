#include "ScriptEngine.hpp"

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string_view>

namespace blubox::studio {
namespace {

constexpr std::size_t kMaximumScriptBytes = 256 * 1024;
constexpr int kNoReference = LUA_NOREF;
constexpr const char* kScriptPhysicsType = "ScriptPhysics";

std::mt19937& RandomGenerator() {
    static std::mt19937 generator(std::random_device{}());
    return generator;
}

void PushVec(lua_State* state, const std::array<float, 3>& value) {
    lua_createtable(state, 3, 3);
    lua_pushnumber(state, value[0]); lua_setfield(state, -2, "x");
    lua_pushnumber(state, value[0]); lua_rawseti(state, -2, 1);
    lua_pushnumber(state, value[1]); lua_setfield(state, -2, "y");
    lua_pushnumber(state, value[1]); lua_rawseti(state, -2, 2);
    lua_pushnumber(state, value[2]); lua_setfield(state, -2, "z");
    lua_pushnumber(state, value[2]); lua_rawseti(state, -2, 3);
}

std::array<float, 3> ReadVec(lua_State* state, int index) {
    if (!lua_istable(state, index)) luaL_error(state, "Expected a Vector3 table.");
    const int absolute = lua_absindex(state, index);
    constexpr const char* names[] = {"x", "y", "z"};
    std::array<float, 3> result{};
    for (int axis = 0; axis < 3; ++axis) {
        lua_getfield(state, absolute, names[axis]);
        if (!lua_isnumber(state, -1)) {
            lua_pop(state, 1);
            lua_rawgeti(state, absolute, axis + 1);
        }
        if (!lua_isnumber(state, -1)) {
            lua_pop(state, 1);
            luaL_error(state, "Vector3 needs numeric x, y and z values.");
        }
        result[axis] = static_cast<float>(lua_tonumber(state, -1));
        lua_pop(state, 1);
        if (!std::isfinite(result[axis])) luaL_error(state, "Vector3 values must be finite.");
    }
    return result;
}

void SetCallbackClosure(lua_State* state, const char* name, lua_CFunction callback, ScriptEngine* engine) {
    lua_pushlightuserdata(state, engine);
    lua_pushcclosure(state, callback, name, 1);
    lua_setfield(state, -2, name);
}

} // namespace

ScriptEngine::ScriptEngine(MapDocument& map) : map_(map) {
    state_ = lua_newstate(&ScriptEngine::Allocate, &memory_);
    if (!state_) throw std::runtime_error("Could not allocate the Luau VM (64 MiB memory limit).");
    luaL_openlibs(state_);
    lua_callbacks(state_)->userdata = this;
    lua_callbacks(state_)->interrupt = &ScriptEngine::Interrupt;
    RegisterBindings();
    // Luau's openlibs includes os/debug; neither belongs in user-authored map scripts.
    lua_pushnil(state_); lua_setglobal(state_, "os");
    lua_pushnil(state_); lua_setglobal(state_, "debug");
    lua_pushnil(state_); lua_setglobal(state_, "loadstring");
    lua_pushnil(state_); lua_setglobal(state_, "dofile");
    lua_pushnil(state_); lua_setglobal(state_, "loadfile");
    lua_pushnil(state_); lua_setglobal(state_, "require");
    luaL_sandbox(state_);
}

ScriptEngine::~ScriptEngine() {
    if (state_) lua_close(state_);
}

void* ScriptEngine::Allocate(void* userData, void* pointer, std::size_t oldSize, std::size_t newSize) {
    auto& memory = *static_cast<MemoryState*>(userData);
    if (newSize == 0) {
        if (pointer) memory.used = oldSize > memory.used ? 0 : memory.used - oldSize;
        std::free(pointer);
        return nullptr;
    }
    const std::size_t previous = pointer ? oldSize : 0;
    if (previous > memory.used) memory.used = 0;
    const std::size_t current = memory.used - previous;
    if (newSize > memory.limit || current > memory.limit - newSize) return nullptr;
    void* result = std::realloc(pointer, newSize);
    if (result) memory.used = current + newSize;
    return result;
}

void ScriptEngine::Interrupt(lua_State* state, int gc) {
    (void)gc;
    auto* engine = static_cast<ScriptEngine*>(lua_callbacks(state)->userdata);
    if (engine && std::chrono::steady_clock::now() > engine->executionDeadline_) {
        luaL_error(state, "Luau execution budget exceeded (8 ms per tick / 20 ms per run).");
    }
}

ScriptEngine* ScriptEngine::FromLua(lua_State* state) {
    return static_cast<ScriptEngine*>(lua_touserdata(state, lua_upvalueindex(1)));
}

void ScriptEngine::RegisterBindings() {
    lua_pushlightuserdata(state_, this);
    lua_pushcclosure(state_, ScriptPrint, "print", 1);
    lua_setglobal(state_, "print");
    lua_pushlightuserdata(state_, this);
    lua_pushcclosure(state_, ScriptWarn, "warn", 1);
    lua_setglobal(state_, "warn");

    lua_newtable(state_);
    SetCallbackClosure(state_, "broadcast", Broadcast, this);
    SetCallbackClosure(state_, "getEntity", GetEntity, this);
    SetCallbackClosure(state_, "spawnEntity", SpawnEntity, this);
    SetCallbackClosure(state_, "destroyEntity", DestroyEntity, this);
    SetCallbackClosure(state_, "getTime", GetTime, this);
    SetCallbackClosure(state_, "findEntitiesInRadius", FindEntitiesInRadius, this);
    lua_setglobal(state_, "game");

    lua_newtable(state_);
    SetCallbackClosure(state_, "getName", BodyGetName, this);
    SetCallbackClosure(state_, "getPosition", BodyGetPosition, this);
    SetCallbackClosure(state_, "setPosition", BodySetPosition, this);
    SetCallbackClosure(state_, "getRotation", BodyGetRotation, this);
    SetCallbackClosure(state_, "setRotation", BodySetRotation, this);
    SetCallbackClosure(state_, "setColor", BodySetColor, this);
    SetCallbackClosure(state_, "setVisible", BodySetVisible, this);
    SetCallbackClosure(state_, "applyForce", BodyApplyForce, this);
    SetCallbackClosure(state_, "applyImpulse", BodyApplyImpulse, this);
    SetCallbackClosure(state_, "onTouch", BodyOnTouch, this);
    SetCallbackClosure(state_, "onUntouch", BodyOnUntouch, this);
    SetCallbackClosure(state_, "onTick", BodyOnTick, this);
    lua_setglobal(state_, "body");

    lua_getglobal(state_, "math");
    SetCallbackClosure(state_, "createVector3", CreateVector3, this);
    SetCallbackClosure(state_, "distance", Distance, this);
    SetCallbackClosure(state_, "lerp", Lerp, this);
    SetCallbackClosure(state_, "clamp", Clamp, this);
    SetCallbackClosure(state_, "randomRange", RandomRange, this);
    SetCallbackClosure(state_, "randomInt", RandomInt, this);
    lua_pop(state_, 1);
}

Entity* ScriptEngine::FindEntity(const std::string& idOrName) {
    const auto found = std::find_if(map_.entities.begin(), map_.entities.end(), [&](const Entity& entity) {
        return entity.id == idOrName || entity.name == idOrName;
    });
    return found == map_.entities.end() ? nullptr : &*found;
}

Entity* ScriptEngine::ActiveEntity() { return FindEntity(activeEntityId_); }

void ScriptEngine::PushVector3(lua_State* state, const std::array<float, 3>& vector) const {
    PushVec(state, vector);
}

void ScriptEngine::PushEntity(lua_State* state, const Entity& entity) const {
    lua_createtable(state, 0, 5);
    lua_pushlstring(state, entity.id.data(), entity.id.size()); lua_setfield(state, -2, "id");
    lua_pushlstring(state, entity.name.data(), entity.name.size()); lua_setfield(state, -2, "name");
    lua_pushlstring(state, entity.kind.data(), entity.kind.size()); lua_setfield(state, -2, "kind");
    PushVector3(state, entity.transform.position); lua_setfield(state, -2, "position");
    PushVector3(state, entity.transform.rotation); lua_setfield(state, -2, "rotation");
}

std::array<float, 3> ScriptEngine::ReadVector3(lua_State* state, int index) { return ReadVec(state, index); }

bool ScriptEngine::RunEntity(const std::string& entityId, std::string& error) {
    Entity* entity = FindEntity(entityId);
    if (!entity) { error = "Selected object no longer exists."; return false; }
    if (entity->luauScript.size() > kMaximumScriptBytes) { error = "Script exceeds 256 KiB."; return false; }
    StopEntity(entityId);

    lua_State* thread = lua_newthread(state_);
    const int threadReference = lua_ref(state_, -1);
    lua_pop(state_, 1);
    luaL_sandboxthread(thread);
    Runtime runtime;
    runtime.thread = thread;
    runtime.threadReference = threadReference;
    runtime.source = entity->luauScript;
    const bool isScriptObject = entity->kind == "script" || entity->kind == "localscript";
    if (isScriptObject) {
        if (Entity* parent = FindEntity(entity->parent);
            parent && parent->kind != "script" && parent->kind != "localscript") {
            runtime.hostEntityId = parent->id;
        }
    } else {
        // Legacy scripts stored directly on a part keep working.
        runtime.hostEntityId = entity->id;
    }
    runtimes_.emplace(entityId, std::move(runtime));

    activeScriptId_ = entityId;
    activeEntityId_ = runtimes_.at(entityId).hostEntityId;
    const std::string bytecode = Luau::compile(entity->luauScript);
    const std::string chunkName = "@blubox/" + entity->name;
    if (luau_load(thread, chunkName.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
        error = lua_tostring(thread, -1) ? lua_tostring(thread, -1) : "Luau compile/load failed.";
        StopEntity(entityId);
        activeEntityId_.clear();
        activeScriptId_.clear();
        return false;
    }
    executionDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    const int status = lua_pcall(thread, 0, 0, 0);
    executionDeadline_ = std::chrono::steady_clock::time_point::max();
    activeEntityId_.clear();
    activeScriptId_.clear();
    if (status != LUA_OK) {
        error = lua_tostring(thread, -1) ? lua_tostring(thread, -1) : "Luau script execution failed.";
        StopEntity(entityId);
        return false;
    }
    error.clear();
    lastRuntimeError_.clear();
    return true;
}

void ScriptEngine::StopEntity(const std::string& entityId) {
    auto found = runtimes_.find(entityId);
    if (found == runtimes_.end()) return;
    Runtime& runtime = found->second;
    if (runtime.tickReference != kNoReference) lua_unref(state_, runtime.tickReference);
    if (runtime.touchReference != kNoReference) lua_unref(state_, runtime.touchReference);
    if (runtime.untouchReference != kNoReference) lua_unref(state_, runtime.untouchReference);
    if (runtime.threadReference != kNoReference) lua_unref(state_, runtime.threadReference);
    runtimes_.erase(found);
}

void ScriptEngine::Tick(double deltaSeconds) {
    std::vector<std::string> entityIds;
    entityIds.reserve(runtimes_.size());
    for (const auto& [id, runtime] : runtimes_) {
        (void)runtime;
        entityIds.push_back(id);
    }
    for (const std::string& id : entityIds) {
        auto found = runtimes_.find(id);
        if (found == runtimes_.end()) continue;
        Entity* entity = FindEntity(id);
        if (!entity) { StopEntity(id); continue; }
        Runtime& runtime = found->second;
        if (runtime.source != entity->luauScript) { StopEntity(id); continue; }
        if (runtime.tickReference == kNoReference) continue;
        activeScriptId_ = id;
        activeEntityId_ = runtime.hostEntityId;
        lua_settop(runtime.thread, 0);
        lua_getref(runtime.thread, runtime.tickReference);
        lua_pushnumber(runtime.thread, deltaSeconds);
        executionDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        const int status = lua_pcall(runtime.thread, 1, 0, 0);
        executionDeadline_ = std::chrono::steady_clock::time_point::max();
        activeEntityId_.clear();
        activeScriptId_.clear();
        if (status != LUA_OK) {
            lastRuntimeError_ = lua_tostring(runtime.thread, -1) ? lua_tostring(runtime.thread, -1) : "onTick failed.";
            StopEntity(id);
        }
    }
}

void ScriptEngine::DispatchTouch(const std::string& entityId, const std::string& otherEntityId, bool touching) {
    std::vector<std::string> matchingScripts;
    for (const auto& [scriptId, runtime] : runtimes_) {
        if (scriptId == entityId || runtime.hostEntityId == entityId) matchingScripts.push_back(scriptId);
    }
    for (const std::string& scriptId : matchingScripts) {
        auto found = runtimes_.find(scriptId);
        if (found == runtimes_.end()) continue;
        Runtime& runtime = found->second;
        const int reference = touching ? runtime.touchReference : runtime.untouchReference;
        if (reference == kNoReference) continue;
        activeScriptId_ = scriptId;
        activeEntityId_ = runtime.hostEntityId;
        lua_settop(runtime.thread, 0);
        lua_getref(runtime.thread, reference);
        if (Entity* other = FindEntity(otherEntityId)) PushEntity(runtime.thread, *other);
        else lua_pushnil(runtime.thread);
        executionDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
        const int status = lua_pcall(runtime.thread, 1, 0, 0);
        executionDeadline_ = std::chrono::steady_clock::time_point::max();
        activeEntityId_.clear();
        activeScriptId_.clear();
        if (status != LUA_OK) {
            lastRuntimeError_ = lua_tostring(runtime.thread, -1) ? lua_tostring(runtime.thread, -1) : "Touch callback failed.";
            StopEntity(scriptId);
        }
    }
}

void ScriptEngine::SetBroadcastHandler(std::function<void(std::string)> handler) {
    broadcastHandler_ = std::move(handler);
}

void ScriptEngine::SetOutputHandler(std::function<void(std::string, bool)> handler) {
    outputHandler_ = std::move(handler);
}

void ScriptEngine::RegisterCallback(lua_State* state, CallbackKind kind) {
    const auto found = runtimes_.find(activeScriptId_);
    if (found == runtimes_.end() || !lua_isfunction(state, 1)) return;
    lua_pushvalue(state, 1);
    const int reference = lua_ref(state, 1);
    Runtime& runtime = found->second;
    int* slot = kind == CallbackKind::Tick ? &runtime.tickReference
        : kind == CallbackKind::Touch ? &runtime.touchReference : &runtime.untouchReference;
    if (*slot != kNoReference) lua_unref(state_, *slot);
    *slot = reference;
}

int ScriptEngine::Broadcast(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    const char* message = luaL_checkstring(state, 1);
    if (engine->broadcastHandler_) engine->broadcastHandler_(message);
    return 0;
}

int ScriptEngine::ScriptPrint(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    if (!engine->outputHandler_) return 0;
    std::string message;
    const int argumentCount = lua_gettop(state);
    for (int index = 1; index <= argumentCount; ++index) {
        if (index > 1) message.push_back('\t');
        std::size_t length = 0;
        const char* value = luaL_tolstring(state, index, &length);
        if (value && message.size() < 8192) {
            const std::size_t count = std::min<std::size_t>(length, 8192 - message.size());
            message.append(value, count);
        }
        lua_pop(state, 1);
    }
    engine->outputHandler_(std::move(message), false);
    return 0;
}

int ScriptEngine::ScriptWarn(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    if (!engine->outputHandler_) return 0;
    std::string message;
    const int argumentCount = lua_gettop(state);
    for (int index = 1; index <= argumentCount; ++index) {
        if (index > 1) message.push_back('\t');
        std::size_t length = 0;
        const char* value = luaL_tolstring(state, index, &length);
        if (value && message.size() < 8192) {
            const std::size_t count = std::min<std::size_t>(length, 8192 - message.size());
            message.append(value, count);
        }
        lua_pop(state, 1);
    }
    engine->outputHandler_(std::move(message), true);
    return 0;
}

int ScriptEngine::GetEntity(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    Entity* entity = engine->FindEntity(luaL_checkstring(state, 1));
    if (entity) engine->PushEntity(state, *entity); else lua_pushnil(state);
    return 1;
}

int ScriptEngine::SpawnEntity(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    const char* kind = luaL_optstring(state, 1, "block");
    try {
        Entity& entity = engine->map_.AddEntity(kind);
        entity.transform.position = {0.0f, 1.0f, 0.0f};
        if (!lua_isnoneornil(state, 2)) entity.name = luaL_checkstring(state, 2);
        engine->PushEntity(state, entity);
    } catch (const std::exception& exception) {
        lua_pushnil(state);
        lua_pushstring(state, exception.what());
        return 2;
    }
    return 1;
}

int ScriptEngine::DestroyEntity(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    const char* id = luaL_checkstring(state, 1);
    auto found = std::find_if(engine->map_.entities.begin(), engine->map_.entities.end(),
        [&](const Entity& entity) { return entity.id == id; });
    if (found == engine->map_.entities.end()) { lua_pushboolean(state, false); return 1; }
    engine->map_.entities.erase(found);
    lua_pushboolean(state, true);
    return 1;
}

int ScriptEngine::GetTime(lua_State* state) {
    const auto elapsed = std::chrono::steady_clock::now() - FromLua(state)->startedAt_;
    lua_pushnumber(state, std::chrono::duration<double>(elapsed).count());
    return 1;
}

int ScriptEngine::FindEntitiesInRadius(lua_State* state) {
    ScriptEngine* engine = FromLua(state);
    const auto center = ReadVector3(state, 1);
    const float radius = static_cast<float>(luaL_checknumber(state, 2));
    const float radiusSquared = radius * radius;
    lua_newtable(state);
    int resultIndex = 1;
    for (const Entity& entity : engine->map_.entities) {
        float distanceSquared = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float delta = entity.transform.position[axis] - center[axis];
            distanceSquared += delta * delta;
        }
        if (distanceSquared <= radiusSquared) {
            engine->PushEntity(state, entity);
            lua_rawseti(state, -2, resultIndex++);
        }
    }
    return 1;
}

int ScriptEngine::CreateVector3(lua_State* state) {
    const std::array<float, 3> value{static_cast<float>(luaL_checknumber(state, 1)),
        static_cast<float>(luaL_checknumber(state, 2)), static_cast<float>(luaL_checknumber(state, 3))};
    PushVec(state, value);
    return 1;
}

int ScriptEngine::Distance(lua_State* state) {
    const auto a = ReadVector3(state, 1);
    const auto b = ReadVector3(state, 2);
    float sum = 0.0f;
    for (int axis = 0; axis < 3; ++axis) { const float d = a[axis] - b[axis]; sum += d * d; }
    lua_pushnumber(state, std::sqrt(sum));
    return 1;
}

int ScriptEngine::Lerp(lua_State* state) {
    const double a = luaL_checknumber(state, 1);
    const double b = luaL_checknumber(state, 2);
    const double alpha = luaL_checknumber(state, 3);
    lua_pushnumber(state, a + (b - a) * alpha);
    return 1;
}

int ScriptEngine::Clamp(lua_State* state) {
    const double value = luaL_checknumber(state, 1);
    const double minimum = luaL_checknumber(state, 2);
    const double maximum = luaL_checknumber(state, 3);
    lua_pushnumber(state, std::clamp(value, std::min(minimum, maximum), std::max(minimum, maximum)));
    return 1;
}

int ScriptEngine::RandomRange(lua_State* state) {
    double minimum = luaL_checknumber(state, 1);
    double maximum = luaL_checknumber(state, 2);
    if (minimum > maximum) std::swap(minimum, maximum);
    std::uniform_real_distribution<double> distribution(minimum, maximum);
    lua_pushnumber(state, distribution(RandomGenerator()));
    return 1;
}

int ScriptEngine::RandomInt(lua_State* state) {
    lua_Integer minimum = luaL_checkinteger(state, 1);
    lua_Integer maximum = luaL_checkinteger(state, 2);
    if (minimum > maximum) std::swap(minimum, maximum);
    std::uniform_int_distribution<lua_Integer> distribution(minimum, maximum);
    lua_pushinteger(state, distribution(RandomGenerator()));
    return 1;
}

int ScriptEngine::BodyGetName(lua_State* state) {
    Entity* entity = FromLua(state)->ActiveEntity();
    if (entity) lua_pushlstring(state, entity->name.data(), entity->name.size()); else lua_pushnil(state);
    return 1;
}

int ScriptEngine::BodyGetPosition(lua_State* state) {
    Entity* entity = FromLua(state)->ActiveEntity();
    if (entity) PushVec(state, entity->transform.position); else lua_pushnil(state);
    return 1;
}

int ScriptEngine::BodySetPosition(lua_State* state) {
    if (Entity* entity = FromLua(state)->ActiveEntity()) entity->transform.position = ReadVector3(state, 1);
    return 0;
}

int ScriptEngine::BodyGetRotation(lua_State* state) {
    Entity* entity = FromLua(state)->ActiveEntity();
    if (entity) PushVec(state, entity->transform.rotation); else lua_pushnil(state);
    return 1;
}

int ScriptEngine::BodySetRotation(lua_State* state) {
    if (Entity* entity = FromLua(state)->ActiveEntity()) entity->transform.rotation = ReadVector3(state, 1);
    return 0;
}

int ScriptEngine::BodySetColor(lua_State* state) {
    if (Entity* entity = FromLua(state)->ActiveEntity()) {
        for (int channel = 0; channel < 3; ++channel) {
            const lua_Integer value = luaL_checkinteger(state, channel + 1);
            entity->color[channel] = static_cast<unsigned char>(std::clamp<lua_Integer>(value, 0, 255));
        }
        if (!lua_isnoneornil(state, 4)) {
            const lua_Integer alpha = luaL_checkinteger(state, 4);
            entity->color[3] = static_cast<unsigned char>(std::clamp<lua_Integer>(alpha, 0, 255));
        }
    }
    return 0;
}

int ScriptEngine::BodySetVisible(lua_State* state) {
    if (Entity* entity = FromLua(state)->ActiveEntity()) entity->visible = lua_toboolean(state, 1) != 0;
    return 0;
}

void ScriptEngine::SetPhysicsVector(const char* field, lua_State* state) {
    Entity* entity = ActiveEntity();
    if (!entity) return;
    const auto value = ReadVector3(state, 1);
    auto component = std::find_if(entity->components.begin(), entity->components.end(), [](const nlohmann::json& item) {
        return item.value("type", std::string{}) == kScriptPhysicsType;
    });
    if (component == entity->components.end()) {
        entity->components.push_back({{"type", kScriptPhysicsType}});
        component = std::prev(entity->components.end());
    }
    (*component)[field] = value;
}

int ScriptEngine::BodyApplyForce(lua_State* state) {
    FromLua(state)->SetPhysicsVector("pendingForce", state);
    return 0;
}

int ScriptEngine::BodyApplyImpulse(lua_State* state) {
    FromLua(state)->SetPhysicsVector("pendingImpulse", state);
    return 0;
}

int ScriptEngine::BodyOnTouch(lua_State* state) {
    FromLua(state)->RegisterCallback(state, CallbackKind::Touch);
    return 0;
}

int ScriptEngine::BodyOnUntouch(lua_State* state) {
    FromLua(state)->RegisterCallback(state, CallbackKind::Untouch);
    return 0;
}

int ScriptEngine::BodyOnTick(lua_State* state) {
    FromLua(state)->RegisterCallback(state, CallbackKind::Tick);
    return 0;
}

} // namespace blubox::studio
