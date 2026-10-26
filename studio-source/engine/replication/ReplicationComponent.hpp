#pragma once
#include <cstdint>
#include <string_view>
namespace blubox::engine::replication {
using NetworkId = std::uint32_t;
using Generation = std::uint32_t;
enum class Priority : std::uint8_t { Low, Normal, High, Critical };
struct ReplicationComponent {
  NetworkId networkId = 0;
  Generation generation = 1;
  std::uint32_t ownerPlayerId = 0;
  std::uint64_t lastChangedTick = 0;
  std::uint64_t lastSentTick = 0;
  std::uint16_t dirtyMask = 0;
  Priority priority = Priority::Normal;
};
inline NetworkId networkIdForEntityId(std::string_view entityId) noexcept {
  std::uint32_t hash = 2166136261u;
  for (const unsigned char byte : entityId) {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash ? hash : 1u;
}
}
