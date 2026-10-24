#pragma once
#include "../EngineCore.hpp"
#include "../../studio/MapDocument.hpp"
#include <array>
#include <optional>

namespace blubox::engine::play {
struct SpawnResolution {
  std::array<float, 3> position{0.0f, 100.0f, 0.0f};
  std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
  std::string source;
};
struct SpawnLocation {
  InstanceHandle instance{};
  bool enabled = true;
  float duration = 0.0f;
  bool neutral = true;
  std::array<std::uint8_t, 4> teamColor{194, 194, 194, 255};
  bool allowTeamChangeOnTouch = false;
};
class SpawnService {
 public:
  // Empty team means no team has been assigned. Selection order is team match,
  // neutral, then any enabled location; absence falls back to (0,100,0).
  static SpawnResolution resolve(const studio::MapDocument& world,
                                std::optional<std::array<std::uint8_t, 4>> team = std::nullopt);
};
}
