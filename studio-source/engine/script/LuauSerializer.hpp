#pragma once
#include <cstddef>
#include <span>
#include <string>
#include <vector>
#include "../EngineCore.hpp"
struct lua_State;
namespace blubox::engine::script {
class LuauSerializer {
 public:
  static constexpr int kMaxArguments = 32;
  static constexpr int kMaxDepth = 8;
  static constexpr std::size_t kMaxString = 8192;
  static constexpr std::size_t kMaxPayload = 16 * 1024;
  static bool encode(lua_State*, int first, int count, std::vector<std::byte>&, std::string& error, InstanceRegistry* registry = nullptr);
  static bool decode(lua_State*, std::span<const std::byte>, int& count, std::string& error, InstanceRegistry* registry = nullptr);
};
}
