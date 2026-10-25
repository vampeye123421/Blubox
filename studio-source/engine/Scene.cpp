#include "Scene.hpp"
#include <algorithm>
#include <stdexcept>
namespace blubox::engine {
ClassId SceneRuntime::mapClass(const studio::Entity& e) {
  const std::string& k = e.kind;
  if (k == "spawnlocation") return ClassId::SpawnLocation;
  if (k == "block") {
    if (e.name == "SpawnLocation" || e.name == "SpawnPoint") return ClassId::SpawnLocation;
    for (const auto& comp : e.components) {
      if (comp.contains("type") && (comp["type"] == "SpawnLocation" || comp["type"] == "SpawnPoint")) {
        return ClassId::SpawnLocation;
      }
    }
    return ClassId::Part;
  }
  if (k == "sphere") return ClassId::Sphere;
  if (k == "wedgepart") return ClassId::WedgePart;
  if (k == "cornerwedgepart") return ClassId::CornerWedgePart;
  if (k == "meshpart") return ClassId::MeshPart;
  if (k == "shirt") return ClassId::Shirt;
  if (k == "pants") return ClassId::Pants;
  if (k == "accessory") return ClassId::Accessory;
  if (k == "model") return ClassId::Model;
  if (k == "script") return ClassId::Script;
  if (k == "localscript") return ClassId::LocalScript;
  if (k == "screengui") return ClassId::ScreenGui;
  if (k == "frame") return ClassId::Frame;
  if (k == "textlabel") return ClassId::TextLabel;
  if (k == "textbutton") return ClassId::TextButton;
  if (k == "remoteevent") return ClassId::RemoteEvent;
  if (k == "remotefunction") return ClassId::RemoteFunction;
  return ClassId::Folder;
}
SceneRuntime::SceneRuntime(studio::MapDocument& d) : document_(d) {
  handles_.reserve(document_.entities.size());
  for (auto& e : document_.entities) {
    auto h = registry_.create(e.name, mapClass(e));
    if (!h) throw std::runtime_error("Could not allocate a DataModel instance for map entity.");
    handles_.emplace(e.id, h);
    auto* r = registry_.get(h);
    r->sourceId = e.id;
    r->local.position = {e.transform.position[0], e.transform.position[1], e.transform.position[2]};
    r->local.rotation = {e.transform.rotation[0], e.transform.rotation[1], e.transform.rotation[2]};
    r->local.scale = {e.transform.scale[0], e.transform.scale[1], e.transform.scale[2]};
    r->color = e.color;
    if (e.properties.is_object()) {
      if (e.properties.contains("text") && e.properties["text"].is_string())
        r->text = e.properties["text"].get<std::string>();
      if (e.properties.contains("textColor") && e.properties["textColor"].is_array() &&
          e.properties["textColor"].size() >= 3) {
        for (std::size_t channel = 0; channel < 4 && channel < e.properties["textColor"].size(); ++channel) {
          if (!e.properties["textColor"][channel].is_number()) continue;
          const int value = e.properties["textColor"][channel].get<int>();
          r->textColor[channel] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        }
      }
      if (e.properties.contains("textTransparency") && e.properties["textTransparency"].is_number())
        r->textTransparency = std::clamp(e.properties["textTransparency"].get<float>(), 0.0f, 1.0f);
    }
    r->transparency = e.properties.contains("transparency") && e.properties["transparency"].is_number()
        ? std::clamp(e.properties["transparency"].get<float>(), 0.0f, 1.0f)
        : 1.0f - static_cast<float>(e.color[3]) / 255.0f;
    registry_.markTransformDirty(h);
  }
  const auto workspace = registry_.service("Workspace");
  for (const auto& e : document_.entities) {
    auto child = handles_.at(e.id);
    auto p = handles_.find(e.parent);
    auto parent = p == handles_.end() ? registry_.service(e.parent) : p->second;
    if (!parent) parent = workspace;
    if (!registry_.setParent(child, parent))
      throw std::runtime_error("Map entity hierarchy contains a cycle or invalid parent.");
  }
  registry_.flushTransforms();
}
InstanceHandle SceneRuntime::find(std::string_view id) const {
  auto i = handles_.find(std::string(id));
  return i == handles_.end() ? InstanceHandle{} : i->second;
}
void SceneRuntime::syncToDocument() {
  for (auto& e : document_.entities) {
    auto h = find(e.id);
    auto* r = registry_.get(h);
    if (!r) continue;
    e.transform.position = {r->local.position.x, r->local.position.y, r->local.position.z};
    e.transform.rotation = {r->local.rotation.x, r->local.rotation.y, r->local.rotation.z};
    e.transform.scale = {r->local.scale.x, r->local.scale.y, r->local.scale.z};
    e.name = r->name;
    e.color = r->color;
    e.properties["transparency"] = std::clamp(r->transparency, 0.0f, 1.0f);
    if (r->classId == ClassId::TextLabel || r->classId == ClassId::TextButton) {
      e.properties["text"] = r->text;
      e.properties["textColor"] = {r->textColor[0], r->textColor[1], r->textColor[2], 255};
      e.properties["textTransparency"] = std::clamp(r->textTransparency, 0.0f, 1.0f);
    }
    if (r->parent != UINT32_MAX) {
      bool foundParent = false;
      for (const auto& [id, handle] : handles_) {
        if (handle.id == r->parent && registry_.alive(handle)) {
          e.parent = id;
          foundParent = true;
          break;
        }
      }
      if (!foundParent) {
        constexpr std::string_view services[] = {"Workspace", "Players", "RunService", "ServerScriptService",
                                                 "ServerStorage", "ReplicatedStorage", "StarterPlayer", "StarterGui",
                                                 "Lighting"};
        for (auto name : services) {
          const auto serviceHandle = registry_.service(name);
          if (serviceHandle.id == r->parent) {
            e.parent = std::string(name);
            foundParent = true;
            break;
          }
        }
      }
    }
  }
}
void SceneRuntime::syncFromDocument() {
  for (const auto& entity : document_.entities) {
    auto h=find(entity.id);auto* r=registry_.get(h);if(!r)continue;
    r->local.position={entity.transform.position[0],entity.transform.position[1],entity.transform.position[2]};
    r->local.rotation={entity.transform.rotation[0],entity.transform.rotation[1],entity.transform.rotation[2]};
    r->local.scale={entity.transform.scale[0],entity.transform.scale[1],entity.transform.scale[2]};
    r->color=entity.color;
    if (entity.properties.is_object()) {
      if (entity.properties.contains("text") && entity.properties["text"].is_string())
        r->text = entity.properties["text"].get<std::string>();
      if (entity.properties.contains("textColor") && entity.properties["textColor"].is_array() &&
          entity.properties["textColor"].size() >= 3) {
        for (std::size_t channel = 0; channel < 4 && channel < entity.properties["textColor"].size(); ++channel) {
          if (!entity.properties["textColor"][channel].is_number()) continue;
          const int value = entity.properties["textColor"][channel].get<int>();
          r->textColor[channel] = static_cast<std::uint8_t>(std::clamp(value, 0, 255));
        }
      }
      if (entity.properties.contains("textTransparency") && entity.properties["textTransparency"].is_number())
        r->textTransparency = std::clamp(entity.properties["textTransparency"].get<float>(), 0.0f, 1.0f);
    }
    r->transparency = entity.properties.contains("transparency") && entity.properties["transparency"].is_number()
      ? std::clamp(entity.properties["transparency"].get<float>(), 0.0f, 1.0f)
      : 1.0f - static_cast<float>(entity.color[3]) / 255.0f;
  }
}
}  // namespace blubox::engine
