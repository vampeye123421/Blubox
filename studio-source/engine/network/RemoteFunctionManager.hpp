#pragma once
#include "RemoteRegistry.hpp"
#include <chrono>
#include <functional>
#include <unordered_map>
namespace blubox::engine::network {
class RemoteFunctionManager {
 public:
  using Completion=std::function<void(bool,std::vector<std::byte>)>;
  std::uint64_t begin(PlayerId,RemoteId,Completion,std::chrono::milliseconds timeout=std::chrono::seconds(5));
  bool complete(std::uint64_t,bool,std::span<const std::byte>);
  void expire();void cancelAll();
 private:struct Pending{PlayerId player;RemoteId remote;std::chrono::steady_clock::time_point deadline;Completion callback;};
  std::uint64_t next_=1;std::unordered_map<std::uint64_t,Pending>pending_;
};
}
