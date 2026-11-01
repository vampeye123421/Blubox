#pragma once

#include "UiLayoutResolver.hpp"
#include "../EngineCore.hpp"

namespace blubox::engine::ui {

class UiRuntimeBridge {
 public:
  UiDrawList resolve(const InstanceRegistry& instances, Vec2 viewport);

 private:
  UiScene scene_;
  UiLayoutResolver resolver_;
};

}  // namespace blubox::engine::ui
