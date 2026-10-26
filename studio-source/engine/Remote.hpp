#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace blubox::engine {
using PlayerId = std::uint64_t;
struct RemoteMessage {
  std::uint64_t id = 0;
  PlayerId sender = 0;
  PlayerId recipient = 0;
  std::string name;
  std::vector<std::uint8_t> payload;
};
struct RemotePolicy {
  std::size_t maxPayload = 64 * 1024;
  double requestsPerSecond = 20;
  double burst = 40;
};
class RemoteManager {
 public:
  using Validator = std::function<bool(PlayerId, const RemoteMessage&, std::string&)>;
  using EventHandler = std::function<void(const RemoteMessage&)>;
  using ReplyHandler = std::function<std::vector<std::uint8_t>(PlayerId, const RemoteMessage&, std::string&)>;
  explicit RemoteManager(bool serverAuthority = true) : server_(serverAuthority) {}
  void setPolicy(RemotePolicy p) {
    p.maxPayload = std::clamp<std::size_t>(p.maxPayload, 1, 1024 * 1024);
    p.requestsPerSecond = std::clamp(p.requestsPerSecond, 0.1, 10000.0);
    p.burst = std::clamp(p.burst, 1.0, 10000.0);
    policy_ = p;
  }
  void onEvent(std::string name, Validator validate, EventHandler handler);
  void onFunction(std::string name, Validator validate, ReplyHandler handler);
  bool receiveEvent(RemoteMessage message, std::string& error);
  bool receiveFunction(RemoteMessage message, std::vector<std::uint8_t>& reply, std::string& error);
  void setTransport(std::function<void(const RemoteMessage&)> sender) { send_ = std::move(sender); }
  bool fireServer(RemoteMessage message);
  bool fireClient(PlayerId player, RemoteMessage message);

 private:
  struct Route {
    Validator validate;
    EventHandler event;
    ReplyHandler function;
  };
  struct Bucket {
    double tokens = -1;
    std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
  };
  bool authorize(const RemoteMessage&, const Route&, std::string&);
  bool consume(PlayerId, std::string&);
  bool server_;
  RemotePolicy policy_{};
  std::uint64_t nextId_ = 1;
  std::unordered_map<std::string, Route> routes_;
  std::unordered_map<PlayerId, Bucket> buckets_;
  std::unordered_map<PlayerId, std::uint64_t> lastMessageIds_;
  std::function<void(const RemoteMessage&)> send_;
};
}  // namespace blubox::engine
