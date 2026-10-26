#include "LocalScriptRuntime.hpp"

#include <Luau/Compiler.h>
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {
constexpr const char *kInstanceMeta = "Blubox.GuiInstance";
constexpr const char *kSignalMeta = "Blubox.GuiSignal";
constexpr const char *kColorMeta = "Blubox.Color3";
constexpr const char *kUDim2Meta = "Blubox.UDim2";
constexpr std::size_t kMaxScripts = 256;
constexpr std::size_t kMaxSourceBytes = 256u * 1024u;
constexpr std::size_t kMaxTotalSourceBytes = 2u * 1024u * 1024u;
constexpr std::size_t kMaxJobsPerFrame = 128;

template <typename T>
T *NewUserdata(lua_State *state, const char *metatable) {
    auto *value = static_cast<T *>(lua_newuserdatatagged(state, sizeof(T), 0));
    ::new (value) T{};
    luaL_getmetatable(state, metatable);
    lua_setmetatable(state, -2);
    return value;
}

template <typename T>
T *TestUserdata(lua_State *state, int index, const char *metatable) {
    if (lua_type(state, index) != LUA_TUSERDATA || !lua_getmetatable(state, index)) return nullptr;
    luaL_getmetatable(state, metatable);
    const bool matches = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    return matches ? static_cast<T *>(lua_touserdata(state, index)) : nullptr;
}

template <std::size_t N>
void CopyId(char (&out)[N], std::string_view value) {
    const std::size_t length = std::min(value.size(), N - 1);
    std::memcpy(out, value.data(), length);
    out[length] = '\0';
}

void RemoveGlobal(lua_State *state, const char *name) {
    lua_pushnil(state);
    lua_setglobal(state, name);
}
}

LocalScriptRuntime::~LocalScriptRuntime() { Stop(); }

LocalScriptRuntime *LocalScriptRuntime::FromLua(lua_State *state) {
    return static_cast<LocalScriptRuntime *>(lua_touserdata(state, lua_upvalueindex(1)));
}

void *LocalScriptRuntime::Allocate(void *userData, void *pointer,
                                   std::size_t oldSize, std::size_t newSize) {
    auto &memory = *static_cast<MemoryState *>(userData);
    if (newSize == 0) {
        if (pointer) memory.used = oldSize > memory.used ? 0 : memory.used - oldSize;
        std::free(pointer);
        return nullptr;
    }
    const std::size_t previous = pointer ? oldSize : 0;
    const std::size_t current = previous > memory.used ? 0 : memory.used - previous;
    if (newSize > memory.limit || current > memory.limit - newSize) return nullptr;
    void *result = std::realloc(pointer, newSize);
    if (result) memory.used = current + newSize;
    return result;
}

void LocalScriptRuntime::Interrupt(lua_State *state, int gc) {
    (void)gc;
    auto *runtime = static_cast<LocalScriptRuntime *>(lua_callbacks(state)->userdata);
    if (runtime && std::chrono::steady_clock::now() > runtime->deadline_)
        luaL_error(state, "LocalScript exceeded the client frame execution budget");
}

bool LocalScriptRuntime::Start(PublishedWorld &world) {
    Stop();
    world_ = &world;
    if (world.LocalScripts().size() > kMaxScripts) {
        TraceLog(LOG_WARNING, "LocalScript runtime skipped: map contains more than %d client scripts.",
                 static_cast<int>(kMaxScripts));
        return false;
    }

    memory_ = MemoryState{};
    state_ = lua_newstate(&LocalScriptRuntime::Allocate, &memory_);
    if (!state_) {
        TraceLog(LOG_ERROR, "Could not create the client Luau VM.");
        return false;
    }
    luaL_openlibs(state_);
    lua_callbacks(state_)->userdata = this;
    lua_callbacks(state_)->interrupt = &LocalScriptRuntime::Interrupt;
    for (const char *unsafe : {"io", "os", "debug", "dofile", "loadfile", "loadstring", "require"})
        RemoveGlobal(state_, unsafe);

    if (luaL_newmetatable(state_, kInstanceMeta)) {
        lua_pushcfunction(state_, InstanceIndex, "GuiInstance.__index");
        lua_setfield(state_, -2, "__index");
        lua_pushcfunction(state_, InstanceNewIndex, "GuiInstance.__newindex");
        lua_setfield(state_, -2, "__newindex");
    }
    lua_pop(state_, 1);
    if (luaL_newmetatable(state_, kSignalMeta)) {
        lua_newtable(state_);
        lua_pushcfunction(state_, SignalConnect, "RBXScriptSignal.Connect");
        lua_setfield(state_, -2, "Connect");
        lua_setfield(state_, -2, "__index");
    }
    lua_pop(state_, 1);
    if (luaL_newmetatable(state_, kColorMeta)) lua_pushliteral(state_, "Color3"), lua_setfield(state_, -2, "__name");
    lua_pop(state_, 1);
    if (luaL_newmetatable(state_, kUDim2Meta)) lua_pushliteral(state_, "UDim2"), lua_setfield(state_, -2, "__name");
    lua_pop(state_, 1);

    luaL_sandbox(state_);
    std::size_t totalBytes = 0;
    std::size_t loaded = 0;
    for (const auto &script : world.LocalScripts()) {
        if (script.source.empty() || script.source.size() > kMaxSourceBytes ||
            totalBytes > kMaxTotalSourceBytes - script.source.size()) continue;
        totalBytes += script.source.size();

        lua_State *thread = lua_newthread(state_);
        lua_callbacks(thread)->userdata = this;
        lua_callbacks(thread)->interrupt = &LocalScriptRuntime::Interrupt;
        const int threadRef = lua_ref(state_, -1);
        lua_pop(state_, 1);
        scriptThreadRefs_.push_back(threadRef);
        luaL_sandboxthread(thread);
        if (!InstallEnvironment(thread, script)) continue;

        std::string bytecode;
        try {
            bytecode = Luau::compile(script.source);
        } catch (const std::exception &exception) {
            LogError(script.name, exception.what());
            continue;
        }
        const std::string chunkName = "@client/" + script.name;
        if (luau_load(thread, chunkName.c_str(), bytecode.data(), bytecode.size(), 0) != 0) {
            const char *message = lua_tostring(thread, -1);
            LogError(script.name, message ? message : "Luau load failed");
            lua_settop(thread, 0);
            continue;
        }
        activeScript_ = script.name;
        Resume(threadRef);
        activeScript_.clear();
        ++loaded;
    }
    TraceLog(LOG_INFO, "Client Luau runtime started (%d LocalScripts loaded).", static_cast<int>(loaded));
    return true;
}

bool LocalScriptRuntime::InstallEnvironment(lua_State *thread,
                                            const PublishedWorld::LocalScript &script) {
    lua_newtable(thread);
    lua_pushlstring(thread, script.name.data(), script.name.size());
    lua_setfield(thread, -2, "Name");
    PushInstance(thread, script.parent);
    lua_setfield(thread, -2, "Parent");
    lua_setglobal(thread, "script");

    lua_newtable(thread);
    lua_pushlightuserdata(thread, this);
    lua_pushcclosure(thread, GetService, "game.GetService", 1);
    lua_setfield(thread, -2, "GetService");
    lua_setglobal(thread, "game");

    // `player` and the `Players.LocalPlayer` path intentionally expose only
    // PlayerGui; identity and authenticated session data remain unavailable.
    lua_newtable(thread);
    lua_newtable(thread);
    lua_pushliteral(thread, "Player"); lua_setfield(thread, -2, "Name");
    PushInstance(thread, "PlayerGui"); lua_setfield(thread, -2, "PlayerGui");
    lua_setfield(thread, -2, "LocalPlayer");
    lua_setglobal(thread, "Players");
    lua_getglobal(thread, "Players");
    lua_getfield(thread, -1, "LocalPlayer");
    lua_setglobal(thread, "player");
    lua_getglobal(thread, "player");
    lua_getfield(thread, -1, "PlayerGui");
    lua_setglobal(thread, "PlayerGui");
    lua_pop(thread, 2);

    lua_newtable(thread);
    lua_getglobal(thread, "Players");
    lua_setfield(thread, -2, "Players");
    lua_pushcfunction(thread, ColorFromRGB, "Color3.fromRGB");
    lua_setfield(thread, -2, "fromRGB");
    lua_pushcfunction(thread, ColorFromRGB, "Color3.new");
    lua_setfield(thread, -2, "new");
    lua_setglobal(thread, "Color3");

    lua_newtable(thread);
    lua_pushcfunction(thread, UDim2New, "UDim2.new"); lua_setfield(thread, -2, "new");
    lua_pushcfunction(thread, UDim2FromScale, "UDim2.fromScale"); lua_setfield(thread, -2, "fromScale");
    lua_pushcfunction(thread, UDim2FromOffset, "UDim2.fromOffset"); lua_setfield(thread, -2, "fromOffset");
    lua_setglobal(thread, "UDim2");

    lua_newtable(thread);
    lua_pushcfunction(thread, FindFirstChild, "Instance.FindFirstChild");
    lua_setfield(thread, -2, "FindFirstChild");
    lua_pushlightuserdata(thread, this);
    lua_pushcclosure(thread, InstanceNew, "Instance.new", 1);
    lua_setfield(thread, -2, "new");
    lua_setglobal(thread, "Instance");

    lua_newtable(thread);
    const struct { const char *name; lua_CFunction function; bool needsRuntime; } taskFunctions[] = {
        {"wait", TaskWait, false}, {"spawn", TaskSpawn, true},
        {"delay", TaskDelay, true}, {"defer", TaskDefer, true}
    };
    for (const auto &entry : taskFunctions) {
        if (entry.needsRuntime) {
            lua_pushlightuserdata(thread, this);
            lua_pushcclosure(thread, entry.function, entry.name, 1);
        } else {
            lua_pushcfunction(thread, entry.function, entry.name);
        }
        lua_setfield(thread, -2, entry.name);
    }
    lua_setglobal(thread, "task");

    lua_pushlightuserdata(thread, this);
    lua_pushcclosure(thread, Print, "print", 1);
    lua_setglobal(thread, "print");
    lua_pushlightuserdata(thread, this);
    lua_pushcclosure(thread, Print, "warn", 1);
    lua_setglobal(thread, "warn");
    return true;
}

LocalScriptRuntime::LuaInstance *LocalScriptRuntime::CheckInstance(lua_State *state, int index) const {
    return static_cast<LuaInstance *>(luaL_checkudata(state, index, kInstanceMeta));
}

void LocalScriptRuntime::PushInstance(lua_State *state, std::string_view id) {
    if (id.empty() || id.size() >= sizeof(LuaInstance::id)) {
        lua_pushnil(state);
        return;
    }
    auto *instance = NewUserdata<LuaInstance>(state, kInstanceMeta);
    instance->runtime = this;
    CopyId(instance->id, id);
}

void LocalScriptRuntime::PushColor(lua_State *state, Color value) {
    auto *color = NewUserdata<LuaColor3>(state, kColorMeta);
    color->r = static_cast<float>(value.r) / 255.0f;
    color->g = static_cast<float>(value.g) / 255.0f;
    color->b = static_cast<float>(value.b) / 255.0f;
}

void LocalScriptRuntime::PushUDim2(lua_State *state, LuaUDim2 value) {
    auto *udim = NewUserdata<LuaUDim2>(state, kUDim2Meta);
    *udim = value;
}

int LocalScriptRuntime::InstanceIndex(lua_State *state) {
    auto *instance = static_cast<LuaInstance *>(luaL_checkudata(state, 1, kInstanceMeta));
    const char *key = luaL_checkstring(state, 2);
    auto *runtime = instance ? instance->runtime : nullptr;
    if (!runtime || !runtime->world_) { lua_pushnil(state); return 1; }
    const std::string_view id(instance->id);
    const std::string_view property(key);
    if (property == "Name") {
        if (id == "PlayerGui" || id == "StarterGui") lua_pushlstring(state, id.data(), id.size());
        else if (auto name = runtime->world_->GuiName(id)) lua_pushlstring(state, name->data(), name->size());
        else lua_pushnil(state);
        return 1;
    }
    if (property == "Parent") {
        const auto parent = runtime->world_->GuiParent(id);
        if (parent && !parent->empty()) runtime->PushInstance(state, *parent);
        else lua_pushnil(state);
        return 1;
    }
    if (property == "Text") {
        const auto text = runtime->world_->GuiText(id);
        if (text) lua_pushlstring(state, text->data(), text->size()); else lua_pushnil(state);
        return 1;
    }
    if (property == "Visible") {
        const auto visible = runtime->world_->GuiVisible(id);
        if (visible) lua_pushboolean(state, *visible); else lua_pushnil(state);
        return 1;
    }
    if (property == "BackgroundTransparency" || property == "TextTransparency") {
        const auto value = runtime->world_->GuiTransparency(id, property);
        if (value) lua_pushnumber(state, *value); else lua_pushnil(state);
        return 1;
    }
    if (property == "BackgroundColor3" || property == "TextColor3") {
        const auto color = runtime->world_->GuiColor(id, property);
        if (color) runtime->PushColor(state, *color); else lua_pushnil(state);
        return 1;
    }
    if (property == "Position" || property == "Size") {
        Vector2 offset{}, scale{};
        if (runtime->world_->GetGuiLayout(id, property, offset, scale))
            runtime->PushUDim2(state, LuaUDim2{scale.x, offset.x, scale.y, offset.y});
        else lua_pushnil(state);
        return 1;
    }
    if (property == "Activated" || property == "MouseButton1Click") {
        auto *signal = NewUserdata<LuaSignal>(state, kSignalMeta);
        signal->runtime = runtime;
        CopyId(signal->id, id);
        CopyId(signal->event, property == "Activated" ? "Activated" : "MouseButton1Click");
        return 1;
    }
    if (property == "FindFirstChild") {
        lua_pushcfunction(state, FindFirstChild, "Instance.FindFirstChild");
        return 1;
    }
    if (const auto child = runtime->world_->FindGuiChild(id, property)) {
        runtime->PushInstance(state, *child);
        return 1;
    }
    lua_pushnil(state);
    return 1;
}

int LocalScriptRuntime::InstanceNewIndex(lua_State *state) {
    auto *instance = static_cast<LuaInstance *>(luaL_checkudata(state, 1, kInstanceMeta));
    const char *key = luaL_checkstring(state, 2);
    auto *runtime = instance ? instance->runtime : nullptr;
    if (!runtime || !runtime->world_) return 0;
    const std::string_view id(instance->id);
    const std::string_view property(key);
    if (property == "Parent") {
        if (lua_isnil(state, 3)) {
            if (!runtime->world_->SetGuiParent(id, ""))
                luaL_error(state, "Could not detach this GUI instance");
            return 0;
        }
        auto *parent = TestUserdata<LuaInstance>(state, 3, kInstanceMeta);
        if (!parent || parent->runtime != runtime ||
            !runtime->world_->SetGuiParent(id, parent->id))
            luaL_error(state, "Parent must be a valid PlayerGui or GUI instance");
        return 0;
    }
    if (property == "Name" || property == "Text") {
        std::size_t size = 0;
        const char *value = luaL_checklstring(state, 3, &size);
        const bool ok = property == "Name"
            ? runtime->world_->SetGuiName(id, std::string_view(value, size))
            : runtime->world_->SetGuiText(id, std::string_view(value, size));
        if (!ok) luaL_error(state, "Invalid UI string property value");
        return 0;
    }
    if (property == "Visible") {
        if (!lua_isboolean(state, 3) || !runtime->world_->SetGuiVisible(id, lua_toboolean(state, 3) != 0))
            luaL_error(state, "Visible expects a boolean");
        return 0;
    }
    if (property == "BackgroundTransparency" || property == "TextTransparency") {
        if (!lua_isnumber(state, 3) || !runtime->world_->SetGuiTransparency(id, property, static_cast<float>(lua_tonumber(state, 3))))
            luaL_error(state, "%s expects a finite number between 0 and 1", key);
        return 0;
    }
    if (property == "BackgroundColor3" || property == "TextColor3") {
        auto *color = TestUserdata<LuaColor3>(state, 3, kColorMeta);
        if (!color) luaL_error(state, "%s expects a Color3 value", key);
        const Color value{
            static_cast<unsigned char>(std::lround(std::clamp(color->r, 0.0f, 1.0f) * 255.0f)),
            static_cast<unsigned char>(std::lround(std::clamp(color->g, 0.0f, 1.0f) * 255.0f)),
            static_cast<unsigned char>(std::lround(std::clamp(color->b, 0.0f, 1.0f) * 255.0f)), 255};
        if (!runtime->world_->SetGuiColor(id, property, value)) luaL_error(state, "Invalid UI color property");
        return 0;
    }
    if (property == "Position" || property == "Size") {
        auto *udim = TestUserdata<LuaUDim2>(state, 3, kUDim2Meta);
        if (!udim) luaL_error(state, "%s expects UDim2", key);
        if (!runtime->world_->SetGuiLayout(id, property,
                Vector2{udim->xOffset, udim->yOffset}, Vector2{udim->xScale, udim->yScale}))
            luaL_error(state, "Invalid UI layout value");
        return 0;
    }
    luaL_error(state, "UI property '%s' is not available in the client runtime", key);
    return 0;
}

int LocalScriptRuntime::InstanceNew(lua_State *state) {
    auto *runtime = FromLua(state);
    const char *className = luaL_checkstring(state, 1);
    if (!runtime || !runtime->world_) { luaL_error(state, "UI runtime is unavailable"); return 0; }
    const std::string id = runtime->world_->CreateGui(className);
    if (id.empty()) { luaL_error(state, "Instance.new supports ScreenGui, Frame, TextLabel, and TextButton only"); return 0; }
    runtime->PushInstance(state, id);
    return 1;
}

int LocalScriptRuntime::FindFirstChild(lua_State *state) {
    auto *instance = static_cast<LuaInstance *>(luaL_checkudata(state, 1, kInstanceMeta));
    const char *name = luaL_checkstring(state, 2);
    if (!instance || !instance->runtime || !instance->runtime->world_) { lua_pushnil(state); return 1; }
    const auto child = instance->runtime->world_->FindGuiChild(instance->id, name);
    if (child) instance->runtime->PushInstance(state, *child); else lua_pushnil(state);
    return 1;
}

void LocalScriptRuntime::Connect(lua_State *caller, int functionIndex,
                                 std::string target, std::string event) {
    if (!lua_isfunction(caller, functionIndex) || target.size() >= 128) return;
    functionIndex = lua_absindex(caller, functionIndex);
    lua_pushvalue(caller, functionIndex);
    if (caller != state_) lua_xmove(caller, state_, 1);
    const int ref = lua_ref(state_, -1);
    lua_pop(state_, 1);
    callbacks_.push_back(Callback{std::move(target), std::move(event), ref});
}

int LocalScriptRuntime::SignalConnect(lua_State *state) {
    auto *signal = static_cast<LuaSignal *>(luaL_checkudata(state, 1, kSignalMeta));
    if (!signal || !signal->runtime || !lua_isfunction(state, 2))
        { luaL_error(state, "Connect expects a callback function"); return 0; }
    signal->runtime->Connect(state, 2, signal->id, signal->event);
    lua_pushboolean(state, 1);
    return 1;
}

int LocalScriptRuntime::GetService(lua_State *state) {
    auto *runtime = FromLua(state);
    const char *serviceName = luaL_checkstring(state, 2);
    if (!runtime) { lua_pushnil(state); return 1; }
    if (std::strcmp(serviceName, "Players") == 0) {
        lua_getglobal(state, "Players");
        return 1;
    }
    if (std::strcmp(serviceName, "StarterGui") == 0) {
        runtime->PushInstance(state, "StarterGui");
        return 1;
    }
    luaL_error(state, "Client service '%s' is not implemented", serviceName);
    return 0;
}

int LocalScriptRuntime::ColorFromRGB(lua_State *state) {
    float r = static_cast<float>(luaL_checknumber(state, 1));
    float g = static_cast<float>(luaL_checknumber(state, 2));
    float b = static_cast<float>(luaL_checknumber(state, 3));
    if (r > 1.0f || g > 1.0f || b > 1.0f) { r /= 255.0f; g /= 255.0f; b /= 255.0f; }
    auto *color = NewUserdata<LuaColor3>(state, kColorMeta);
    color->r = std::clamp(r, 0.0f, 1.0f);
    color->g = std::clamp(g, 0.0f, 1.0f);
    color->b = std::clamp(b, 0.0f, 1.0f);
    return 1;
}

int LocalScriptRuntime::UDim2New(lua_State *state) {
    const LuaUDim2 value{static_cast<float>(luaL_checknumber(state, 1)),
                         static_cast<float>(luaL_checknumber(state, 2)),
                         static_cast<float>(luaL_checknumber(state, 3)),
                         static_cast<float>(luaL_checknumber(state, 4))};
    auto *udim = NewUserdata<LuaUDim2>(state, kUDim2Meta); *udim = value; return 1;
}
int LocalScriptRuntime::UDim2FromScale(lua_State *state) {
    auto *udim = NewUserdata<LuaUDim2>(state, kUDim2Meta);
    udim->xScale = static_cast<float>(luaL_checknumber(state, 1));
    udim->yScale = static_cast<float>(luaL_checknumber(state, 2));
    return 1;
}
int LocalScriptRuntime::UDim2FromOffset(lua_State *state) {
    auto *udim = NewUserdata<LuaUDim2>(state, kUDim2Meta);
    udim->xOffset = static_cast<float>(luaL_checknumber(state, 1));
    udim->yOffset = static_cast<float>(luaL_checknumber(state, 2));
    return 1;
}

int LocalScriptRuntime::TaskWait(lua_State *state) {
    double seconds = lua_gettop(state) > 0 ? luaL_checknumber(state, 1) : 0.0;
    if (!std::isfinite(seconds)) { luaL_error(state, "task.wait expects a finite number"); return 0; }
    lua_pushnumber(state, std::clamp(seconds, 0.0, 3600.0));
    return lua_yield(state, 1);
}

void LocalScriptRuntime::QueueFunction(int functionRef, double wakeTime) {
    if (!state_ || functionRef < 0) return;
    lua_State *thread = lua_newthread(state_);
    lua_callbacks(thread)->userdata = this;
    lua_callbacks(thread)->interrupt = &LocalScriptRuntime::Interrupt;
    const int threadRef = lua_ref(state_, -1);
    lua_pop(state_, 1);
    luaL_sandboxthread(thread);
    lua_getref(state_, functionRef);
    lua_xmove(state_, thread, 1);
    jobs_.push(Job{std::max(now_, wakeTime), now_, nextJobOrder_++, threadRef, false});
}

int LocalScriptRuntime::TaskSpawn(lua_State *state) {
    auto *runtime = FromLua(state);
    if (!runtime || !lua_isfunction(state, 1)) { luaL_error(state, "task.spawn expects a function"); return 0; }
    const int functionRef = lua_ref(state, 1);
    runtime->QueueFunction(functionRef, runtime->now_);
    lua_unref(state, functionRef);
    lua_pushboolean(state, 1);
    return 1;
}
int LocalScriptRuntime::TaskDelay(lua_State *state) {
    auto *runtime = FromLua(state);
    if (!runtime || !lua_isfunction(state, 2)) { luaL_error(state, "task.delay expects seconds and a function"); return 0; }
    const double seconds = luaL_checknumber(state, 1);
    if (!std::isfinite(seconds)) { luaL_error(state, "task.delay expects a finite delay"); return 0; }
    const int functionRef = lua_ref(state, 2);
    runtime->QueueFunction(functionRef, runtime->now_ + std::clamp(seconds, 0.0, 3600.0));
    lua_unref(state, functionRef);
    lua_pushboolean(state, 1);
    return 1;
}
int LocalScriptRuntime::TaskDefer(lua_State *state) {
    auto *runtime = FromLua(state);
    if (!runtime || !lua_isfunction(state, 1)) { luaL_error(state, "task.defer expects a function"); return 0; }
    const int functionRef = lua_ref(state, 1);
    runtime->QueueFunction(functionRef, runtime->now_ + 1.0 / 60.0);
    lua_unref(state, functionRef);
    lua_pushboolean(state, 1);
    return 1;
}

int LocalScriptRuntime::Print(lua_State *state) {
    auto *runtime = FromLua(state);
    std::string message;
    const int count = std::min(lua_gettop(state), 16);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) message += "\t";
        std::size_t size = 0;
        const char *value = luaL_tolstring(state, i, &size);
        if (value && message.size() < 4096) message.append(value, std::min(size, 4096 - message.size()));
        lua_pop(state, 1);
    }
    if (runtime) TraceLog(LOG_INFO, "[LocalScript %s] %s", runtime->activeScript_.c_str(), message.c_str());
    return 0;
}

void LocalScriptRuntime::Resume(int threadRef, bool resumeWithDelta, double elapsed) {
    if (!state_) return;
    lua_getref(state_, threadRef);
    lua_State *thread = lua_tothread(state_, -1);
    lua_pop(state_, 1);
    if (!thread) return;
    deadline_ = std::min(frameDeadline_,
                         std::chrono::steady_clock::now() + std::chrono::milliseconds(4));
    if (resumeWithDelta) lua_pushnumber(thread, std::max(0.0, elapsed));
    const int status = lua_resume(thread, state_, resumeWithDelta ? 1 : 0);
    deadline_ = std::chrono::steady_clock::time_point::max();
    if (status == LUA_YIELD) {
        double wait = 0.0;
        if (lua_gettop(thread) > 0 && lua_isnumber(thread, -1)) wait = lua_tonumber(thread, -1);
        lua_settop(thread, 0);
        jobs_.push(Job{now_ + std::clamp(wait, 0.0, 3600.0), now_, nextJobOrder_++, threadRef, true});
        return;
    }
    if (status != LUA_OK) {
        const char *message = lua_tostring(thread, -1);
        LogError(activeScript_.empty() ? "runtime" : activeScript_,
                 message ? message : "Luau callback failed");
    }
    lua_settop(thread, 0);
    const bool persistent = std::find(scriptThreadRefs_.begin(), scriptThreadRefs_.end(), threadRef) != scriptThreadRefs_.end();
    if (!persistent) lua_unref(state_, threadRef);
}

void LocalScriptRuntime::FireActivated(std::string_view guiId) {
    if (!state_ || guiId.empty()) return;
    for (const Callback &callback : callbacks_) {
        if (callback.target == guiId && (callback.event == "Activated" || callback.event == "MouseButton1Click"))
            QueueFunction(callback.functionRef, now_);
    }
}

void LocalScriptRuntime::Tick(double deltaSeconds) {
    if (!state_) return;
    now_ += std::clamp(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0, 0.0, 0.25);
    frameDeadline_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(8);
    std::size_t count = 0;
    while (!jobs_.empty() && jobs_.top().wake <= now_ && count++ < kMaxJobsPerFrame &&
           std::chrono::steady_clock::now() < frameDeadline_) {
        const Job job = jobs_.top();
        jobs_.pop();
        activeScript_ = "client event";
        Resume(job.threadRef, job.resumeWithDelta,
               job.resumeWithDelta ? now_ - job.queuedAt : 0.0);
        activeScript_.clear();
    }
}

void LocalScriptRuntime::LogError(std::string_view script, const char *message) const {
    TraceLog(LOG_ERROR, "[LocalScript:%.*s] %s", static_cast<int>(script.size()), script.data(),
             message ? message : "unknown Luau error");
}

void LocalScriptRuntime::Stop() {
    if (!state_) { world_ = nullptr; return; }
    std::vector<int> ownedScriptRefs = scriptThreadRefs_;
    while (!jobs_.empty()) {
        const int ref = jobs_.top().threadRef;
        jobs_.pop();
        if (std::find(ownedScriptRefs.begin(), ownedScriptRefs.end(), ref) == ownedScriptRefs.end())
            lua_unref(state_, ref);
    }
    for (const Callback &callback : callbacks_) if (callback.functionRef >= 0) lua_unref(state_, callback.functionRef);
    callbacks_.clear();
    for (int ref : ownedScriptRefs) lua_unref(state_, ref);
    scriptThreadRefs_.clear();
    lua_close(state_);
    state_ = nullptr;
    world_ = nullptr;
    activeScript_.clear();
    now_ = 0.0;
    memory_ = MemoryState{};
    frameDeadline_ = std::chrono::steady_clock::time_point::max();
}
