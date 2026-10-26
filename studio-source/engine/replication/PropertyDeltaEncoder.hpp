#pragma once
#include "ReplicationComponent.hpp"
#include "../network/PacketTypes.hpp"
#include "../../studio/MapDocument.hpp"
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>
#include <string>

namespace blubox::engine::replication {
class PropertyDeltaEncoder {
 public:
  // Stable IDs are derived from the immutable play snapshot's entity order.
  using VelocityMap = std::unordered_map<std::string, std::array<float, 3>>;
  std::vector<std::vector<std::byte>> encode(const studio::MapDocument& document,
                                              const VelocityMap& velocities = {});
 private:
  struct State { studio::Transform transform{}; std::array<unsigned char,4> color{};
                 std::array<float, 3> velocity{}; };
  std::unordered_map<NetworkId, State> lastSent_;
};
}
