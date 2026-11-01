#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blubox::engine {

using InstanceId = std::uint32_t;
using Generation = std::uint32_t;
struct InstanceHandle {
  InstanceId id = UINT32_MAX;
  Generation generation = 0;
  friend bool operator==(InstanceHandle, InstanceHandle) = default;
  explicit operator bool() const noexcept { return id != UINT32_MAX; }
};
struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct UDim {
  float scale = 0, offset = 0;
};
struct UDim2 {
  UDim x{}, y{};
};
struct Transform {
  Vec3 position{};
  Vec3 rotation{};
  Vec3 scale{1, 1, 1};
};
struct WorldTransform {
  std::array<float, 16> matrix{};
  std::uint64_t version = 0;
};
enum class ClassId : std::uint8_t {
  DataModel,
  Folder,
  Model,
  Part,
  Sphere,
  WedgePart,
  CornerWedgePart,
  ScreenGui,
  Frame,
  TextLabel,
  TextButton,
  TextBox,
  ImageLabel,
  ImageButton,
  ScrollingFrame,
  MeshPart,
  Shirt,
  Pants,
  Accessory,
  Light,
  Camera,
  SpawnLocation,
  UIListLayout,
  UIGridLayout,
  UIPadding,
  RemoteEvent,
  RemoteFunction,
  Script,
  LocalScript,
  Service,
  Player,
  PlayerGui
};
enum ComponentBits : std::uint32_t {
  HasTransform = 1u,
  HasRenderable = 2u,
  HasPhysics = 4u,
  HasScript = 8u,
  HasUi = 16u,
  HasNetwork = 32u
};
struct InstanceRecord {
  ClassId classId = ClassId::Folder;
  std::string name;
  InstanceId parent = UINT32_MAX, firstChild = UINT32_MAX, nextSibling = UINT32_MAX, prevSibling = UINT32_MAX;
  std::uint32_t componentMask = 0;
  Transform local{};
  WorldTransform world{};
  bool transformDirty = true, anchored = true, visible = true;
  bool enabled = true;
  float duration = 0.0f;
  float transparency = 0.0f;
  // UI text lives in the DataModel so scripts and editor preview share state.
  std::string text;
  std::array<std::uint8_t, 4> textColor{242, 246, 252, 255};
  float textTransparency = 0.0f;
  float brightness = 1.0f;
  std::string source;
  std::string sourceId;
  Vec3 size{1, 1, 1};
  std::array<std::uint8_t, 4> color{75, 163, 226, 255};
  std::uint64_t meshAssetId = 0;
  std::uint64_t materialId = 0;
  UDim2 uiPosition{};
  UDim2 uiSize{UDim{0, 100}, UDim{0, 100}};
  Vec3 uiAnchor{};
  float uiRotation = 0;
  std::uint32_t uiZIndex = 1;
  std::uint32_t uiLayoutOrder = 0;
  bool uiClipsDescendants = false;
  std::string imagePath;
};

class InstanceRegistry {
 public:
  InstanceRegistry();
  InstanceHandle create(std::string name, ClassId kind, InstanceHandle parent = {});
  bool destroy(InstanceHandle handle);
  bool protectedInstance(InstanceHandle handle) const noexcept;
  InstanceHandle clone(InstanceHandle handle);
  bool alive(InstanceHandle handle) const noexcept;
  InstanceRecord* get(InstanceHandle handle) noexcept;
  const InstanceRecord* get(InstanceHandle handle) const noexcept;
  bool setParent(InstanceHandle child, InstanceHandle parent);
  std::vector<InstanceHandle> children(InstanceHandle parent) const;
  InstanceHandle root() const noexcept { return root_; }
  InstanceHandle service(std::string_view name) const;
  InstanceHandle handleForId(InstanceId id) const noexcept;
  InstanceHandle findBySourceId(std::string_view sourceId) const noexcept;
  std::size_t size() const noexcept { return liveCount_; }
  const std::vector<InstanceId>& dirtyTransforms() const noexcept { return dirty_; }
  const std::vector<InstanceHandle>& renderables() const noexcept { return renderables_; }
  void markTransformDirty(InstanceHandle handle);
  void flushTransforms();

 private:
  struct Slot {
    InstanceRecord value;
    Generation generation = 1;
    bool occupied = false;
  };
  void unlink(InstanceId id);
  bool isAncestor(InstanceId candidate, InstanceId node) const;
  void updateSubtree(InstanceId id, const std::array<float, 16>& parentWorld);
  std::vector<Slot> slots_;
  std::vector<InstanceId> free_;
  std::vector<InstanceId> dirty_;
  std::vector<InstanceHandle> renderables_;
  std::vector<std::pair<std::string, InstanceHandle>> services_;
  InstanceHandle root_{};
  std::size_t liveCount_ = 0;
  std::uint64_t worldVersion_ = 0;
};

using DataModel = InstanceRegistry;

}  // namespace blubox::engine
