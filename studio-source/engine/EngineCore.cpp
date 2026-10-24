#include "EngineCore.hpp"

#include <algorithm>
#include <cmath>

namespace blubox::engine {
namespace {
using M = std::array<float, 16>;
M identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
M multiply(const M& a, const M& b) {
  M o{};
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      for (int k = 0; k < 4; ++k) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
  return o;
}
M localMatrix(const Transform& t) {
  const float cx = std::cos(t.rotation.x), sx = std::sin(t.rotation.x), cy = std::cos(t.rotation.y),
              sy = std::sin(t.rotation.y), cz = std::cos(t.rotation.z), sz = std::sin(t.rotation.z);
  M m{};
  // XYZ Euler rotation, column-major, with local scale folded into basis columns.
  m[0] = (cy * cz) * t.scale.x;
  m[1] = (sx * sy * cz - cx * sz) * t.scale.x;
  m[2] = (cx * sy * cz + sx * sz) * t.scale.x;
  m[4] = (cy * sz) * t.scale.y;
  m[5] = (sx * sy * sz + cx * cz) * t.scale.y;
  m[6] = (cx * sy * sz - sx * cz) * t.scale.y;
  m[8] = (-sy) * t.scale.z;
  m[9] = (sx * cy) * t.scale.z;
  m[10] = (cx * cy) * t.scale.z;
  m[12] = t.position.x;
  m[13] = t.position.y;
  m[14] = t.position.z;
  m[15] = 1;
  return m;
}
}  // namespace

InstanceRegistry::InstanceRegistry() {
  constexpr std::string_view names[] = {"Workspace", "Players", "RunService", "ServerScriptService",
                                        "ServerStorage", "ReplicatedStorage", "StarterPlayer", "StarterGui", "Lighting"};
  for (auto name : names) {
    auto h = create(std::string(name), ClassId::Service);
    services_.emplace_back(name, h);
  }
}
InstanceHandle InstanceRegistry::create(std::string name, ClassId kind, InstanceHandle parent) {
  if (parent && !alive(parent)) return {};
  InstanceId id;
  if (free_.empty()) {
    if (slots_.size() >= UINT32_MAX) return {};
    id = static_cast<InstanceId>(slots_.size());
    slots_.emplace_back();
  } else {
    id = free_.back();
    free_.pop_back();
  }
  auto& s = slots_[id];
  s.occupied = true;
  s.value = InstanceRecord{};
  s.value.transformDirty = false;
  s.value.name = std::move(name);
  s.value.classId = kind;
  if (kind == ClassId::Part || kind == ClassId::SpawnLocation || kind == ClassId::Sphere || kind == ClassId::WedgePart ||
      kind == ClassId::CornerWedgePart || kind == ClassId::MeshPart)
    s.value.componentMask |= HasTransform | HasRenderable | HasPhysics;
  if (kind == ClassId::Model) s.value.componentMask |= HasTransform;
  if (kind == ClassId::Script || kind == ClassId::LocalScript) s.value.componentMask |= HasScript;
  if (kind == ClassId::ScreenGui || kind == ClassId::Frame || kind == ClassId::TextLabel ||
      kind == ClassId::TextButton || kind == ClassId::UIListLayout || kind == ClassId::UIGridLayout ||
      kind == ClassId::UIPadding)
    s.value.componentMask |= HasUi;
  if (kind == ClassId::RemoteEvent || kind == ClassId::RemoteFunction) s.value.componentMask |= HasNetwork;
  if ((s.value.componentMask & HasRenderable) != 0) renderables_.push_back({id, s.generation});
  if (parent) setParent({id, s.generation}, parent);
  ++liveCount_;
  markTransformDirty({id, s.generation});
  return {id, s.generation};
}
bool InstanceRegistry::alive(InstanceHandle h) const noexcept {
  return h.id < slots_.size() && slots_[h.id].occupied && slots_[h.id].generation == h.generation;
}
InstanceRecord* InstanceRegistry::get(InstanceHandle h) noexcept { return alive(h) ? &slots_[h.id].value : nullptr; }
const InstanceRecord* InstanceRegistry::get(InstanceHandle h) const noexcept {
  return alive(h) ? &slots_[h.id].value : nullptr;
}
void InstanceRegistry::unlink(InstanceId id) {
  auto& n = slots_[id].value;
  if (n.parent == UINT32_MAX) return;
  auto& p = slots_[n.parent].value;
  if (p.firstChild == id) p.firstChild = n.nextSibling;
  if (n.prevSibling != UINT32_MAX) slots_[n.prevSibling].value.nextSibling = n.nextSibling;
  if (n.nextSibling != UINT32_MAX) slots_[n.nextSibling].value.prevSibling = n.prevSibling;
  n.parent = n.nextSibling = n.prevSibling = UINT32_MAX;
}
bool InstanceRegistry::isAncestor(InstanceId candidate, InstanceId node) const {
  for (auto p = node; p != UINT32_MAX; p = slots_[p].value.parent)
    if (p == candidate) return true;
  return false;
}
bool InstanceRegistry::setParent(InstanceHandle child, InstanceHandle parent) {
  if (!alive(child) || (parent && !alive(parent)) || (parent && isAncestor(child.id, parent.id))) return false;
  unlink(child.id);
  if (parent) {
    auto& c = slots_[child.id].value;
    auto& p = slots_[parent.id].value;
    c.parent = parent.id;
    c.nextSibling = p.firstChild;
    c.prevSibling = UINT32_MAX;
    if (p.firstChild != UINT32_MAX) slots_[p.firstChild].value.prevSibling = child.id;
    p.firstChild = child.id;
  }
  markTransformDirty(child);
  return true;
}
std::vector<InstanceHandle> InstanceRegistry::children(InstanceHandle p) const {
  std::vector<InstanceHandle> out;
  auto* r = get(p);
  if (!r) return out;
  for (auto id = r->firstChild; id != UINT32_MAX; id = slots_[id].value.nextSibling)
    out.push_back({id, slots_[id].generation});
  return out;
}
InstanceHandle InstanceRegistry::service(std::string_view n) const {
  for (const auto& [name, h] : services_)
    if (name == n && alive(h)) return h;
  return {};
}
InstanceHandle InstanceRegistry::handleForId(InstanceId id) const noexcept {
  if (id >= slots_.size() || !slots_[id].occupied) return {};
  return {id, slots_[id].generation};
}
InstanceHandle InstanceRegistry::findBySourceId(std::string_view sourceId) const noexcept {
  if (sourceId.empty()) return {};
  for (InstanceId id = 0; id < slots_.size(); ++id)
    if (slots_[id].occupied && slots_[id].value.sourceId == sourceId)
      return {id, slots_[id].generation};
  return {};
}
void InstanceRegistry::markTransformDirty(InstanceHandle h) {
  if (!alive(h)) return;
  auto& r = slots_[h.id].value;
  if (r.transformDirty) return;
  r.transformDirty = true;
  dirty_.push_back(h.id);
}
void InstanceRegistry::updateSubtree(InstanceId id, const M& parent) {
  std::vector<std::pair<InstanceId, M>> pending;
  pending.emplace_back(id, parent);
  while (!pending.empty()) {
    const auto [current, parentWorld] = pending.back();
    pending.pop_back();
    auto& r = slots_[current].value;
    r.world.matrix = multiply(parentWorld, localMatrix(r.local));
    r.world.version = ++worldVersion_;
    r.transformDirty = false;
    for (auto c = r.firstChild; c != UINT32_MAX; c = slots_[c].value.nextSibling)
      pending.emplace_back(c, r.world.matrix);
  }
}
void InstanceRegistry::flushTransforms() {
  auto work = std::move(dirty_);
  dirty_.clear();
  std::sort(work.begin(), work.end());
  work.erase(std::unique(work.begin(), work.end()), work.end());
  for (auto id : work) {
    if (id >= slots_.size() || !slots_[id].occupied) continue;
    auto parent = slots_[id].value.parent;
    bool covered = false;
    for (auto p = parent; p != UINT32_MAX; p = slots_[p].value.parent)
      if (std::binary_search(work.begin(), work.end(), p)) {
        covered = true;
        break;
      }
    if (!covered) {
      M base = parent == UINT32_MAX ? identity() : slots_[parent].value.world.matrix;
      updateSubtree(id, base);
    }
  }
}
bool InstanceRegistry::destroy(InstanceHandle h) {
  if (!alive(h)) return false;
  std::vector<InstanceId> pending{h.id};
  std::vector<InstanceId> subtree;
  while (!pending.empty()) {
    const auto id = pending.back();
    pending.pop_back();
    for (auto child = slots_[id].value.firstChild; child != UINT32_MAX; child = slots_[child].value.nextSibling)
      pending.push_back(child);
    subtree.push_back(id);
  }
  for (auto id : subtree) {
    unlink(id);
    auto& s = slots_[id];
    if ((s.value.componentMask & HasRenderable) != 0) {
      const InstanceHandle old{id, s.generation};
      auto i = std::find(renderables_.begin(), renderables_.end(), old);
      if (i != renderables_.end()) renderables_.erase(i);
    }
    s.occupied = false;
    ++s.generation;
    if (s.generation == 0) ++s.generation;
    free_.push_back(id);
    --liveCount_;
  }
  return true;
}
}  // namespace blubox::engine
