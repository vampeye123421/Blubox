#pragma once
#include "PropertyDeltaEncoder.hpp"
#include <array>
#include <deque>
#include <optional>
#include <unordered_map>

namespace blubox::engine::replication {
struct TransformSnapshot {
  double time = 0;
  studio::Transform transform{};
  std::array<unsigned char,4> color{};
  std::array<float, 3> linearVelocity{};
};
class SnapshotBuffer {
 public:
  void push(NetworkId id, TransformSnapshot snapshot);
  std::optional<TransformSnapshot> sample(NetworkId id, double renderTime) const;
 private:
  std::unordered_map<NetworkId, std::deque<TransformSnapshot>> snapshots_;
};
}
