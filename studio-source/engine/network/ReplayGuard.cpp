#include "ReplayGuard.hpp"
#include <cstdlib>
namespace blubox::engine::network {
std::uint64_t unixTimeMs()noexcept{return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());}
bool ReplayGuard::accept(std::uint32_t seq,std::uint64_t ts,std::uint64_t now)noexcept{
  constexpr std::uint64_t skew=5000;
  if(seq==0||seq<=lastSequence_||ts>now+skew||now>ts+skew||ts<lastTimestamp_)return false;
  lastSequence_=seq;lastTimestamp_=ts;return true;
}
}
