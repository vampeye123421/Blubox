#include "SpawnService.hpp"

namespace blubox::engine::play {
SpawnResolution SpawnService::resolve(const studio::MapDocument& world,
                                     std::optional<std::array<std::uint8_t, 4>> team) {
  const studio::Entity* matching = nullptr;
  const studio::Entity* neutral = nullptr;
  const studio::Entity* any = nullptr;
  for (const auto& entity : world.entities) {
    bool isSpawn = (entity.kind == "spawnlocation");
    if (!isSpawn && entity.kind == "block") {
      if (entity.name == "SpawnLocation" || entity.name == "SpawnPoint") isSpawn = true;
      for (const auto& comp : entity.components) {
        if (comp.contains("type") && (comp["type"] == "SpawnLocation" || comp["type"] == "SpawnPoint")) {
          isSpawn = true;
          break;
        }
      }
    }
    if (!isSpawn) continue;
    const auto& props = entity.properties;
    if (props.is_object() && props.contains("Enabled") && props["Enabled"].is_boolean() && !props["Enabled"].get<bool>()) continue;
    if (!any) any = &entity;
    const bool isNeutral = !props.is_object() || !props.contains("Neutral") || !props["Neutral"].is_boolean() || props["Neutral"].get<bool>();
    if (isNeutral) { if (!neutral) neutral = &entity; continue; }
    if (team && props.contains("TeamColor") && props["TeamColor"].is_array() && props["TeamColor"].size() >= 3 &&
        props["TeamColor"][0].is_number_integer() && props["TeamColor"][1].is_number_integer() && props["TeamColor"][2].is_number_integer()) {
      bool equal = true;
      for (std::size_t i = 0; i < 3; ++i) equal &= props["TeamColor"][i].get<int>() == (*team)[i];
      if (equal) { matching = &entity; break; }
    }
  }
  const auto* selected = matching ? matching : neutral ? neutral : any;
  if (!selected) return {};
  SpawnResolution result;
  result.position = selected->transform.position;
  result.position[1] += selected->transform.scale[1] * 0.5f + 3.0f;
  result.rotation = selected->transform.rotation;
  result.source = selected->id;
  return result;
}
}
