#pragma once
#include "PacketTypes.hpp"
#include <chrono>
#include <cstdint>
#include <unordered_map>
namespace blubox::engine::network {
class RateLimiter {
 public:
  bool allow(std::uint32_t player, PacketType type);
  // Local playtest movement is sent at 60 Hz. Keep it independently bounded
  // so it cannot exhaust the ordinary user RemoteEvent budget.
  bool allowInput(std::uint32_t player);
 private:
  bool allowBucket(std::uint32_t player, std::uint8_t bucketId, double rate, double burst);
  struct Key{std::uint32_t player;std::uint8_t type;bool operator==(const Key&)const=default;};
  struct Hash{std::size_t operator()(Key k)const noexcept{return (std::size_t(k.player)<<8)^k.type;}};
  struct Bucket{double tokens=0;std::chrono::steady_clock::time_point at=std::chrono::steady_clock::now();bool initialized=false;};
  std::unordered_map<Key,Bucket,Hash>buckets_;
};
}
