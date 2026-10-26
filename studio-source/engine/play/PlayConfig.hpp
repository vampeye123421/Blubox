#pragma once
#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace blubox::engine::play {
enum class PlayModeType : std::uint8_t { Play, PlayHere, Run };
struct PlayConfig {
  bool useStandaloneClient = true;
  PlayModeType mode = PlayModeType::Play;
  std::array<float, 3> requestedSpawn{0.0f, 0.0f, 0.0f};
  std::array<float, 3> requestedForward{0.0f, 0.0f, -1.0f};
  std::string playerDisplayName = "Player";
  std::uint16_t serverPort = 0;
  std::size_t maxClients = 1;
  bool enableNetworking = true;
  std::chrono::milliseconds clientConnectTimeout{5000};
  std::chrono::milliseconds serverStartTimeout{5000};
  std::chrono::milliseconds fixedStep{16};
};
}
