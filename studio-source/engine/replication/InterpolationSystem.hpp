#pragma once
#include "SnapshotBuffer.hpp"
#include "../network/RemoteManager.hpp"
#include "../../studio/MapDocument.hpp"
#include <chrono>
#include <string>

namespace blubox::engine::replication {
class InterpolationSystem {
 public:
  void receive(std::span<const std::byte> packet, std::string& error);
  void tick(studio::MapDocument& document, double deltaSeconds, NetworkId locallyPredicted = 0);
 private:
  SnapshotBuffer snapshots_;
  double renderClock_ = 0;
  double newestServerTime_ = 0;
  static constexpr double kInterpolationDelay = 0.1;
};
}
