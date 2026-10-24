#pragma once
#include "EngineCore.hpp"
#include "../studio/MapDocument.hpp"
#include <memory>
#include <string>
#include <unordered_map>
namespace blubox::engine {
class SceneRuntime {
 public:
  explicit SceneRuntime(studio::MapDocument& document);
  InstanceRegistry& instances() noexcept { return registry_; }
  const InstanceRegistry& instances() const noexcept { return registry_; }
  InstanceHandle find(std::string_view mapId) const;
  void syncToDocument();
  void syncFromDocument();

 private:
  static ClassId mapClass(const studio::Entity& e);
  studio::MapDocument& document_;
  InstanceRegistry registry_;
  std::unordered_map<std::string, InstanceHandle> handles_;
};
}  // namespace blubox::engine
