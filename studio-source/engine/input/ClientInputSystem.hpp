#pragma once
#include "InputCommand.hpp"

namespace blubox::engine::input {
struct PhysicalInput {
  bool forward = false, backward = false, left = false, right = false;
  bool jump = false, sprint = false;
  float mouseDeltaX = 0, mouseDeltaY = 0, mouseWheel = 0;
};
class ClientInputSystem {
 public:
  InputCommand createCommand(const PhysicalInput& input, float yaw, float pitch);
 private:
  std::uint32_t tick_ = 0;
};
}
