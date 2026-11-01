#include "LuaInstanceBinding.hpp"

#include "TaskScheduler.hpp"
#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>

namespace blubox::engine {
namespace {
constexpr int kInstanceTag = 73;
constexpr int kSignalTag = 74;
constexpr int kConnectionTag = 75;
constexpr int kBindingStateTag = 76;
constexpr int kVector3Tag = 77;
constexpr int kColor3Tag = 78;
constexpr int kUDimTag = 79;
constexpr int kUDim2Tag = 80;
constexpr int kTweenTag = 81;
constexpr const char *kBindingStateKey = "__blubox_lua_binding_state";
constexpr const char *kMethodsKey = "__blubox_instance_methods";

struct LuaSignal {
  InstanceHandle handle;
  std::uint8_t event = 0;
  char property[129]{};
};
struct LuaConnection {
  std::uint64_t id = 0;
};
struct LuaVector3 {
  float x = 0, y = 0, z = 0;
};
struct LuaColor3 {
  float r = 0, g = 0, b = 0;
};
struct LuaUDim { float scale = 0, offset = 0; };
struct LuaUDim2 { LuaUDim x{}, y{}; };
struct LuaTween { std::uint64_t id = 0; };
struct PendingDebris { InstanceHandle handle{}; double destroyAt = 0; };
struct TweenValue {
  enum class Kind : std::uint8_t { Number, Vector3, Color3, UDim2 } kind = Kind::Number;
  std::array<float, 4> components{};
};
struct TweenProperty {
  std::string name;
  TweenValue initial{}, target{};
};
struct TweenRecord {
  std::uint64_t id = 0;
  InstanceHandle target{};
  std::vector<TweenProperty> properties;
  double duration = 0, delay = 0, startedAt = 0, pausedAt = 0;
  std::uint32_t repeatCount = 0;
  bool reverses = false, playing = false, paused = false, destroyed = false, completed = false;
};
struct ConnectionRecord {
  std::uint64_t id = 0;
  InstanceHandle owner{};
  std::uint8_t event = 0;
  int callback = LUA_NOREF;
  bool connected = true;
  std::string property;
};
struct BindingState {
  InstanceRegistry *instances = nullptr;
  TaskScheduler *scheduler = nullptr;
  std::uint64_t nextConnection = 1;
  std::unordered_map<std::uint64_t, ConnectionRecord> connections;
  std::unordered_map<std::string, std::vector<InstanceHandle>> tags;
  std::vector<PendingDebris> debris;
  std::vector<TweenRecord> tweens;
  std::uint64_t nextTween = 1;
};

BindingState *binding(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kBindingStateKey);
  auto *state = static_cast<BindingState *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return state;
}
InstanceRegistry *registry(lua_State *L) {
  auto *b = binding(L);
  return b ? b->instances : nullptr;
}
InstanceHandle checkedInstance(lua_State *L, int index) {
  auto *handle = static_cast<InstanceHandle *>(lua_touserdatatagged(L, index, kInstanceTag));
  auto *r = registry(L);
  if (!handle || !r || !r->alive(*handle))
    luaL_error(L, "Instance reference is stale or invalid");
  return *handle;
}

const char *className(ClassId id) {
  switch (id) {
  case ClassId::DataModel:
    return "DataModel";
  case ClassId::Folder:
    return "Folder";
  case ClassId::Model:
    return "Model";
  case ClassId::Part:
    return "Part";
  case ClassId::Sphere:
    return "Sphere";
  case ClassId::WedgePart:
    return "WedgePart";
  case ClassId::CornerWedgePart:
    return "CornerWedgePart";
  case ClassId::MeshPart:
    return "MeshPart";
  case ClassId::Shirt:
    return "Shirt";
  case ClassId::Pants:
    return "Pants";
  case ClassId::Accessory:
    return "Accessory";
  case ClassId::SpawnLocation:
    return "SpawnLocation";
  case ClassId::ScreenGui:
    return "ScreenGui";
  case ClassId::Frame:
    return "Frame";
  case ClassId::TextLabel:
    return "TextLabel";
  case ClassId::TextButton:
    return "TextButton";
  case ClassId::TextBox:
    return "TextBox";
  case ClassId::ImageLabel:
    return "ImageLabel";
  case ClassId::ImageButton:
    return "ImageButton";
  case ClassId::ScrollingFrame:
    return "ScrollingFrame";
  case ClassId::RemoteEvent:
    return "RemoteEvent";
  case ClassId::RemoteFunction:
    return "RemoteFunction";
  case ClassId::Script:
    return "Script";
  case ClassId::LocalScript:
    return "LocalScript";
  case ClassId::Service:
    return "Service";
  case ClassId::Player:
    return "Player";
  case ClassId::PlayerGui:
    return "PlayerGui";
  default:
    return "Instance";
  }
}
bool classId(std::string_view name, ClassId &out) {
  static constexpr std::pair<std::string_view, ClassId> entries[] = {{"Folder", ClassId::Folder},
                                                                     {"Model", ClassId::Model},
                                                                     {"Part", ClassId::Part},
                                                                     {"Sphere", ClassId::Sphere},
                                                                     {"WedgePart", ClassId::WedgePart},
                                                                     {"CornerWedgePart", ClassId::CornerWedgePart},
                                                                     {"MeshPart", ClassId::MeshPart},
                                                                     {"Shirt", ClassId::Shirt},
                                                                     {"Pants", ClassId::Pants},
                                                                     {"Accessory", ClassId::Accessory},
                                                                     {"SpawnLocation", ClassId::SpawnLocation},
                                                                     {"ScreenGui", ClassId::ScreenGui},
                                                                     {"Frame", ClassId::Frame},
                                                                     {"TextLabel", ClassId::TextLabel},
                                                                     {"TextButton", ClassId::TextButton},
                                                                     {"TextBox", ClassId::TextBox},
                                                                     {"ImageLabel", ClassId::ImageLabel},
                                                                     {"ImageButton", ClassId::ImageButton},
                                                                     {"ScrollingFrame", ClassId::ScrollingFrame},
                                                                     {"RemoteEvent", ClassId::RemoteEvent},
                                                                     {"RemoteFunction", ClassId::RemoteFunction},
                                                                     {"Script", ClassId::Script},
                                                                     {"LocalScript", ClassId::LocalScript}};
  for (const auto &[label, id] : entries)
    if (label == name) {
      out = id;
      return true;
    }
  return false;
}
void pushVector(lua_State *L, Vec3 v) {
  auto *value = static_cast<LuaVector3 *>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaVector3), kVector3Tag));
  new (value) LuaVector3{v.x, v.y, v.z};
}
bool readVector(lua_State *L, int index, Vec3 &v) {
  if (auto *value = static_cast<LuaVector3 *>(lua_touserdatatagged(L, index, kVector3Tag))) {
    v = {value->x, value->y, value->z};
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  }
  if (!lua_istable(L, index))
    return false;
  const int abs = lua_absindex(L, index);
  const char *names[][2] = {{"X", "x"}, {"Y", "y"}, {"Z", "z"}};
  float *axis[] = {&v.x, &v.y, &v.z};
  for (int i = 0; i < 3; ++i) {
    lua_getfield(L, abs, names[i][0]);
    if (!lua_isnumber(L, -1)) {
      lua_pop(L, 1);
      lua_getfield(L, abs, names[i][1]);
    }
    if (!lua_isnumber(L, -1)) {
      lua_pop(L, 1);
      return false;
    }
    *axis[i] = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    if (!std::isfinite(*axis[i]))
      return false;
  }
  return true;
}
bool readVector2(lua_State* L, int index, float& x, float& y) {
  const int abs = lua_absindex(L, index);
  if (!lua_istable(L, abs)) return false;
  lua_getfield(L, abs, "X");
  if (!lua_isnumber(L, -1)) {
    lua_pop(L, 1);
    lua_getfield(L, abs, "x");
  }
  if (!lua_isnumber(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  x = static_cast<float>(lua_tonumber(L, -1));
  lua_pop(L, 1);
  lua_getfield(L, abs, "Y");
  if (!lua_isnumber(L, -1)) {
    lua_pop(L, 1);
    lua_getfield(L, abs, "y");
  }
  if (!lua_isnumber(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  y = static_cast<float>(lua_tonumber(L, -1));
  lua_pop(L, 1);
  return std::isfinite(x) && std::isfinite(y);
}
void pushColor(lua_State *L, const std::array<std::uint8_t, 4> &color) {
  auto *value = static_cast<LuaColor3 *>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaColor3), kColor3Tag));
  new (value) LuaColor3{color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f};
}
bool readColor(lua_State *L, int index, std::array<std::uint8_t, 4> &color) {
  if (auto *value = static_cast<LuaColor3 *>(lua_touserdatatagged(L, index, kColor3Tag))) {
    const float channels[] = {value->r, value->g, value->b};
    for (int i = 0; i < 3; ++i) {
      if (!std::isfinite(channels[i]))
        return false;
      color[i] = static_cast<std::uint8_t>(std::clamp(std::lround(channels[i] * 255.0f), 0l, 255l));
    }
    color[3] = 255;
    return true;
  }
  if (!lua_istable(L, index))
    return false;
  const int abs = lua_absindex(L, index);
  constexpr const char *names[] = {"R", "G", "B"};
  for (int i = 0; i < 3; ++i) {
    lua_getfield(L, abs, names[i]);
    if (!lua_isnumber(L, -1)) {
      lua_pop(L, 1);
      return false;
    }
    double n = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (!std::isfinite(n))
      return false;
    color[i] = static_cast<std::uint8_t>(std::clamp(std::lround(n * 255.0), 0l, 255l));
  }
  color[3] = 255;
  return true;
}
void pushCFrame(lua_State *L, Vec3 position, Vec3 rotation) {
  lua_createtable(L, 0, 5);
  pushVector(L, position);
  lua_setfield(L, -2, "Position");
  pushVector(L, rotation);
  lua_setfield(L, -2, "Rotation");
  lua_pushnumber(L, position.x);
  lua_setfield(L, -2, "X");
  lua_pushnumber(L, position.y);
  lua_setfield(L, -2, "Y");
  lua_pushnumber(L, position.z);
  lua_setfield(L, -2, "Z");
  lua_pushliteral(L, "CFrame");
  lua_setfield(L, -2, "__bluboxType");
}
bool isGuiObject(ClassId id) {
  return id == ClassId::Frame || id == ClassId::TextLabel || id == ClassId::TextButton ||
         id == ClassId::TextBox || id == ClassId::ImageLabel || id == ClassId::ImageButton ||
         id == ClassId::ScrollingFrame;
}
void pushUDim2(lua_State *L, UDim2 value) {
  auto* result = static_cast<LuaUDim2*>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaUDim2), kUDim2Tag));
  new (result) LuaUDim2{{value.x.scale, value.x.offset}, {value.y.scale, value.y.offset}};
}
bool readUDim2(lua_State* L, int index, UDim2& value) {
  auto* input = static_cast<LuaUDim2*>(lua_touserdatatagged(L, index, kUDim2Tag));
  if (!input) return false;
  value = {{input->x.scale, input->x.offset}, {input->y.scale, input->y.offset}};
  return std::isfinite(value.x.scale) && std::isfinite(value.x.offset) &&
         std::isfinite(value.y.scale) && std::isfinite(value.y.offset);
}
int udimReadOnly(lua_State* L) {
  luaL_error(L, "UDim values are immutable");
  return 0;
}
int udim2ReadOnly(lua_State* L) {
  luaL_error(L, "UDim2 values are immutable");
  return 0;
}
int udimNew(lua_State* L) {
  auto* result = static_cast<LuaUDim*>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaUDim), kUDimTag));
  const double scale = luaL_optnumber(L, 1, 0), offset = luaL_optnumber(L, 2, 0);
  if (!std::isfinite(scale) || !std::isfinite(offset)) {
    luaL_error(L, "UDim components must be finite");
    return 0;
  }
  new (result) LuaUDim{static_cast<float>(scale), static_cast<float>(offset)};
  return 1;
}
int udim2New(lua_State* L) {
  const double xs = luaL_optnumber(L, 1, 0), xo = luaL_optnumber(L, 2, 0);
  const double ys = luaL_optnumber(L, 3, 0), yo = luaL_optnumber(L, 4, 0);
  if (!std::isfinite(xs) || !std::isfinite(xo) || !std::isfinite(ys) || !std::isfinite(yo)) {
    luaL_error(L, "UDim2 components must be finite");
    return 0;
  }
  pushUDim2(L, {{static_cast<float>(xs), static_cast<float>(xo)},
                {static_cast<float>(ys), static_cast<float>(yo)}});
  return 1;
}
void pushHandle(lua_State *L, InstanceHandle h) {
  auto *p = static_cast<InstanceHandle *>(lua_newuserdatataggedwithmetatable(L, sizeof(InstanceHandle), kInstanceTag));
  new (p) InstanceHandle(h);
}
void pushSignal(lua_State *L, InstanceHandle h, std::uint8_t event, std::string_view property = {}) {
  auto *p = static_cast<LuaSignal *>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaSignal), kSignalTag));
  new (p) LuaSignal{};
  p->handle = h;
  p->event = event;
  const auto count = std::min(property.size(), sizeof(p->property) - 1);
  std::memcpy(p->property, property.data(), count);
}

int vectorIndex(lua_State *L) {
  auto *v = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 1, kVector3Tag));
  const char *key = luaL_checkstring(L, 2);
  if (!v) {
    lua_pushnil(L);
    return 1;
  }
  if (key[1] == '\0') {
    switch (key[0]) {
    case 'X':
    case 'x':
      lua_pushnumber(L, v->x);
      return 1;
    case 'Y':
    case 'y':
      lua_pushnumber(L, v->y);
      return 1;
    case 'Z':
    case 'z':
      lua_pushnumber(L, v->z);
      return 1;
    }
  }
  if (std::strcmp(key, "Magnitude") == 0) {
    lua_pushnumber(L, std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z));
    return 1;
  }
  if (std::strcmp(key, "Unit") == 0) {
    float n = std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
    pushVector(L, n > 0 ? Vec3{v->x / n, v->y / n, v->z / n} : Vec3{});
    return 1;
  }
  lua_pushnil(L);
  return 1;
}
int vectorReadOnly(lua_State *L) {
  luaL_error(L, "Vector3 values are immutable");
  return 0;
}
int vectorAdd(lua_State *L) {
  auto *a = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 1, kVector3Tag));
  auto *b = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 2, kVector3Tag));
  if (!a || !b) {
    luaL_error(L, "Vector3 addition expects two Vector3 values");
    return 0;
  }
  pushVector(L, {a->x + b->x, a->y + b->y, a->z + b->z});
  return 1;
}
int vectorSub(lua_State *L) {
  auto *a = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 1, kVector3Tag));
  auto *b = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 2, kVector3Tag));
  if (!a || !b) {
    luaL_error(L, "Vector3 subtraction expects two Vector3 values");
    return 0;
  }
  pushVector(L, {a->x - b->x, a->y - b->y, a->z - b->z});
  return 1;
}
int vectorMul(lua_State *L) {
  auto *a = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 1, kVector3Tag));
  auto *b = static_cast<LuaVector3 *>(lua_touserdatatagged(L, 2, kVector3Tag));
  if (a && b) {
    pushVector(L, {a->x * b->x, a->y * b->y, a->z * b->z});
    return 1;
  }
  if (a && lua_isnumber(L, 2)) {
    float n = static_cast<float>(lua_tonumber(L, 2));
    pushVector(L, {a->x * n, a->y * n, a->z * n});
    return 1;
  }
  if (b && lua_isnumber(L, 1)) {
    float n = static_cast<float>(lua_tonumber(L, 1));
    pushVector(L, {b->x * n, b->y * n, b->z * n});
    return 1;
  }
  luaL_error(L, "Vector3 multiplication expects a scalar or Vector3");
  return 0;
}
int colorIndex(lua_State *L) {
  auto *c = static_cast<LuaColor3 *>(lua_touserdatatagged(L, 1, kColor3Tag));
  const char *key = luaL_checkstring(L, 2);
  if (!c) {
    lua_pushnil(L);
    return 1;
  }
  if (std::strcmp(key, "R") == 0 || std::strcmp(key, "r") == 0)
    lua_pushnumber(L, c->r);
  else if (std::strcmp(key, "G") == 0 || std::strcmp(key, "g") == 0)
    lua_pushnumber(L, c->g);
  else if (std::strcmp(key, "B") == 0 || std::strcmp(key, "b") == 0)
    lua_pushnumber(L, c->b);
  else
    lua_pushnil(L);
  return 1;
}
int colorReadOnly(lua_State *L) {
  luaL_error(L, "Color3 values are immutable");
  return 0;
}
int vectorNew(lua_State *L) {
  pushVector(L, {static_cast<float>(luaL_optnumber(L, 1, 0)), static_cast<float>(luaL_optnumber(L, 2, 0)),
                 static_cast<float>(luaL_optnumber(L, 3, 0))});
  return 1;
}
int vector2New(lua_State *L) {
  lua_createtable(L, 0, 5);
  double x = luaL_optnumber(L, 1, 0), y = luaL_optnumber(L, 2, 0);
  lua_pushnumber(L, x);
  lua_setfield(L, -2, "X");
  lua_pushnumber(L, x);
  lua_setfield(L, -2, "x");
  lua_pushnumber(L, y);
  lua_setfield(L, -2, "Y");
  lua_pushnumber(L, y);
  lua_setfield(L, -2, "y");
  lua_pushliteral(L, "Vector2");
  lua_setfield(L, -2, "__bluboxType");
  return 1;
}
int udim2FromScale(lua_State* L) {
  const double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2);
  if (!std::isfinite(x) || !std::isfinite(y)) {
    luaL_error(L, "UDim2 components must be finite");
    return 0;
  }
  pushUDim2(L, {{static_cast<float>(x), 0}, {static_cast<float>(y), 0}});
  return 1;
}
int udim2FromOffset(lua_State* L) {
  const double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2);
  if (!std::isfinite(x) || !std::isfinite(y)) {
    luaL_error(L, "UDim2 components must be finite");
    return 0;
  }
  pushUDim2(L, {{0, static_cast<float>(x)}, {0, static_cast<float>(y)}});
  return 1;
}
int udimIndex(lua_State* L) {
  auto* value = static_cast<LuaUDim*>(lua_touserdatatagged(L, 1, kUDimTag));
  const char* key = luaL_checkstring(L, 2);
  if (!value) {
    lua_pushnil(L);
    return 1;
  }
  if (std::strcmp(key, "Scale") == 0) lua_pushnumber(L, value->scale);
  else if (std::strcmp(key, "Offset") == 0) lua_pushnumber(L, value->offset);
  else lua_pushnil(L);
  return 1;
}
int udim2Index(lua_State* L) {
  auto* value = static_cast<LuaUDim2*>(lua_touserdatatagged(L, 1, kUDim2Tag));
  const char* key = luaL_checkstring(L, 2);
  if (!value) {
    lua_pushnil(L);
    return 1;
  }
  if (std::strcmp(key, "X") == 0) {
    auto* axis = static_cast<LuaUDim*>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaUDim), kUDimTag));
    new (axis) LuaUDim(value->x);
  } else if (std::strcmp(key, "Y") == 0) {
    auto* axis = static_cast<LuaUDim*>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaUDim), kUDimTag));
    new (axis) LuaUDim(value->y);
  } else {
    lua_pushnil(L);
  }
  return 1;
}
int colorNew(lua_State *L) {
  auto conv = [&](int i) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(luaL_optnumber(L, i, 0) * 255.0), 0l, 255l));
  };
  pushColor(L, {conv(1), conv(2), conv(3), 255});
  return 1;
}
int colorFromRGBNative(lua_State *L) {
  auto conv = [&](int i) {
    return static_cast<std::uint8_t>(std::clamp<lua_Integer>(luaL_checkinteger(L, i), 0, 255));
  };
  pushColor(L, {conv(1), conv(2), conv(3), 255});
  return 1;
}
int brickColorNew(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  static constexpr std::pair<std::string_view, std::array<std::uint8_t, 4>> colors[] = {
      {"Really red", {255, 0, 0, 255}},     {"Bright red", {196, 40, 28, 255}},
      {"Bright blue", {13, 105, 172, 255}}, {"Bright green", {75, 151, 75, 255}},
      {"New Yeller", {255, 255, 0, 255}},   {"White", {242, 243, 243, 255}},
      {"Black", {27, 42, 53, 255}},         {"Medium stone grey", {163, 162, 165, 255}}};
  for (const auto &[label, color] : colors)
    if (label == name) {
      pushColor(L, color);
      return 1;
    }
  pushColor(L, {163, 162, 165, 255});
  return 1;
}
int luaTime(lua_State *L) {
  auto *b = binding(L);
  if (b && b->scheduler)
    lua_pushnumber(L, b->scheduler->now());
  else {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    lua_pushnumber(L, std::chrono::duration<double>(now).count());
  }
  return 1;
}
void addEnumGroup(lua_State *L, int root, const char *name, std::initializer_list<const char *> values) {
  root = lua_absindex(L, root);
  lua_newtable(L);
  for (const char *value : values) {
    lua_createtable(L, 0, 4);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "EnumType");
    lua_pushstring(L, value);
    lua_setfield(L, -2, "Name");
    lua_pushstring(L, value);
    lua_setfield(L, -2, "Value");
    lua_pushliteral(L, "EnumItem");
    lua_setfield(L, -2, "__bluboxType");
    lua_setfield(L, -2, value);
  }
  lua_setfield(L, root, name);
}
int cframeNew(lua_State *L) {
  Vec3 p{};
  if (lua_istable(L, 1) || lua_type(L, 1) == LUA_TUSERDATA) {
    if (!readVector(L, 1, p)) {
      luaL_error(L, "CFrame.new(position) expects Vector3");
      return 0;
    }
  } else if (lua_gettop(L) >= 3) {
    p = {static_cast<float>(luaL_checknumber(L, 1)), static_cast<float>(luaL_checknumber(L, 2)),
         static_cast<float>(luaL_checknumber(L, 3))};
  }
  pushCFrame(L, p, {});
  return 1;
}
int luaTypeof(lua_State *L) {
  if (lua_type(L, 1) == LUA_TUSERDATA) {
    if (lua_touserdatatagged(L, 1, kInstanceTag)) {
      lua_pushliteral(L, "Instance");
      return 1;
    }
    if (lua_touserdatatagged(L, 1, kSignalTag)) {
      lua_pushliteral(L, "RBXScriptSignal");
      return 1;
    }
    if (lua_touserdatatagged(L, 1, kConnectionTag)) {
      lua_pushliteral(L, "RBXScriptConnection");
      return 1;
    }
    if (lua_touserdatatagged(L, 1, kVector3Tag)) {
      lua_pushliteral(L, "Vector3");
      return 1;
    }
    if (lua_touserdatatagged(L, 1, kColor3Tag)) {
      lua_pushliteral(L, "Color3");
      return 1;
    }
    if (lua_touserdatatagged(L, 1, kUDimTag)) {
      lua_pushliteral(L, "UDim");
      return 1;
    }
    if (lua_touserdatatagged(L, 1, kUDim2Tag)) {
      lua_pushliteral(L, "UDim2");
      return 1;
    }
  }
  if (lua_istable(L, 1)) {
    lua_getfield(L, 1, "__bluboxType");
    if (lua_isstring(L, -1))
      return 1;
    lua_pop(L, 1);
    lua_getfield(L, 1, "__bluboxColor3");
    if (lua_toboolean(L, -1)) {
      lua_pop(L, 1);
      lua_pushliteral(L, "Color3");
      return 1;
    }
    lua_pop(L, 1);
  }
  lua_pushstring(L, lua_typename(L, lua_type(L, 1)));
  return 1;
}
void fire(lua_State *L, InstanceHandle h, std::uint8_t event, const char *property,
          InstanceHandle other);
int instanceNew(lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  ClassId id{};
  if (!classId(name, id)) {
    luaL_error(L, "Invalid instance class: %s", name);
    return 0;
  }
  auto *r = registry(L);
  auto h = r->create(name, id);
  if (!h) {
    luaL_error(L, "Could not create instance '%s'", name);
    return 0;
  }
  if (!lua_isnoneornil(L, 2)) {
    auto *parent = static_cast<InstanceHandle *>(lua_touserdatatagged(L, 2, kInstanceTag));
    if (!parent || !r->setParent(h, *parent)) {
      r->destroy(h);
      luaL_error(L, "Instance.new parent must be a live Instance");
      return 0;
    }
    fire(L, *parent, 3, nullptr, h);
    fire(L, h, 5, nullptr, *parent);
  }
  pushHandle(L, h);
  return 1;
}
int dataModelIndex(lua_State *L) {
  const char *key = luaL_checkstring(L, 2);
  auto *r = registry(L);
  auto h = r ? r->service(key) : InstanceHandle{};
  if (h)
    pushHandle(L, h);
  else
    lua_pushnil(L);
  return 1;
}
int instanceFindFirstChild(lua_State *L) {
  auto h = checkedInstance(L, 1);
  if (h == registry(L)->root()) {
    lua_pushnil(L);
    return 1;
  }
  const char *name = luaL_checkstring(L, 2);
  for (auto child : registry(L)->children(h)) {
    auto *rec = registry(L)->get(child);
    if (rec && rec->name == name) {
      pushHandle(L, child);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}
int instanceWaitForChild(lua_State* L) {
  const auto parent = checkedInstance(L, 1);
  const char* name = luaL_checkstring(L, 2);
  for (const auto child : registry(L)->children(parent)) {
    const auto* record = registry(L)->get(child);
    if (record && record->name == name) {
      pushHandle(L, child);
      return 1;
    }
  }
  luaL_error(L, "WaitForChild cannot yield for '%s' because this runtime creates its core instances before scripts", name);
  return 0;
}
void disconnectOwner(lua_State *L, InstanceHandle h) {
  auto *b = binding(L);
  if (!b)
    return;
  for (auto &[_, c] : b->connections)
    if (c.owner == h && c.connected) {
      c.connected = false;
      if (c.callback != LUA_NOREF)
        lua_unref(L, c.callback);
      c.callback = LUA_NOREF;
    }
}
int instanceDestroy(lua_State *L) {
  auto h = checkedInstance(L, 1);
  auto *instances = registry(L);
  if (instances->protectedInstance(h)) {
    luaL_error(L, "The DataModel and service instances cannot be destroyed");
    return 0;
  }
  std::vector<InstanceHandle> pending{h};
  std::vector<InstanceHandle> subtree;
  while (!pending.empty()) {
    auto current = pending.back();
    pending.pop_back();
    for (auto child : registry(L)->children(current))
      pending.push_back(child);
    subtree.push_back(current);
  }
  for (auto current : subtree)
    fire(L, current, 6, nullptr, {});
  for (auto current : subtree) {
    auto* state = binding(L);
    if (state) {
      for (auto tag = state->tags.begin(); tag != state->tags.end();) {
        auto& tagged = tag->second;
        const auto position = std::find(tagged.begin(), tagged.end(), current);
        if (position != tagged.end()) {
          fire(L, instances->service("CollectionService"), 8, tag->first.c_str(), current);
          tagged.erase(position);
        }
        if (tagged.empty()) tag = state->tags.erase(tag);
        else ++tag;
      }
    }
    auto *record = registry(L)->get(current);
    if (record && record->parent != UINT32_MAX) {
      auto parent = registry(L)->handleForId(record->parent);
      fire(L, parent, 4, nullptr, current);
    }
    fire(L, current, 5, nullptr, {});
  }
  for (auto current : subtree) {
    disconnectOwner(L, current);
  }
  if (!instances->destroy(h)) {
    luaL_error(L, "Could not destroy the instance");
    return 0;
  }
  return 0;
}
int instanceClone(lua_State *L) {
  auto h = checkedInstance(L, 1);
  if (registry(L)->protectedInstance(h)) {
    luaL_error(L, "The DataModel and service instances cannot be cloned");
    return 0;
  }
  auto cloned = registry(L)->clone(h);
  if (!cloned) {
    luaL_error(L, "Could not clone instance");
    return 0;
  }
  pushHandle(L, cloned);
  return 1;
}
int instanceIsA(lua_State *L) {
  auto h = checkedInstance(L, 1);
  const char *wanted = luaL_checkstring(L, 2);
  auto *rec = registry(L)->get(h);
  const char *actual = rec->classId == ClassId::Service ? rec->name.c_str() : className(rec->classId);
  lua_pushboolean(L, std::strcmp(wanted, "Instance") == 0 ||
                         std::strcmp(actual, wanted) == 0 ||
                         (std::strcmp(wanted, "BasePart") == 0 && (rec->componentMask & HasRenderable)));
  return 1;
}
int instanceEqual(lua_State *L) {
  auto *a = static_cast<InstanceHandle *>(lua_touserdatatagged(L, 1, kInstanceTag));
  auto *b = static_cast<InstanceHandle *>(lua_touserdatatagged(L, 2, kInstanceTag));
  lua_pushboolean(L, a && b && *a == *b);
  return 1;
}
int connectionDisconnect(lua_State *L) {
  auto *c = static_cast<LuaConnection *>(lua_touserdatatagged(L, 1, kConnectionTag));
  auto *b = binding(L);
  if (!c || !b)
    return 0;
  auto i = b->connections.find(c->id);
  if (i != b->connections.end() && i->second.connected) {
    i->second.connected = false;
    lua_unref(L, i->second.callback);
    i->second.callback = LUA_NOREF;
  }
  return 0;
}
int connectionIndex(lua_State *L) {
  auto *c = static_cast<LuaConnection *>(lua_touserdatatagged(L, 1, kConnectionTag));
  auto *b = binding(L);
  const char *key = luaL_checkstring(L, 2);
  if (std::strcmp(key, "Connected") == 0) {
    bool connected = false;
    if (c && b) {
      auto i = b->connections.find(c->id);
      connected = i != b->connections.end() && i->second.connected;
    }
    lua_pushboolean(L, connected);
    return 1;
  }
  if (std::strcmp(key, "Disconnect") == 0) {
    lua_pushcfunction(L, connectionDisconnect, "RBXScriptConnection.Disconnect");
    return 1;
  }
  lua_pushnil(L);
  return 1;
}
int signalConnect(lua_State *L) {
  auto *s = static_cast<LuaSignal *>(lua_touserdatatagged(L, 1, kSignalTag));
  if (!s || !lua_isfunction(L, 2)) {
    luaL_error(L, "Signal:Connect expects a callback");
    return 0;
  }
  auto *b = binding(L);
  auto id = b->nextConnection++;
  lua_pushvalue(L, 2);
  int ref = lua_ref(L, -1);
  lua_pop(L, 1);
  ConnectionRecord record{};
  record.id = id;
  record.owner = s->handle;
  record.event = s->event;
  record.callback = ref;
  record.property = s->property;
  b->connections.emplace(id, std::move(record));
  auto *c = static_cast<LuaConnection *>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaConnection), kConnectionTag));
  new (c) LuaConnection{id};
  return 1;
}
int signalIndex(lua_State *L) {
  const char *key = luaL_checkstring(L, 2);
  if (std::strcmp(key, "Connect") == 0) {
    lua_pushcfunction(L, signalConnect, "RBXScriptSignal.Connect");
    return 1;
  }
  lua_pushnil(L);
  return 1;
}
int debrisAddItem(lua_State* L) {
  const auto serviceHandle = checkedInstance(L, 1);
  InstanceHandle item{};
  auto* state = binding(L);
  if (!state || !LuaInstanceBinding::read(L, 2, item)) {
    luaL_error(L, "Debris:AddItem expects a live Instance");
    return 0;
  }
  if (state->instances->protectedInstance(item))
    { luaL_error(L, "Debris cannot destroy the DataModel or a service"); return 0; }
  const double lifetime = luaL_checknumber(L, 3);
  if (!std::isfinite(lifetime) || lifetime < 0 || lifetime > 86400)
    { luaL_error(L, "Debris lifetime must be between 0 and 86400 seconds"); return 0; }
  if (serviceHandle != state->instances->service("Debris"))
    { luaL_error(L, "AddItem must be called on Debris"); return 0; }
  const double now = state->scheduler ? state->scheduler->now() : 0;
  state->debris.push_back({item, now + lifetime});
  return 0;
}
bool readTag(lua_State* L, int index, std::string& tag) {
  size_t size = 0;
  const char* text = luaL_checklstring(L, index, &size);
  if (!text || size == 0 || size > 128) return false;
  tag.assign(text, size);
  return true;
}
int collectionAddTag(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  InstanceHandle item{};
  std::string tag;
  if (!state || serviceHandle != state->instances->service("CollectionService") ||
      !LuaInstanceBinding::read(L, 2, item) || !readTag(L, 3, tag))
    { luaL_error(L, "CollectionService:AddTag expects a live Instance and a non-empty tag"); return 0; }
  auto& tagged = state->tags[tag];
  if (std::find(tagged.begin(), tagged.end(), item) == tagged.end()) {
    tagged.push_back(item);
    fire(L, serviceHandle, 7, tag.c_str(), item);
  }
  return 0;
}
int collectionRemoveTag(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  InstanceHandle item{};
  std::string tag;
  if (!state || serviceHandle != state->instances->service("CollectionService") ||
      !LuaInstanceBinding::read(L, 2, item) || !readTag(L, 3, tag))
    { luaL_error(L, "CollectionService:RemoveTag expects a live Instance and a non-empty tag"); return 0; }
  auto found = state->tags.find(tag);
  if (found != state->tags.end()) {
    auto& tagged = found->second;
    const auto position = std::find(tagged.begin(), tagged.end(), item);
    if (position != tagged.end()) {
      tagged.erase(position);
      fire(L, serviceHandle, 8, tag.c_str(), item);
    }
    if (tagged.empty()) state->tags.erase(found);
  }
  return 0;
}
int collectionHasTag(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  InstanceHandle item{};
  std::string tag;
  if (!state || serviceHandle != state->instances->service("CollectionService") ||
      !LuaInstanceBinding::read(L, 2, item) || !readTag(L, 3, tag))
    { luaL_error(L, "CollectionService:HasTag expects a live Instance and a non-empty tag"); return 0; }
  const auto found = state->tags.find(tag);
  lua_pushboolean(L, found != state->tags.end() &&
                         std::find(found->second.begin(), found->second.end(), item) != found->second.end());
  return 1;
}
int collectionGetTagged(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  std::string tag;
  if (!state || serviceHandle != state->instances->service("CollectionService") || !readTag(L, 2, tag))
    { luaL_error(L, "CollectionService:GetTagged expects a non-empty tag"); return 0; }
  const auto found = state->tags.find(tag);
  lua_newtable(L);
  if (found == state->tags.end()) return 1;
  int index = 1;
  for (const auto handle : found->second) {
    if (!state->instances->alive(handle)) continue;
    pushHandle(L, handle);
    lua_rawseti(L, -2, index++);
  }
  return 1;
}
int collectionGetTags(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  InstanceHandle item{};
  if (!state || serviceHandle != state->instances->service("CollectionService") ||
      !LuaInstanceBinding::read(L, 2, item))
    { luaL_error(L, "CollectionService:GetTags expects a live Instance"); return 0; }
  lua_newtable(L);
  int index = 1;
  for (const auto& [tag, tagged] : state->tags) {
    if (std::find(tagged.begin(), tagged.end(), item) == tagged.end()) continue;
    lua_pushlstring(L, tag.data(), tag.size());
    lua_rawseti(L, -2, index++);
  }
  return 1;
}
int collectionSignal(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  std::string tag;
  if (!state || serviceHandle != state->instances->service("CollectionService") || !readTag(L, 2, tag))
    { luaL_error(L, "GetInstanceAddedSignal/GetInstanceRemovedSignal expects a non-empty tag"); return 0; }
  const bool removed = lua_toboolean(L, lua_upvalueindex(1)) != 0;
  pushSignal(L, serviceHandle, removed ? 8 : 7, tag);
  return 1;
}
int textServiceGetTextSize(lua_State* L) {
  const auto serviceHandle = checkedInstance(L, 1);
  auto* instances = registry(L);
  if (!instances || serviceHandle != instances->service("TextService"))
    { luaL_error(L, "GetTextSize must be called on TextService"); return 0; }
  size_t byteLength = 0;
  const char* text = luaL_checklstring(L, 2, &byteLength);
  const double fontSize = luaL_checknumber(L, 3);
  if (byteLength > 16 * 1024 || !std::isfinite(fontSize) || fontSize < 1 || fontSize > 512)
    { luaL_error(L, "TextService:GetTextSize received invalid text or font size"); return 0; }
  float maxWidth = 0;
  if (lua_istable(L, 5)) {
    lua_getfield(L, 5, "X");
    if (lua_isnumber(L, -1)) maxWidth = std::max(0.0f, static_cast<float>(lua_tonumber(L, -1)));
    lua_pop(L, 1);
  }
  const float glyphWidth = static_cast<float>(fontSize) * 0.55f;
  float lineWidth = 0, widest = 0;
  std::size_t lines = 1;
  for (std::size_t i = 0; i < byteLength; ++i) {
    const unsigned char byte = static_cast<unsigned char>(text[i]);
    if (byte == '\n') {
      widest = std::max(widest, lineWidth);
      lineWidth = 0;
      ++lines;
      continue;
    }
    if ((byte & 0xC0) == 0x80) continue;
    if (maxWidth > 0 && lineWidth > 0 && lineWidth + glyphWidth > maxWidth) {
      widest = std::max(widest, lineWidth);
      lineWidth = 0;
      ++lines;
    }
    lineWidth += glyphWidth;
  }
  widest = std::max(widest, lineWidth);
  const float height = static_cast<float>(lines) * static_cast<float>(fontSize) * 1.2f;
  lua_createtable(L, 0, 3);
  lua_pushnumber(L, widest);
  lua_setfield(L, -2, "X");
  lua_pushnumber(L, height);
  lua_setfield(L, -2, "Y");
  lua_pushliteral(L, "Vector2");
  lua_setfield(L, -2, "__bluboxType");
  return 1;
}
bool readTweenValue(lua_State* L, int index, TweenValue& result) {
  if (lua_isnumber(L, index)) {
    const double value = lua_tonumber(L, index);
    if (!std::isfinite(value) || std::abs(value) > 1.0e8) return false;
    result.kind = TweenValue::Kind::Number;
    result.components[0] = static_cast<float>(value);
    return true;
  }
  if (auto* vector = static_cast<LuaVector3*>(lua_touserdatatagged(L, index, kVector3Tag))) {
    result.kind = TweenValue::Kind::Vector3;
    result.components = {vector->x, vector->y, vector->z, 0};
    return std::isfinite(vector->x) && std::isfinite(vector->y) && std::isfinite(vector->z);
  }
  if (auto* color = static_cast<LuaColor3*>(lua_touserdatatagged(L, index, kColor3Tag))) {
    result.kind = TweenValue::Kind::Color3;
    result.components = {color->r, color->g, color->b, 0};
    return std::isfinite(color->r) && std::isfinite(color->g) && std::isfinite(color->b);
  }
  if (auto* size = static_cast<LuaUDim2*>(lua_touserdatatagged(L, index, kUDim2Tag))) {
    result.kind = TweenValue::Kind::UDim2;
    result.components = {size->x.scale, size->x.offset, size->y.scale, size->y.offset};
    return std::all_of(result.components.begin(), result.components.end(),
                       [](float component) { return std::isfinite(component); });
  }
  return false;
}
bool readTweenProperty(const InstanceRecord& instance, std::string_view name, TweenValue& result) {
  if (name == "Position") {
    if (isGuiObject(instance.classId)) {
      result.kind = TweenValue::Kind::UDim2;
      result.components = {instance.uiPosition.x.scale, instance.uiPosition.x.offset,
                           instance.uiPosition.y.scale, instance.uiPosition.y.offset};
    } else {
      result.kind = TweenValue::Kind::Vector3;
      result.components = {instance.local.position.x, instance.local.position.y, instance.local.position.z, 0};
    }
  } else if (name == "Size") {
    if (isGuiObject(instance.classId)) {
      result.kind = TweenValue::Kind::UDim2;
      result.components = {instance.uiSize.x.scale, instance.uiSize.x.offset,
                           instance.uiSize.y.scale, instance.uiSize.y.offset};
    } else {
      result.kind = TweenValue::Kind::Vector3;
      result.components = {instance.size.x, instance.size.y, instance.size.z, 0};
    }
  } else if (name == "Color" || name == "BackgroundColor3" || name == "TextColor3") {
    const auto& color = name == "TextColor3" ? instance.textColor : instance.color;
    result.kind = TweenValue::Kind::Color3;
    result.components = {color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, 0};
  } else if (name == "Transparency" || name == "BackgroundTransparency" ||
             name == "TextTransparency" || name == "Rotation") {
    result.kind = TweenValue::Kind::Number;
    if (name == "TextTransparency") result.components[0] = instance.textTransparency;
    else if (name == "Rotation" && isGuiObject(instance.classId)) result.components[0] = instance.uiRotation;
    else result.components[0] = instance.transparency;
  } else {
    return false;
  }
  return true;
}
void writeTweenProperty(lua_State* L, InstanceHandle handle, std::string_view name,
                        const TweenValue& value) {
  auto* instances = registry(L);
  auto* instance = instances ? instances->get(handle) : nullptr;
  if (!instance) return;
  if (name == "Position" && value.kind == TweenValue::Kind::UDim2 && isGuiObject(instance->classId))
    instance->uiPosition = {{value.components[0], value.components[1]},
                            {value.components[2], value.components[3]}};
  else if (name == "Position" && value.kind == TweenValue::Kind::Vector3) {
    instance->local.position = {value.components[0], value.components[1], value.components[2]};
    instances->markTransformDirty(handle);
  } else if (name == "Size" && value.kind == TweenValue::Kind::UDim2 && isGuiObject(instance->classId))
    instance->uiSize = {{value.components[0], value.components[1]},
                        {value.components[2], value.components[3]}};
  else if (name == "Size" && value.kind == TweenValue::Kind::Vector3)
    instance->size = {value.components[0], value.components[1], value.components[2]};
  else if ((name == "Color" || name == "BackgroundColor3" || name == "TextColor3") &&
           value.kind == TweenValue::Kind::Color3) {
    auto& color = name == "TextColor3" ? instance->textColor : instance->color;
    for (std::size_t channel = 0; channel < 3; ++channel)
      color[channel] = static_cast<std::uint8_t>(std::clamp(std::lround(value.components[channel] * 255), 0l, 255l));
  } else if (name == "TextTransparency" && value.kind == TweenValue::Kind::Number)
    instance->textTransparency = std::clamp(value.components[0], 0.0f, 1.0f);
  else if (name == "Rotation" && isGuiObject(instance->classId) && value.kind == TweenValue::Kind::Number)
    instance->uiRotation = value.components[0];
  else if ((name == "Transparency" || name == "BackgroundTransparency") &&
           value.kind == TweenValue::Kind::Number)
    instance->transparency = std::clamp(value.components[0], 0.0f, 1.0f);
  fire(L, handle, 1, std::string(name).c_str(), {});
}
TweenRecord* findTween(BindingState* state, std::uint64_t id) {
  if (!state) return nullptr;
  const auto found = std::find_if(state->tweens.begin(), state->tweens.end(),
                                  [id](const TweenRecord& tween) { return tween.id == id; });
  return found == state->tweens.end() ? nullptr : &*found;
}
int tweenCreate(lua_State* L) {
  auto* state = binding(L);
  const auto serviceHandle = checkedInstance(L, 1);
  InstanceHandle target{};
  if (!state || serviceHandle != state->instances->service("TweenService") ||
      !LuaInstanceBinding::read(L, 2, target))
    { luaL_error(L, "TweenService:Create expects a live Instance"); return 0; }
  const int info = lua_istable(L, 3) ? lua_absindex(L, 3) : 0;
  const auto* instance = state->instances->get(target);
  if (!info || !instance || !lua_istable(L, 4))
    { luaL_error(L, "TweenService:Create expects TweenInfo and a goal property table"); return 0; }
  lua_getfield(L, info, "Time");
  const double duration = luaL_optnumber(L, -1, 0);
  lua_pop(L, 1);
  lua_getfield(L, info, "DelayTime");
  const double delay = luaL_optnumber(L, -1, 0);
  lua_pop(L, 1);
  lua_getfield(L, info, "RepeatCount");
  const lua_Integer repeats = luaL_optinteger(L, -1, 0);
  lua_pop(L, 1);
  lua_getfield(L, info, "Reverses");
  const bool reverses = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (!std::isfinite(duration) || duration < 0 || duration > 3600 ||
      !std::isfinite(delay) || delay < 0 || delay > 3600 || repeats < 0 || repeats > 100)
    { luaL_error(L, "TweenInfo has invalid duration, delay, or repeat count"); return 0; }
  TweenRecord tween;
  tween.id = state->nextTween++;
  if (tween.id == 0) tween.id = state->nextTween++;
  tween.target = target;
  tween.duration = duration;
  tween.delay = delay;
  tween.repeatCount = static_cast<std::uint32_t>(repeats);
  tween.reverses = reverses;
  const int goals = lua_absindex(L, 4);
  lua_pushnil(L);
  while (lua_next(L, goals) != 0) {
    if (!lua_isstring(L, -2) || tween.properties.size() >= 16) {
      lua_pop(L, 2);
      { luaL_error(L, "Tween goal properties must be strings with at most 16 targets"); return 0; }
    }
    const std::string property = lua_tostring(L, -2);
    TweenValue destination{}, current{};
    if (!readTweenValue(L, -1, destination) || !readTweenProperty(*instance, property, current) ||
        destination.kind != current.kind) {
      lua_pop(L, 2);
      { luaL_error(L, "Tween goal contains an unsupported property or value type"); return 0; }
    }
    tween.properties.push_back({property, current, destination});
    lua_pop(L, 1);
  }
  if (tween.properties.empty())
    { luaL_error(L, "Tween goal table must contain at least one supported property"); return 0; }
  const auto id = tween.id;
  state->tweens.push_back(std::move(tween));
  auto* handle = static_cast<LuaTween*>(lua_newuserdatataggedwithmetatable(L, sizeof(LuaTween), kTweenTag));
  new (handle) LuaTween{id};
  return 1;
}
int tweenPlay(lua_State* L) {
  auto* value = static_cast<LuaTween*>(lua_touserdatatagged(L, 1, kTweenTag));
  auto* state = binding(L);
  auto* tween = value ? findTween(state, value->id) : nullptr;
  if (!tween || tween->destroyed) { luaL_error(L, "Tween is destroyed or invalid"); return 0; }
  const auto* target = state->instances->get(tween->target);
  if (!target) { luaL_error(L, "Tween target is destroyed"); return 0; }
  const double now = state->scheduler ? state->scheduler->now() : 0;
  if (tween->playing && !tween->paused) return 0;
  if (tween->paused) {
    tween->startedAt += now - tween->pausedAt;
    tween->paused = false;
    return 0;
  }
  for (auto& other : state->tweens) {
    if (&other == tween || !other.playing || other.target != tween->target) continue;
    for (const auto& property : tween->properties)
      if (std::any_of(other.properties.begin(), other.properties.end(),
                      [&](const TweenProperty& old) { return old.name == property.name; })) {
        other.playing = false;
        break;
      }
  }
  for (auto& property : tween->properties)
    if (!readTweenProperty(*target, property.name, property.initial))
      { luaL_error(L, "Tween target property is no longer available"); return 0; }
  tween->startedAt = now + tween->delay;
  tween->playing = true;
  tween->paused = false;
  tween->completed = false;
  return 0;
}
int tweenCancel(lua_State* L) {
  auto* value = static_cast<LuaTween*>(lua_touserdatatagged(L, 1, kTweenTag));
  auto* tween = value ? findTween(binding(L), value->id) : nullptr;
  if (tween) {
    tween->playing = false;
    tween->paused = false;
    tween->completed = false;
  }
  return 0;
}
int tweenDestroy(lua_State* L) {
  auto* value = static_cast<LuaTween*>(lua_touserdatatagged(L, 1, kTweenTag));
  auto* tween = value ? findTween(binding(L), value->id) : nullptr;
  if (tween) {
    tween->playing = false;
    tween->paused = false;
    tween->destroyed = true;
    tween->completed = false;
  }
  return 0;
}
int tweenPause(lua_State* L) {
  auto* value = static_cast<LuaTween*>(lua_touserdatatagged(L, 1, kTweenTag));
  auto* state = binding(L);
  auto* tween = value ? findTween(state, value->id) : nullptr;
  if (tween && tween->playing && !tween->paused) {
    tween->paused = true;
    tween->pausedAt = state->scheduler ? state->scheduler->now() : 0;
  }
  return 0;
}
int tweenIndex(lua_State* L) {
  auto* value = static_cast<LuaTween*>(lua_touserdatatagged(L, 1, kTweenTag));
  const char* key = luaL_checkstring(L, 2);
  if (std::strcmp(key, "Play") == 0) lua_pushcfunction(L, tweenPlay, "Tween.Play");
  else if (std::strcmp(key, "Cancel") == 0) lua_pushcfunction(L, tweenCancel, "Tween.Cancel");
  else if (std::strcmp(key, "Pause") == 0) lua_pushcfunction(L, tweenPause, "Tween.Pause");
  else if (std::strcmp(key, "Destroy") == 0) lua_pushcfunction(L, tweenDestroy, "Tween.Destroy");
  else if (std::strcmp(key, "PlaybackState") == 0) {
    auto* tween = value ? findTween(binding(L), value->id) : nullptr;
    lua_pushstring(L, !tween || tween->destroyed ? "Cancelled" :
                      (tween->paused ? "Paused" : tween->playing ? "Playing" :
                       tween->completed ? "Completed" : "Cancelled"));
  } else lua_pushnil(L);
  return 1;
}
int tweenInfoNew(lua_State* L) {
  const double time = luaL_checknumber(L, 1);
  if (!std::isfinite(time) || time < 0 || time > 3600)
    { luaL_error(L, "TweenInfo duration must be between 0 and 3600 seconds"); return 0; }
  lua_createtable(L, 0, 4);
  lua_pushnumber(L, time);
  lua_setfield(L, -2, "Time");
  lua_pushinteger(L, luaL_optinteger(L, 4, 0));
  lua_setfield(L, -2, "RepeatCount");
  lua_pushboolean(L, lua_toboolean(L, 5));
  lua_setfield(L, -2, "Reverses");
  lua_pushnumber(L, luaL_optnumber(L, 6, 0));
  lua_setfield(L, -2, "DelayTime");
  return 1;
}
void fire(lua_State *L, InstanceHandle h, std::uint8_t event, const char *property, InstanceHandle other) {
  auto *b = binding(L);
  if (!b)
    return;
  std::vector<std::uint64_t> ids;
  for (const auto &[id, c] : b->connections) {
    const bool matches = (c.event == event && (event != 7 && event != 8 ||
                                                (property && c.property == property))) ||
                         (event == 1 && c.event == 2 && property && c.property == property);
    if (c.connected && c.owner == h && matches)
      ids.push_back(id);
  }
  for (auto id : ids) {
    auto i = b->connections.find(id);
    if (i == b->connections.end() || !i->second.connected)
      continue;
    lua_getref(L, i->second.callback);
    int nargs = 0;
    if (event == 1 && i->second.event == 1) {
      lua_pushstring(L, property ? property : "");
      nargs = 1;
    } else if ((event == 0 || event == 3 || event == 4 || event == 7 || event == 8) && other) {
      pushHandle(L, other);
      nargs = 1;
    } else if (event == 5) {
      pushHandle(L, h);
      if (other)
        pushHandle(L, other);
      else
        lua_pushnil(L);
      nargs = 2;
    }
    if (b->scheduler) {
      b->scheduler->spawnWithArguments(L, -nargs - 1, -nargs, nargs);
      lua_pop(L, nargs + 1);
    } else {
      if (lua_pcall(L, nargs, 0, 0) != LUA_OK)
        lua_pop(L, 1);
    }
  }
}

int instanceIndex(lua_State *L) {
  auto h = checkedInstance(L, 1);
  const char *key = luaL_checkstring(L, 2);
  auto *r = registry(L);
  auto *rec = r->get(h);
  if (std::strcmp(key, "Name") == 0) {
    lua_pushlstring(L, rec->name.data(), rec->name.size());
    return 1;
  }
  if (std::strcmp(key, "ClassName") == 0) {
    lua_pushstring(L, rec->classId == ClassId::Service ? rec->name.c_str() : className(rec->classId));
    return 1;
  }
  if (std::strcmp(key, "LocalPlayer") == 0 && rec->classId == ClassId::Service &&
      rec->name == "Players") {
    for (const auto child : r->children(h)) {
      const auto *player = r->get(child);
      if (player && player->classId == ClassId::Player) {
        pushHandle(L, child);
        return 1;
      }
    }
    lua_pushnil(L);
    return 1;
  }
  if (std::strcmp(key, "Parent") == 0) {
    if (rec->parent == UINT32_MAX)
      lua_pushnil(L);
    else
      pushHandle(L, r->handleForId(rec->parent));
    return 1;
  }
  if (isGuiObject(rec->classId) && std::strcmp(key, "Position") == 0) {
    pushUDim2(L, rec->uiPosition);
    return 1;
  }
  if (isGuiObject(rec->classId) && std::strcmp(key, "Size") == 0) {
    pushUDim2(L, rec->uiSize);
    return 1;
  }
  if (std::strcmp(key, "Position") == 0) {
    pushVector(L, rec->local.position);
    return 1;
  }
  if (std::strcmp(key, "CFrame") == 0) {
    pushCFrame(L, rec->local.position, rec->local.rotation);
    return 1;
  }
  if (std::strcmp(key, "Rotation") == 0) {
    pushVector(L, rec->local.rotation);
    return 1;
  }
  if (std::strcmp(key, "Size") == 0) {
    pushVector(L, rec->size);
    return 1;
  }
  if (std::strcmp(key, "Color") == 0) {
    pushColor(L, rec->color);
    return 1;
  }
  if (std::strcmp(key, "Text") == 0 &&
      (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton ||
       rec->classId == ClassId::TextBox)) {
    lua_pushlstring(L, rec->text.data(), rec->text.size());
    return 1;
  }
  if (std::strcmp(key, "TextColor3") == 0 &&
      (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton ||
       rec->classId == ClassId::TextBox)) {
    pushColor(L, rec->textColor);
    return 1;
  }
  if (std::strcmp(key, "TextTransparency") == 0 &&
      (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton ||
       rec->classId == ClassId::TextBox)) {
    lua_pushnumber(L, rec->textTransparency);
    return 1;
  }
  if (std::strcmp(key, "Anchored") == 0) {
    lua_pushboolean(L, rec->anchored);
    return 1;
  }
  if (std::strcmp(key, "Visible") == 0 || std::strcmp(key, "Transparency") == 0) {
    if (std::strcmp(key, "Visible") == 0)
      lua_pushboolean(L, rec->visible);
    else
      lua_pushnumber(L, rec->transparency);
    return 1;
  }
  if (std::strcmp(key, "Enabled") == 0) {
    lua_pushboolean(L, rec->enabled);
    return 1;
  }
  if (std::strcmp(key, "AnchorPoint") == 0 && isGuiObject(rec->classId)) {
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, rec->uiAnchor.x);
    lua_setfield(L, -2, "X");
    lua_pushnumber(L, rec->uiAnchor.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, rec->uiAnchor.y);
    lua_setfield(L, -2, "Y");
    lua_pushnumber(L, rec->uiAnchor.y);
    lua_setfield(L, -2, "y");
    lua_pushliteral(L, "Vector2");
    lua_setfield(L, -2, "__bluboxType");
    return 1;
  }
  if (std::strcmp(key, "Rotation") == 0 && isGuiObject(rec->classId)) {
    lua_pushnumber(L, rec->uiRotation);
    return 1;
  }
  if ((std::strcmp(key, "ZIndex") == 0 || std::strcmp(key, "LayoutOrder") == 0) &&
      isGuiObject(rec->classId)) {
    lua_pushinteger(L, std::strcmp(key, "ZIndex") == 0 ? rec->uiZIndex : rec->uiLayoutOrder);
    return 1;
  }
  if (std::strcmp(key, "ClipsDescendants") == 0 && isGuiObject(rec->classId)) {
    lua_pushboolean(L, rec->uiClipsDescendants);
    return 1;
  }
  if ((std::strcmp(key, "BackgroundColor3") == 0 || std::strcmp(key, "BackgroundColor") == 0) &&
      isGuiObject(rec->classId)) {
    pushColor(L, rec->color);
    return 1;
  }
  if ((std::strcmp(key, "BackgroundTransparency") == 0) && isGuiObject(rec->classId)) {
    lua_pushnumber(L, rec->transparency);
    return 1;
  }
  if (std::strcmp(key, "Image") == 0 &&
      (rec->classId == ClassId::ImageLabel || rec->classId == ClassId::ImageButton)) {
    lua_pushlstring(L, rec->imagePath.data(), rec->imagePath.size());
    return 1;
  }
  if (std::strcmp(key, "Source") == 0) {
    lua_pushlstring(L, rec->source.data(), rec->source.size());
    return 1;
  }
  if (std::strcmp(key, "Touched") == 0) {
    pushSignal(L, h, 0);
    return 1;
  }
  if (std::strcmp(key, "Changed") == 0) {
    pushSignal(L, h, 1);
    return 1;
  }
  if (std::strcmp(key, "ChildAdded") == 0) {
    pushSignal(L, h, 3);
    return 1;
  }
  if (std::strcmp(key, "ChildRemoved") == 0) {
    pushSignal(L, h, 4);
    return 1;
  }
  if (std::strcmp(key, "AncestryChanged") == 0) {
    pushSignal(L, h, 5);
    return 1;
  }
  if (std::strcmp(key, "Destroying") == 0) {
    pushSignal(L, h, 6);
    return 1;
  }
  if (std::strcmp(key, "GetPropertyChangedSignal") == 0) {
    lua_pushcfunction(
        L,
        [](lua_State *s) -> int {
          const auto instance = checkedInstance(s, 1);
          const char *property = luaL_checkstring(s, 2);
          if (std::strlen(property) > 128) {
            luaL_error(s, "Property signal name exceeds 128 bytes");
            return 0;
          }
          pushSignal(s, instance, 2, property);
          return 1;
        },
        "Instance.GetPropertyChangedSignal");
    return 1;
  }
  if (rec->classId == ClassId::Service && rec->name == "Debris" &&
      std::strcmp(key, "AddItem") == 0) {
    lua_pushcfunction(L, debrisAddItem, "Debris.AddItem");
    return 1;
  }
  if (rec->classId == ClassId::Service && rec->name == "TweenService" &&
      std::strcmp(key, "Create") == 0) {
    lua_pushcfunction(L, tweenCreate, "TweenService.Create");
    return 1;
  }
  if (rec->classId == ClassId::Service && rec->name == "TextService" &&
      std::strcmp(key, "GetTextSize") == 0) {
    lua_pushcfunction(L, textServiceGetTextSize, "TextService.GetTextSize");
    return 1;
  }
  if (rec->classId == ClassId::Service && rec->name == "CollectionService") {
    if (std::strcmp(key, "AddTag") == 0)
      lua_pushcfunction(L, collectionAddTag, "CollectionService.AddTag");
    else if (std::strcmp(key, "RemoveTag") == 0)
      lua_pushcfunction(L, collectionRemoveTag, "CollectionService.RemoveTag");
    else if (std::strcmp(key, "HasTag") == 0)
      lua_pushcfunction(L, collectionHasTag, "CollectionService.HasTag");
    else if (std::strcmp(key, "GetTagged") == 0)
      lua_pushcfunction(L, collectionGetTagged, "CollectionService.GetTagged");
    else if (std::strcmp(key, "GetTags") == 0)
      lua_pushcfunction(L, collectionGetTags, "CollectionService.GetTags");
    else if (std::strcmp(key, "GetInstanceAddedSignal") == 0 ||
             std::strcmp(key, "GetInstanceRemovedSignal") == 0) {
      lua_pushboolean(L, std::strcmp(key, "GetInstanceRemovedSignal") == 0);
      lua_pushcclosure(L, collectionSignal, "CollectionService.GetInstanceSignal", 1);
    } else {
      lua_pushnil(L);
    }
    return 1;
  }
  if (std::strcmp(key, "GetChildren") == 0) {
    lua_pushcfunction(
        L,
        [](lua_State *s) -> int {
          auto x = checkedInstance(s, 1);
          auto *instances = registry(s);
          auto children = x == instances->root() ? std::vector<InstanceHandle>{} : instances->children(x);
          lua_createtable(s, static_cast<int>(children.size()), 0);
          int n = 1;
          for (auto child : children) {
            pushHandle(s, child);
            lua_rawseti(s, -2, n++);
          }
          return 1;
        },
        "Instance.GetChildren");
    return 1;
  }
  if (std::strcmp(key, "FindFirstChild") == 0) {
    lua_pushcfunction(L, instanceFindFirstChild, "Instance.FindFirstChild");
    return 1;
  }
  if (std::strcmp(key, "WaitForChild") == 0) {
    lua_pushcfunction(L, instanceWaitForChild, "Instance.WaitForChild");
    return 1;
  }
  if (std::strcmp(key, "Destroy") == 0) {
    lua_pushcfunction(L, instanceDestroy, "Instance.Destroy");
    return 1;
  }
  if (std::strcmp(key, "Clone") == 0) {
    lua_pushcfunction(L, instanceClone, "Instance.Clone");
    return 1;
  }
  if (std::strcmp(key, "IsA") == 0) {
    lua_pushcfunction(L, instanceIsA, "Instance.IsA");
    return 1;
  }
  if (h != r->root()) {
    for (auto child : r->children(h)) {
      auto *c = r->get(child);
      if (c && c->name == key) {
        pushHandle(L, child);
        return 1;
      }
    }
  }
  lua_getfield(L, LUA_REGISTRYINDEX, kMethodsKey);
  lua_getfield(L, -1, key);
  lua_remove(L, -2);
  return 1;
}
int instanceNewIndex(lua_State *L) {
  auto h = checkedInstance(L, 1);
  const char *key = luaL_checkstring(L, 2);
  auto *r = registry(L);
  auto *rec = r->get(h);
  if (std::strcmp(key, "Parent") == 0) {
    const auto previousParent = rec->parent == UINT32_MAX ? InstanceHandle{} : r->handleForId(rec->parent);
    if (lua_isnil(L, 3)) {
      if (previousParent == InstanceHandle{})
        return 0;
      if (!r->setParent(h, {})) {
        luaL_error(L, "Parent cannot be changed for this Instance");
        return 0;
      }
      fire(L, h, 1, key, {});
      fire(L, previousParent, 4, nullptr, h);
      std::vector<InstanceHandle> pending{h};
      while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        fire(L, current, 5, nullptr, {});
        for (auto child : r->children(current))
          pending.push_back(child);
      }
      return 0;
    }
    auto *p = static_cast<InstanceHandle *>(lua_touserdatatagged(L, 3, kInstanceTag));
    if (p && previousParent == *p)
      return 0;
    if (!p || !r->setParent(h, *p)) {
      luaL_error(L, "Parent must be a live Instance and cannot create a cycle or reparent a service");
      return 0;
    }
    fire(L, h, 1, key, {});
    if (previousParent)
      fire(L, previousParent, 4, nullptr, h);
    fire(L, *p, 3, nullptr, h);
    std::vector<InstanceHandle> pending{h};
    while (!pending.empty()) {
      const auto current = pending.back();
      pending.pop_back();
      const auto *currentRecord = r->get(current);
      const auto currentParent = currentRecord && currentRecord->parent != UINT32_MAX
                                     ? r->handleForId(currentRecord->parent)
                                     : InstanceHandle{};
      fire(L, current, 5, nullptr, currentParent);
      for (auto child : r->children(current))
        pending.push_back(child);
    }
    return 0;
  }
  bool changed = true;
  if (std::strcmp(key, "Name") == 0) {
    const char *v = luaL_checkstring(L, 3);
    if (std::strlen(v) > 128) {
      luaL_error(L, "Name exceeds 128 bytes");
      return 0;
    }
    rec->name = v;
  } else if (isGuiObject(rec->classId) && std::strcmp(key, "Position") == 0) {
    UDim2 value{};
    if (!readUDim2(L, 3, value)) {
      luaL_error(L, "Position expects UDim2.new(xScale, xOffset, yScale, yOffset)");
      return 0;
    }
    rec->uiPosition = value;
  } else if (isGuiObject(rec->classId) && std::strcmp(key, "Size") == 0) {
    UDim2 value{};
    if (!readUDim2(L, 3, value)) {
      luaL_error(L, "Size expects UDim2.new(xScale, xOffset, yScale, yOffset)");
      return 0;
    }
    rec->uiSize = value;
  } else if (std::strcmp(key, "Position") == 0) {
    Vec3 v{};
    if (!readVector(L, 3, v)) {
      luaL_error(L, "Position expects Vector3.new(x, y, z)");
      return 0;
    }
    rec->local.position = v;
    r->markTransformDirty(h);
  } else if (std::strcmp(key, "CFrame") == 0) {
    Vec3 position{};
    Vec3 rotation{};
    if (!lua_istable(L, 3)) {
      luaL_error(L, "CFrame expects CFrame.new(...)");
      return 0;
    }
    lua_getfield(L, 3, "Position");
    const bool positionValid = readVector(L, -1, position);
    lua_pop(L, 1);
    lua_getfield(L, 3, "Rotation");
    const bool rotationValid = lua_isnil(L, -1) || readVector(L, -1, rotation);
    lua_pop(L, 1);
    if (!positionValid || !rotationValid) {
      luaL_error(L, "CFrame Position and Rotation must be Vector3 values");
      return 0;
    }
    rec->local.position = position;
    rec->local.rotation = rotation;
    r->markTransformDirty(h);
  } else if (std::strcmp(key, "Rotation") == 0) {
    Vec3 v{};
    if (!readVector(L, 3, v)) {
      luaL_error(L, "Rotation expects Vector3.new(x, y, z)");
      return 0;
    }
    rec->local.rotation = v;
    r->markTransformDirty(h);
  } else if (std::strcmp(key, "Size") == 0) {
    Vec3 v{};
    if (!readVector(L, 3, v) || v.x <= 0 || v.y <= 0 || v.z <= 0) {
      luaL_error(L, "Size expects a positive Vector3");
      return 0;
    }
    rec->size = v;
  } else if (std::strcmp(key, "Color") == 0) {
    std::array<std::uint8_t, 4> c{};
    if (!readColor(L, 3, c)) {
      luaL_error(L, "Color expects Color3.new or Color3.fromRGB");
      return 0;
    }
    rec->color = c;
  } else if (std::strcmp(key, "Text") == 0 &&
             (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton ||
              rec->classId == ClassId::TextBox)) {
    size_t length = 0;
    const char *value = luaL_checklstring(L, 3, &length);
    if (length > 16 * 1024) {
      luaL_error(L, "Text exceeds 16 KiB");
      return 0;
    }
    rec->text.assign(value, length);
  } else if (std::strcmp(key, "TextColor3") == 0 &&
             (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton ||
              rec->classId == ClassId::TextBox)) {
    std::array<std::uint8_t, 4> color{};
    if (!readColor(L, 3, color)) {
      luaL_error(L, "TextColor3 expects Color3.new or Color3.fromRGB");
      return 0;
    }
    rec->textColor = color;
  } else if (std::strcmp(key, "TextTransparency") == 0 &&
             (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton ||
              rec->classId == ClassId::TextBox)) {
    const double value = luaL_checknumber(L, 3);
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
      luaL_error(L, "TextTransparency must be a number from 0 to 1");
      return 0;
    }
    rec->textTransparency = static_cast<float>(value);
  } else if (isGuiObject(rec->classId) && std::strcmp(key, "AnchorPoint") == 0) {
    float x = 0, y = 0;
    if (!readVector2(L, 3, x, y) || x < 0 || x > 1 || y < 0 || y > 1) {
      luaL_error(L, "AnchorPoint expects a Vector2 with components from 0 to 1");
      return 0;
    }
    rec->uiAnchor = {x, y, 0};
  } else if (isGuiObject(rec->classId) && std::strcmp(key, "Rotation") == 0) {
    const double value = luaL_checknumber(L, 3);
    if (!std::isfinite(value) || std::abs(value) > 360000.0) {
      luaL_error(L, "Rotation must be a finite number");
      return 0;
    }
    rec->uiRotation = static_cast<float>(value);
  } else if (isGuiObject(rec->classId) &&
             (std::strcmp(key, "ZIndex") == 0 || std::strcmp(key, "LayoutOrder") == 0)) {
    const lua_Integer value = luaL_checkinteger(L, 3);
    if (value < 0 || value > 1000000) {
      luaL_error(L, "%s must be between 0 and 1000000", key);
      return 0;
    }
    if (std::strcmp(key, "ZIndex") == 0) rec->uiZIndex = static_cast<std::uint32_t>(value);
    else rec->uiLayoutOrder = static_cast<std::uint32_t>(value);
  } else if (isGuiObject(rec->classId) && std::strcmp(key, "ClipsDescendants") == 0) {
    rec->uiClipsDescendants = lua_toboolean(L, 3) != 0;
  } else if (isGuiObject(rec->classId) &&
             (std::strcmp(key, "BackgroundColor3") == 0 || std::strcmp(key, "BackgroundColor") == 0)) {
    std::array<std::uint8_t, 4> color{};
    if (!readColor(L, 3, color)) {
      luaL_error(L, "BackgroundColor3 expects Color3.new or Color3.fromRGB");
      return 0;
    }
    rec->color = color;
  } else if (isGuiObject(rec->classId) && std::strcmp(key, "BackgroundTransparency") == 0) {
    const double value = luaL_checknumber(L, 3);
    if (!std::isfinite(value) || value < 0 || value > 1) {
      luaL_error(L, "BackgroundTransparency must be a number from 0 to 1");
      return 0;
    }
    rec->transparency = static_cast<float>(value);
  } else if ((rec->classId == ClassId::ImageLabel || rec->classId == ClassId::ImageButton) &&
             std::strcmp(key, "Image") == 0) {
    size_t length = 0;
    const char* value = luaL_checklstring(L, 3, &length);
    if (length > 4096) {
      luaL_error(L, "Image asset reference exceeds 4 KiB");
      return 0;
    }
    rec->imagePath.assign(value, length);
  } else if (std::strcmp(key, "Anchored") == 0)
    rec->anchored = lua_toboolean(L, 3) != 0;
  else if (std::strcmp(key, "Visible") == 0)
    rec->visible = lua_toboolean(L, 3) != 0;
  else if (std::strcmp(key, "Transparency") == 0) {
    const double value = luaL_checknumber(L, 3);
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
      luaL_error(L, "Transparency must be a number from 0 to 1");
      return 0;
    }
    rec->transparency = static_cast<float>(value);
  }
  else if (std::strcmp(key, "Enabled") == 0)
    rec->enabled = lua_toboolean(L, 3) != 0;
  else if (std::strcmp(key, "Source") == 0) {
    size_t n = 0;
    const char *v = luaL_checklstring(L, 3, &n);
    if (n > 256 * 1024) {
      luaL_error(L, "Source exceeds 256 KiB");
      return 0;
    }
    rec->source.assign(v, n);
  } else {
    changed = false;
    luaL_error(L, "Unknown or read-only property '%s' on %s", key, className(rec->classId));
    return 0;
  }
  if (changed)
    fire(L, h, 1, key, {});
  return 0;
}
int getServiceNative(lua_State *L) {
  const char *name = luaL_checkstring(L, 2);
  auto *r = registry(L);
  auto h = r ? r->service(name) : InstanceHandle{};
  if (h)
    pushHandle(L, h);
  else
    lua_pushnil(L);
  return 1;
}
int stateGc(lua_State *L) {
  auto *b = static_cast<BindingState *>(lua_touserdata(L, 1));
  if (b) {
    for (auto &[_, c] : b->connections)
      if (c.callback != LUA_NOREF)
        lua_unref(L, c.callback);
    b->~BindingState();
  }
  return 0;
}
} // namespace

void LuaInstanceBinding::install(lua_State *L, InstanceRegistry &r) {
  lua_newtable(L);
  lua_pushcfunction(L, stateGc, "binding state gc");
  lua_setfield(L, -2, "__gc");
  lua_setuserdatametatable(L, kBindingStateTag);
  auto *b = static_cast<BindingState *>(lua_newuserdatataggedwithmetatable(L, sizeof(BindingState), kBindingStateTag));
  new (b) BindingState{};
  b->instances = &r;
  lua_setfield(L, LUA_REGISTRYINDEX, kBindingStateKey);
  lua_newtable(L);
  lua_pushcfunction(L, instanceIndex, "Instance.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, instanceNewIndex, "Instance.__newindex");
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, instanceEqual, "Instance.__eq");
  lua_setfield(L, -2, "__eq");
  lua_pushliteral(L, "Instance");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kInstanceTag);
  lua_newtable(L);
  lua_pushcfunction(L, signalIndex, "Signal.__index");
  lua_setfield(L, -2, "__index");
  lua_pushliteral(L, "RBXScriptSignal");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kSignalTag);
  lua_newtable(L);
  lua_pushcfunction(L, connectionIndex, "Connection.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, connectionDisconnect, "Connection.Disconnect");
  lua_setfield(L, -2, "Disconnect");
  lua_setuserdatametatable(L, kConnectionTag);
  lua_newtable(L);
  lua_pushcfunction(L, vectorIndex, "Vector3.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, vectorReadOnly, "Vector3.__newindex");
  lua_setfield(L, -2, "__newindex");
  lua_pushcfunction(L, vectorAdd, "Vector3.__add");
  lua_setfield(L, -2, "__add");
  lua_pushcfunction(L, vectorSub, "Vector3.__sub");
  lua_setfield(L, -2, "__sub");
  lua_pushcfunction(L, vectorMul, "Vector3.__mul");
  lua_setfield(L, -2, "__mul");
  lua_pushliteral(L, "Vector3");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kVector3Tag);
  lua_newtable(L);
  lua_pushcfunction(L, colorIndex, "Color3.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, colorReadOnly, "Color3.__newindex");
  lua_setfield(L, -2, "__newindex");
  lua_pushliteral(L, "Color3");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kColor3Tag);
  lua_newtable(L);
  lua_pushcfunction(L, udimIndex, "UDim.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, udimReadOnly, "UDim.__newindex");
  lua_setfield(L, -2, "__newindex");
  lua_pushliteral(L, "UDim");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kUDimTag);
  lua_newtable(L);
  lua_pushcfunction(L, udim2Index, "UDim2.__index");
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, udim2ReadOnly, "UDim2.__newindex");
  lua_setfield(L, -2, "__newindex");
  lua_pushliteral(L, "UDim2");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kUDim2Tag);
  lua_newtable(L);
  lua_pushcfunction(L, tweenIndex, "Tween.__index");
  lua_setfield(L, -2, "__index");
  lua_pushliteral(L, "Tween");
  lua_setfield(L, -2, "__type");
  lua_setuserdatametatable(L, kTweenTag);
  lua_newtable(L);
  lua_pushcfunction(L, instanceNew, "Instance.new");
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "Instance");
  lua_newtable(L);
  lua_pushcfunction(L, vectorNew, "Vector3.new");
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "Vector3");
  lua_newtable(L);
  lua_pushcfunction(L, vector2New, "Vector2.new");
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "Vector2");
  lua_newtable(L);
  lua_pushcfunction(L, udimNew, "UDim.new");
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "UDim");
  lua_newtable(L);
  lua_pushcfunction(L, udim2New, "UDim2.new");
  lua_setfield(L, -2, "new");
  lua_pushcfunction(L, udim2FromScale, "UDim2.fromScale");
  lua_setfield(L, -2, "fromScale");
  lua_pushcfunction(L, udim2FromOffset, "UDim2.fromOffset");
  lua_setfield(L, -2, "fromOffset");
  lua_setglobal(L, "UDim2");
  lua_newtable(L);
  lua_pushcfunction(L, tweenInfoNew, "TweenInfo.new");
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "TweenInfo");
  lua_newtable(L);
  lua_pushcfunction(L, colorNew, "Color3.new");
  lua_setfield(L, -2, "new");
  lua_pushcfunction(L, colorFromRGBNative, "Color3.fromRGB");
  lua_setfield(L, -2, "fromRGB");
  lua_setglobal(L, "Color3");
  lua_newtable(L);
  lua_pushcfunction(L, cframeNew, "CFrame.new");
  lua_setfield(L, -2, "new");
  lua_setglobal(L, "CFrame");
  lua_pushcfunction(L, luaTypeof, "typeof");
  lua_setglobal(L, "typeof");
  lua_newtable(L);
  addEnumGroup(L, -1, "Material", {"Plastic", "SmoothPlastic", "Concrete", "Grass", "Metal", "Wood", "Neon", "Glass"});
  addEnumGroup(L, -1, "PartType", {"Block", "Ball", "Cylinder"});
  addEnumGroup(L, -1, "Font", {"Gotham", "SourceSans", "Arial", "Code"});
  addEnumGroup(L, -1, "EasingStyle", {"Linear", "Sine", "Back", "Quad", "Quart", "Quint", "Bounce", "Elastic"});
  addEnumGroup(L, -1, "EasingDirection", {"In", "Out", "InOut"});
  lua_setglobal(L, "Enum");
  lua_newtable(L);
  lua_pushcfunction(L, brickColorNew, "BrickColor.new");
  lua_setfield(L, -2, "new");
  lua_pushcfunction(L, colorFromRGBNative, "BrickColor.fromRGB");
  lua_setfield(L, -2, "fromRGB");
  lua_setglobal(L, "BrickColor");
  lua_pushcfunction(L, luaTime, "time");
  lua_setglobal(L, "time");
  lua_pushcfunction(L, luaTime, "tick");
  lua_setglobal(L, "tick");
  lua_newtable(L);
  lua_pushcfunction(L, getServiceNative, "game.GetService");
  lua_setfield(L, -2, "GetService");
  lua_newtable(L);
  lua_pushcfunction(L, dataModelIndex, "game.__index");
  lua_setfield(L, -2, "__index");
  lua_setmetatable(L, -2);
  lua_setglobal(L, "game");
  auto w = r.service("Workspace");
  if (w)
    push(L, w);
  else
    lua_pushnil(L);
  lua_setglobal(L, "workspace");
  lua_newtable(L);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, kMethodsKey);
  lua_pop(L, 1);
}
void LuaInstanceBinding::setScheduler(lua_State *L, TaskScheduler &scheduler) {
  if (auto *b = binding(L))
    b->scheduler = &scheduler;
}
void LuaInstanceBinding::push(lua_State *L, InstanceHandle h) {
  auto *p = static_cast<InstanceHandle *>(lua_newuserdatataggedwithmetatable(L, sizeof(InstanceHandle), kInstanceTag));
  new (p) InstanceHandle(h);
}
bool LuaInstanceBinding::read(lua_State *L, int i, InstanceHandle &out) {
  auto *p = static_cast<InstanceHandle *>(lua_touserdatatagged(L, i, kInstanceTag));
  auto *r = registry(L);
  if (!p || !r || !r->alive(*p))
    return false;
  out = *p;
  return true;
}
void LuaInstanceBinding::fireTouched(lua_State *L, InstanceHandle a, InstanceHandle b) {
  fire(L, a, 0, nullptr, b);
}
void LuaInstanceBinding::tickServices(lua_State* L) {
  auto* state = binding(L);
  if (!state || !state->scheduler) return;
  const double now = state->scheduler->now();
  for (auto& tween : state->tweens) {
    if (!tween.playing || tween.paused || tween.destroyed) continue;
    if (!state->instances->alive(tween.target)) {
      tween.playing = false;
      continue;
    }
    const double cycleLength = tween.duration * (tween.reverses ? 2.0 : 1.0);
    const double total = cycleLength * (static_cast<double>(tween.repeatCount) + 1.0);
    const double elapsed = std::max(0.0, now - tween.startedAt);
    const bool finished = tween.duration == 0 || elapsed >= total;
    const double bounded = finished ? total : elapsed;
    const auto cycle = cycleLength > 0
                           ? static_cast<std::uint32_t>(std::min<double>(tween.repeatCount,
                                 std::floor(bounded / cycleLength)))
                           : 0;
    const double withinCycle = cycleLength > 0 ? std::fmod(bounded, cycleLength) : 0;
    double alpha = tween.duration > 0 ? std::clamp(withinCycle / tween.duration, 0.0, 1.0) : 1.0;
    if (tween.reverses && withinCycle > tween.duration)
      alpha = std::clamp(2.0 - withinCycle / tween.duration, 0.0, 1.0);
    if (finished)
      alpha = tween.reverses && tween.repeatCount % 2 != 0 ? 0.0 : 1.0;
    for (const auto& property : tween.properties) {
      TweenValue value = property.initial;
      for (std::size_t i = 0; i < value.components.size(); ++i)
        value.components[i] = static_cast<float>(property.initial.components[i] +
            (property.target.components[i] - property.initial.components[i]) * alpha);
      writeTweenProperty(L, tween.target, property.name, value);
    }
    if (finished) {
      tween.playing = false;
      tween.completed = true;
    }
  }
  std::vector<InstanceHandle> expired;
  auto& pending = state->debris;
  for (auto i = pending.begin(); i != pending.end();) {
    if (!state->instances->alive(i->handle)) {
      i = pending.erase(i);
    } else if (i->destroyAt <= now) {
      expired.push_back(i->handle);
      i = pending.erase(i);
    } else {
      ++i;
    }
  }
  for (const auto handle : expired) {
    if (!state->instances->alive(handle) || state->instances->protectedInstance(handle)) continue;
    const int base = lua_gettop(L);
    pushHandle(L, handle);
    if (instanceDestroy(L) != 0) {
      lua_settop(L, base);
      luaL_error(L, "Debris failed to destroy an expired instance");
      return;
    }
    lua_settop(L, base);
  }
}
} // namespace blubox::engine
