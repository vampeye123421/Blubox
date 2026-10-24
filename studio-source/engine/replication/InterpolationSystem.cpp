#include "InterpolationSystem.hpp"
#include "../network/BinaryReader.hpp"
#include <algorithm>
#include <vector>

namespace blubox::engine::replication {
namespace {
bool readQuantized(network::BinaryReader& reader, float& out, float minimum, float maximum) {
  std::uint16_t value = 0;
  if (!reader.u16(value)) return false;
  out = minimum + (static_cast<float>(value) / 65535.0f) * (maximum - minimum);
  return true;
}
}

void InterpolationSystem::receive(std::span<const std::byte> bytes, std::string& error) {
  network::BinaryReader reader(bytes);
  std::uint64_t timestamp = 0;
  std::uint16_t count = 0;
  if (!reader.u64(timestamp) || !reader.u16(count) || count > 1024) {
    error = "Invalid property delta header";
    return;
  }
  const double time = static_cast<double>(timestamp) / 1'000'000.0;
  std::vector<std::pair<NetworkId, TransformSnapshot>> pending;
  pending.reserve(count);
  for (std::uint16_t n=0; n<count; ++n) {
    NetworkId id=0;
    Generation generation=0;
    std::uint16_t mask=0;
    if (!reader.u32(id) || !id || !reader.u32(generation) || generation!=1 ||
        !reader.u16(mask) || (mask & ~std::uint16_t(7))) {
      error = "Invalid property delta entry";
      return;
    }
    TransformSnapshot snapshot{};
    snapshot.time = time;
    if (!readQuantized(reader,snapshot.transform.position[0],-4096.0f,4096.0f) ||
        !readQuantized(reader,snapshot.transform.position[1],-4096.0f,4096.0f) ||
        !readQuantized(reader,snapshot.transform.position[2],-4096.0f,4096.0f) ||
        !readQuantized(reader,snapshot.transform.rotation[0],-3.14159265f,3.14159265f) ||
        !readQuantized(reader,snapshot.transform.rotation[1],-3.14159265f,3.14159265f) ||
        !readQuantized(reader,snapshot.transform.rotation[2],-3.14159265f,3.14159265f) ||
        !readQuantized(reader,snapshot.transform.scale[0],0.0f,64.0f) ||
        !readQuantized(reader,snapshot.transform.scale[1],0.0f,64.0f) ||
        !readQuantized(reader,snapshot.transform.scale[2],0.0f,64.0f)) {
      error = "Truncated transform delta";
      return;
    }
    for (auto& channel : snapshot.color) if (!reader.u8(channel)) {
      error = "Truncated color delta";
      return;
    }
    for (float& value : snapshot.linearVelocity) {
      std::uint16_t raw=0;
      if (!reader.u16(raw)) { error = "Truncated velocity delta"; return; }
      const int signedValue = raw <= 32767u ? static_cast<int>(raw) : static_cast<int>(raw) - 65536;
      value = static_cast<float>(signedValue) / 256.0f;
    }
    pending.emplace_back(id, std::move(snapshot));
  }
  if (reader.remaining()) { error = "Trailing bytes in property delta"; return; }
  for (auto& [id, snapshot] : pending) snapshots_.push(id, std::move(snapshot));
  newestServerTime_ = std::max(newestServerTime_, time);
  error.clear();
}

void InterpolationSystem::tick(studio::MapDocument& document, double dt, NetworkId locallyPredicted) {
  if (newestServerTime_ <= 0) return;
  renderClock_ = std::min(newestServerTime_, std::max(renderClock_ + std::clamp(dt,0.0,0.1),
                                                       newestServerTime_ - kInterpolationDelay));
  const double renderTime = std::min(renderClock_, newestServerTime_ - kInterpolationDelay);
  for (auto& entity : document.entities) {
    if (entity.kind=="block" || entity.kind=="sphere" || entity.kind=="wedgepart" || entity.kind=="cornerwedgepart") {
      const NetworkId networkId = networkIdForEntityId(entity.id);
      if (networkId != locallyPredicted) if (auto state=snapshots_.sample(networkId,renderTime)) {
        entity.transform=state->transform;
        entity.color=state->color;
      }
    }
  }
}
}
