#include "UiRuntimeBridge.hpp"

#include <algorithm>

namespace blubox::engine::ui {
namespace {
bool isGuiObject(ClassId id) {
  return id == ClassId::Frame || id == ClassId::TextLabel || id == ClassId::TextButton ||
         id == ClassId::TextBox || id == ClassId::ImageLabel || id == ClassId::ImageButton ||
         id == ClassId::ScrollingFrame;
}

Element elementFor(ClassId id) {
  switch (id) {
    case ClassId::ScreenGui: return Element::ScreenGui;
    case ClassId::TextLabel: return Element::TextLabel;
    case ClassId::TextButton: return Element::TextButton;
    case ClassId::TextBox: return Element::TextBox;
    case ClassId::ImageLabel: return Element::ImageLabel;
    case ClassId::ImageButton: return Element::ImageButton;
    case ClassId::ScrollingFrame: return Element::ScrollingFrame;
    default: return Element::Frame;
  }
}

Color uiColor(const std::array<std::uint8_t, 4>& value) {
  return {value[0], value[1], value[2], value[3]};
}

void projectChildren(const InstanceRegistry& instances, blubox::engine::InstanceHandle sourceParent,
                     InstanceHandle targetParent, UiScene& target) {
  for (const auto child : instances.children(sourceParent)) {
    const auto* record = instances.get(child);
    if (!record || (!isGuiObject(record->classId) && record->classId != ClassId::ScreenGui))
      continue;
    if (record->classId == ClassId::ScreenGui && !record->enabled)
      continue;
    const auto projected = target.create(elementFor(record->classId), targetParent);
    auto& node = target.get(projected);
    node.position = record->uiPosition;
    node.size = record->uiSize;
    node.anchor = {record->uiAnchor.x, record->uiAnchor.y};
    node.rotation = record->uiRotation;
    node.zIndex = record->uiZIndex;
    node.layoutOrder = record->uiLayoutOrder;
    node.visible = record->visible && record->enabled;
    node.clipsDescendants = record->uiClipsDescendants ||
                            record->classId == ClassId::ScrollingFrame;
    node.text = record->text;
    node.style.background = uiColor(record->color);
    node.style.backgroundTransparency = record->transparency;
    node.style.text = uiColor(record->textColor);
    node.style.textTransparency = record->textTransparency;
    if (record->classId == ClassId::ScreenGui) {
      node.position = {};
      node.size = {UDim{1, 0}, UDim{1, 0}};
      node.style.background = {0, 0, 0, 0};
      node.style.backgroundTransparency = 1;
    }
    projectChildren(instances, child, projected, target);
  }
}
}  // namespace

UiDrawList UiRuntimeBridge::resolve(const InstanceRegistry& instances, Vec2 viewport) {
  scene_ = UiScene{};
  const auto players = instances.service("Players");
  if (players) {
    for (const auto player : instances.children(players)) {
      const auto* playerRecord = instances.get(player);
      if (!playerRecord || playerRecord->classId != ClassId::Player)
        continue;
      for (const auto playerChild : instances.children(player)) {
        const auto* playerChildRecord = instances.get(playerChild);
        if (playerChildRecord && playerChildRecord->classId == ClassId::PlayerGui)
          projectChildren(instances, playerChild, {}, scene_);
      }
    }
  }
  return resolver_.resolve(scene_, viewport);
}

}  // namespace blubox::engine::ui
