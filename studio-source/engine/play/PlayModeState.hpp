#pragma once
#include <cstdint>
namespace blubox::engine::play {
enum class PlayModeState : std::uint8_t { Stopped, Starting, Running, Paused, Stopping, Failed };
}
