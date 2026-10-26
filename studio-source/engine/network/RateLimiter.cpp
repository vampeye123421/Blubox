#include "RateLimiter.hpp"
#include <algorithm>
namespace blubox::engine::network {
bool RateLimiter::allow(std::uint32_t player,PacketType type){
  double rate=20,burst=40;switch(type){case PacketType::RemoteFunctionRequest:rate=8;burst=12;break;case PacketType::RemoteEvent:rate=20;burst=30;break;case PacketType::Ping:rate=2;burst=4;break;default:rate=30;burst=40;break;}
  return allowBucket(player, static_cast<std::uint8_t>(type), rate, burst);
}
bool RateLimiter::allowInput(std::uint32_t player) {
  return allowBucket(player, 0xff, 120.0, 180.0);
}
bool RateLimiter::allowBucket(std::uint32_t player, std::uint8_t bucketId, double rate, double burst) {
  auto& b = buckets_[{player, bucketId}];
  const auto now = std::chrono::steady_clock::now();
  if (!b.initialized) { b.tokens = burst; b.initialized = true; }
  const double dt = std::chrono::duration<double>(now - b.at).count();
  b.tokens = std::min(burst, b.tokens + dt * rate);
  b.at = now;
  if (b.tokens < 1.0) return false;
  b.tokens -= 1.0;
  return true;
}
}
