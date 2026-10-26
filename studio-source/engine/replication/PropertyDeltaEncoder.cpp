#include "PropertyDeltaEncoder.hpp"
#include "../network/BinaryWriter.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace blubox::engine::replication {
namespace {
bool same(const studio::Transform& a, const studio::Transform& b) {
  return a.position == b.position && a.rotation == b.rotation && a.scale == b.scale;
}
void writeTransform(network::BinaryWriter& writer, const studio::Transform& t) {
  auto q=[](float v,float lo,float hi){if(!std::isfinite(v))v=0;const float n=std::clamp((v-lo)/(hi-lo),0.0f,1.0f);return static_cast<std::uint16_t>(std::lround(n*65535.0f));};
  for (float v : t.position) writer.u16(q(v,-4096.0f,4096.0f));
  for (float v : t.rotation) writer.u16(q(v,-3.14159265f,3.14159265f));
  for (float v : t.scale) writer.u16(q(v,0.0f,64.0f));
}
}
std::vector<std::vector<std::byte>> PropertyDeltaEncoder::encode(const studio::MapDocument& document,
                                                                  const VelocityMap& velocities) {
  struct Entry { NetworkId id; const studio::Entity* entity; std::uint16_t mask; };
  std::vector<Entry> changed;
  changed.reserve(document.entities.size());
  for (const auto& entity : document.entities) {
    const NetworkId id = networkIdForEntityId(entity.id);
    const bool replicable = entity.kind == "block" || entity.kind == "sphere" ||
      entity.kind == "wedgepart" || entity.kind == "cornerwedgepart";
    if (replicable) {
      auto i = lastSent_.find(id);
      std::uint16_t mask = 0;
      if (i == lastSent_.end() || !same(i->second.transform, entity.transform)) mask |= 1;
      if (i == lastSent_.end() || i->second.color != entity.color) mask |= 2;
      const auto velocityIt = velocities.find(entity.id);
      const std::array<float, 3> velocity = velocityIt == velocities.end()
          ? std::array<float, 3>{0.0f, 0.0f, 0.0f} : velocityIt->second;
      if (i == lastSent_.end() || i->second.velocity != velocity) mask |= 4;
      if (mask) changed.push_back({id, &entity, mask});
      lastSent_[id] = {entity.transform, entity.color, velocity};
    }
  }
  constexpr std::size_t kEntriesPerPacket=300;
  const auto timestamp=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
  std::vector<std::vector<std::byte>> packets;
  for(std::size_t start=0;start<changed.size();start+=kEntriesPerPacket){
    const auto end=std::min(changed.size(),start+kEntriesPerPacket);
    std::vector<std::byte> bytes;network::BinaryWriter writer(bytes);
    writer.u64(timestamp);writer.u16(static_cast<std::uint16_t>(end-start));
    for(std::size_t n=start;n<end;++n){const auto& item=changed[n];
      writer.u32(item.id);writer.u32(1);writer.u16(item.mask);
      // Send a complete transform/color value for changed entities to make
      // sparse property updates self-contained and robust to packet loss.
      writeTransform(writer, item.entity->transform);
      for (auto channel : item.entity->color) writer.u8(channel);
      const auto velocityIt = velocities.find(item.entity->id);
      const auto velocity = velocityIt == velocities.end()
          ? std::array<float, 3>{0.0f, 0.0f, 0.0f} : velocityIt->second;
      for (float value : velocity) {
        const float bounded = std::isfinite(value) ? std::clamp(value, -128.0f, 128.0f) : 0.0f;
        const auto quantized = static_cast<std::int16_t>(std::lround(bounded * 256.0f));
        writer.u16(static_cast<std::uint16_t>(quantized));
      }
    }
    packets.push_back(std::move(bytes));
  }
  return packets;
}
}
