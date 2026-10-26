#pragma once
#include <cstdint>

namespace blubox::engine::input {
enum InputButton : std::uint8_t { Jump = 1u << 0, Sprint = 1u << 1 };
struct InputCommand {
  std::uint32_t tick = 0;
  std::uint8_t buttons = 0;
  float yaw = 0.0f, pitch = 0.0f;
  float moveX = 0.0f, moveY = 0.0f;
  float mouseDeltaX = 0.0f, mouseDeltaY = 0.0f, mouseWheel = 0.0f;
};
}
