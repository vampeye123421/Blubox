#include "Remote.hpp"
#include <algorithm>
namespace blubox::engine {
void RemoteManager::onEvent(std::string n, Validator v, EventHandler h) {
  routes_[std::move(n)] = {std::move(v), std::move(h), {}};
}
void RemoteManager::onFunction(std::string n, Validator v, ReplyHandler h) {
  routes_[std::move(n)] = {std::move(v), {}, std::move(h)};
}
bool RemoteManager::consume(PlayerId id, std::string& e) {
  auto& b = buckets_[id];
  auto now = std::chrono::steady_clock::now();
  if (b.tokens < 0) b.tokens = policy_.burst;
  double dt = std::chrono::duration<double>(now - b.at).count();
  b.tokens = std::min(policy_.burst, b.tokens + dt * policy_.requestsPerSecond);
  b.at = now;
  if (b.tokens < 1) {
    e = "Remote rate limit exceeded.";
    return false;
  }
  b.tokens -= 1;
  return true;
}
bool RemoteManager::authorize(const RemoteMessage& m, const Route& r, std::string& e) {
  if (m.id == 0 || m.id <= lastMessageIds_[m.sender]) {
    e = "Remote message id is invalid or was already received.";
    return false;
  }
  if (m.payload.size() > policy_.maxPayload) {
    e = "Remote payload exceeds policy limit.";
    return false;
  }
  if (!consume(m.sender, e)) return false;
  if (!server_) {
    e = "Remote request receiver is not authoritative.";
    return false;
  }
  if (r.validate && !r.validate(m.sender, m, e)) return false;
  lastMessageIds_[m.sender] = m.id;
  return true;
}
bool RemoteManager::receiveEvent(RemoteMessage m, std::string& e) {
  auto i = routes_.find(m.name);
  if (i == routes_.end() || !i->second.event) {
    e = "Unknown RemoteEvent.";
    return false;
  }
  if (!authorize(m, i->second, e)) return false;
  i->second.event(m);
  return true;
}
bool RemoteManager::receiveFunction(RemoteMessage m, std::vector<std::uint8_t>& reply, std::string& e) {
  auto i = routes_.find(m.name);
  if (i == routes_.end() || !i->second.function) {
    e = "Unknown RemoteFunction.";
    return false;
  }
  if (!authorize(m, i->second, e)) return false;
  reply = i->second.function(m.sender, m, e);
  if (reply.size() > policy_.maxPayload) {
    reply.clear();
    e = "RemoteFunction reply exceeds policy limit.";
    return false;
  }
  return true;
}
bool RemoteManager::fireServer(RemoteMessage m) {
  if (!send_ || server_) return false;
  m.id = nextId_++;
  send_(m);
  return true;
}
bool RemoteManager::fireClient(PlayerId p, RemoteMessage m) {
  if (!send_ || !server_) return false;
  m.sender = 0;
  m.recipient = p;
  m.id = nextId_++;
  send_(m);
  return true;
}
}  // namespace blubox::engine
