#pragma once
#include <cstdint>
#include <chrono>
namespace blubox::engine::network {
class ReplayGuard {
 public: bool accept(std::uint32_t sequence,std::uint64_t timestampMs,std::uint64_t nowMs)noexcept;
 private:std::uint32_t lastSequence_=0;std::uint64_t lastTimestamp_=0;
};
std::uint64_t unixTimeMs() noexcept;
}
