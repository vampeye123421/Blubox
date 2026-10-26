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
    lua_pushstring(L, value);
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
  std::vector<InstanceHandle> pending{h};
  while (!pending.empty()) {
    auto current = pending.back();
    pending.pop_back();
    for (auto child : registry(L)->children(current))
      pending.push_back(child);
    disconnectOwner(L, current);
  }
  registry(L)->destroy(h);
  return 0;
}
InstanceHandle cloneTree(InstanceRegistry &r, InstanceHandle source) {
  auto *original = r.get(source);
  if (!original)
    return {};
  const auto children = r.children(source);
  InstanceRecord value = *original;
  value.parent = value.firstChild = value.nextSibling = value.prevSibling = UINT32_MAX;
  value.sourceId.clear();
  auto result = r.create(value.name, value.classId);
  auto *copy = r.get(result);
  if (copy) {
    copy->local = value.local;
    copy->anchored = value.anchored;
    copy->visible = value.visible;
    copy->enabled = value.enabled;
    copy->duration = value.duration;
    copy->brightness = value.brightness;
    copy->source = value.source;
    copy->size = value.size;
    copy->color = value.color;
    copy->meshAssetId = value.meshAssetId;
    copy->materialId = value.materialId;
    r.markTransformDirty(result);
  }
  for (auto child : children) {
    auto cloned = cloneTree(r, child);
    if (cloned)
      r.setParent(cloned, result);
  }
  return result;
}
int instanceClone(lua_State *L) {
  auto h = checkedInstance(L, 1);
  auto cloned = cloneTree(*registry(L), h);
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
  const char *actual = className(rec->classId);
  lua_pushboolean(L, std::strcmp(actual, wanted) == 0 ||
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
void fire(lua_State *L, InstanceHandle h, std::uint8_t event, const char *property, InstanceHandle other) {
  auto *b = binding(L);
  if (!b)
    return;
  std::vector<std::uint64_t> ids;
  for (const auto &[id, c] : b->connections) {
    const bool matches = c.event == event || (event == 1 && c.event == 2 && property && c.property == property);
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
    } else if (event == 0 && other) {
      pushHandle(L, other);
      nargs = 1;
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
    lua_pushstring(L, className(rec->classId));
    return 1;
  }
  if (std::strcmp(key, "Parent") == 0) {
    if (rec->parent == UINT32_MAX)
      lua_pushnil(L);
    else
      pushHandle(L, r->handleForId(rec->parent));
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
      (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton)) {
    lua_pushlstring(L, rec->text.data(), rec->text.size());
    return 1;
  }
  if (std::strcmp(key, "TextColor3") == 0 &&
      (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton)) {
    pushColor(L, rec->textColor);
    return 1;
  }
  if (std::strcmp(key, "TextTransparency") == 0 &&
      (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton)) {
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
  if (std::strcmp(key, "GetChildren") == 0) {
    lua_pushcfunction(
        L,
        [](lua_State *s) -> int {
          auto x = checkedInstance(s, 1);
          auto children = registry(s)->children(x);
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
  for (auto child : r->children(h)) {
    auto *c = r->get(child);
    if (c && c->name == key) {
      pushHandle(L, child);
      return 1;
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
    if (lua_isnil(L, 3)) {
      r->setParent(h, {});
      fire(L, h, 1, key, {});
      return 0;
    }
    auto *p = static_cast<InstanceHandle *>(lua_touserdatatagged(L, 3, kInstanceTag));
    if (!p || !r->setParent(h, *p)) {
      luaL_error(L, "Parent must be a live Instance and cannot create a cycle");
      return 0;
    }
    fire(L, h, 1, key, {});
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
             (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton)) {
    size_t length = 0;
    const char *value = luaL_checklstring(L, 3, &length);
    if (length > 16 * 1024) {
      luaL_error(L, "Text exceeds 16 KiB");
      return 0;
    }
    rec->text.assign(value, length);
  } else if (std::strcmp(key, "TextColor3") == 0 &&
             (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton)) {
    std::array<std::uint8_t, 4> color{};
    if (!readColor(L, 3, color)) {
      luaL_error(L, "TextColor3 expects Color3.new or Color3.fromRGB");
      return 0;
    }
    rec->textColor = color;
  } else if (std::strcmp(key, "TextTransparency") == 0 &&
             (rec->classId == ClassId::TextLabel || rec->classId == ClassId::TextButton)) {
    const double value = luaL_checknumber(L, 3);
    if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
      luaL_error(L, "TextTransparency must be a number from 0 to 1");
      return 0;
    }
    rec->textTransparency = static_cast<float>(value);
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
} // namespace blubox::engine
