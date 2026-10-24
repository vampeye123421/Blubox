#pragma once
#include "EngineCore.hpp"
struct lua_State;
namespace blubox::engine {
class TaskScheduler;
}
namespace blubox::engine {
class LuaInstanceBinding {
public:
  static void install(lua_State *state, InstanceRegistry &registry);
  static void setScheduler(lua_State *state, TaskScheduler &scheduler);
  static void push(lua_State *state, InstanceHandle handle);
  static bool read(lua_State *state, int index, InstanceHandle &out);
  static void fireTouched(lua_State *state, InstanceHandle part, InstanceHandle otherPart);
};
} // namespace blubox::engine
