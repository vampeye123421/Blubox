#pragma once
#include "PacketTypes.hpp"
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace blubox::engine::network {
using PlayerId=std::uint32_t;using RemoteId=std::uint16_t;
class RemoteRegistry {
 public:
  using EventHandler=std::function<void(PlayerId,std::span<const std::byte>)>;
  using FunctionHandler=std::function<std::vector<std::byte>(PlayerId,std::span<const std::byte>)>;
  RemoteId registerEvent(std::string name,EventHandler handler);
  RemoteId ensureEvent(std::string name);
  RemoteId ensureFunction(std::string name);
  RemoteId registerFunction(std::string name,FunctionHandler handler);
  bool setFunctionHandler(RemoteId id, FunctionHandler handler);
  std::optional<RemoteId> find(std::string_view name)const;
  bool dispatchEvent(PlayerId,RemoteId,std::span<const std::byte>)const;
  bool dispatchFunction(PlayerId,RemoteId,std::span<const std::byte>,std::vector<std::byte>&)const;
  std::optional<std::string_view> name(RemoteId)const;
 private:struct Entry{RemoteId id;std::string name;EventHandler event;FunctionHandler function;};
  std::unordered_map<RemoteId,Entry>entries_;std::unordered_map<std::string,RemoteId>byName_;
};
}
