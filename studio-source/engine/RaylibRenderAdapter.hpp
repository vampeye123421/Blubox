#pragma once
#include "EngineCore.hpp"
namespace blubox::engine {
class RaylibRenderAdapter {
 public:
  void draw(const InstanceRegistry& registry) const;
};
}  // namespace blubox::engine
