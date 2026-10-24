#include "ClientInputSystem.hpp"
#include <cmath>

namespace blubox::engine::input {
InputCommand ClientInputSystem::createCommand(const PhysicalInput& physical, float yaw, float pitch) {
  InputCommand command;
  command.tick = ++tick_;
  command.yaw = yaw;
  command.pitch = pitch;
  command.moveY = float(physical.forward) - float(physical.backward);
  command.moveX = float(physical.right) - float(physical.left);
  const float length = std::sqrt(command.moveX * command.moveX + command.moveY * command.moveY);
  if (length > 1.0f) { command.moveX /= length; command.moveY /= length; }
  if (physical.jump) command.buttons |= Jump;
  if (physical.sprint) command.buttons |= Sprint;
  command.mouseDeltaX = physical.mouseDeltaX;
  command.mouseDeltaY = physical.mouseDeltaY;
  command.mouseWheel = physical.mouseWheel;
  return command;
}
}
