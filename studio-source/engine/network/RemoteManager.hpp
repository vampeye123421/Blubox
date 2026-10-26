#pragma once
#include "EnetTransport.hpp"
#include "RateLimiter.hpp"
#include "RemoteFunctionManager.hpp"
#include "RemoteRegistry.hpp"
#include "ReplayGuard.hpp"
#include "../Remote.hpp"
#include <functional>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
namespace blubox::engine::network {
class RemoteManager {
 public:
  using Authenticate=std::function<std::optional<PlayerId>(std::string_view credential)>;
  using EventSink=std::function<void(PlayerId,std::string_view,std::span<const std::byte>)>;
  using PropertyDeltaSink=std::function<void(std::span<const std::byte>)>;
  using EventSinkToken=std::uint64_t;
  bool startHost(std::uint16_t port,std::size_t maxClients,Authenticate);
  bool startHost(std::string_view bindAddress,std::uint16_t port,std::size_t maxClients,Authenticate);
  bool connect(std::string_view host,std::uint16_t port,std::string credential);
  void poll(std::uint32_t timeoutMs=0);void shutdown();
  // Executes queued RemoteFunction handlers outside the ENet service callback.
  void processQueuedFunctions(std::size_t maximum = 64);
  void disconnect();
  [[nodiscard]] const std::string& lastDiagnostic() const noexcept { return lastDiagnostic_; }
  RemoteRegistry& registry()noexcept{return registry_;}RemoteFunctionManager& functions()noexcept{return functions_;}
  void setEventSink(EventSink sink){eventSink_=std::move(sink);}
  EventSinkToken addEventSink(EventSink sink);
  void removeEventSink(EventSinkToken token);
  void setPropertyDeltaSink(PropertyDeltaSink sink){propertyDeltaSink_=std::move(sink);}
  // Bridge the pre-existing validated RemoteManager API to ENet. The referenced
  // engine manager must outlive this binding and its callbacks.
  void bindLegacy(blubox::engine::RemoteManager& manager);
  bool sendRemoteEvent(PlayerId,RemoteId,std::span<const std::byte>,Channel=Channel::ReliableOrdered);
  bool broadcastRemoteEvent(RemoteId,std::span<const std::byte>,Channel=Channel::ReliableOrdered);
  bool broadcastPropertyDelta(std::span<const std::byte> bytes);
  std::uint64_t invokeServer(std::string_view,std::span<const std::byte>,RemoteFunctionManager::Completion);
  std::uint64_t invokeClient(PlayerId,std::string_view,std::span<const std::byte>,RemoteFunctionManager::Completion);
  std::uint32_t playerId()const noexcept{return localPlayer_;}bool authenticated()const noexcept{return localAuthenticated_;}
  std::uint16_t boundPort() const noexcept { return transport_.boundPort(); }
  std::size_t authenticatedPeerCount() const noexcept { return server_ ? peers_.size() : (localAuthenticated_ ? 1u : 0u); }
  void flush() { transport_.flush(); }
  void setLog(std::function<void(std::string_view)> log){log_=std::move(log);}
 private:
  struct Session{PlayerId player=0;bool authenticated=false;std::uint64_t token=0;std::uint32_t sendSequence=1;ReplayGuard replay;};
  void receive(ENetPeer*,ENetPacket*);void connected(ENetPeer*);void disconnected(ENetPeer*, std::uint32_t data);
  void dispatchEvent(PlayerId,std::string_view,std::span<const std::byte>,RemoteId);
  bool sendPacket(ENetPeer*,Channel,PacketType,std::span<const std::byte>,bool session=true);
  void reject(ENetPeer*,std::string_view);
  EnetTransport transport_;bool server_=false;std::string credential_;Authenticate authenticate_;EventSink eventSink_;PropertyDeltaSink propertyDeltaSink_;std::function<void(std::string_view)>log_;std::string lastDiagnostic_;
  blubox::engine::RemoteManager* legacy_=nullptr;std::unordered_map<PlayerId,std::uint64_t>legacyReceiveIds_;
  std::unordered_map<EventSinkToken,EventSink> additionalEventSinks_;EventSinkToken nextEventSinkToken_=1;
  struct PendingFunction { ENetPeer* peer=nullptr; PlayerId player=0; std::uint64_t request=0; RemoteId id=0; std::vector<std::byte> payload; };
  std::deque<PendingFunction> pendingFunctions_;
  std::unordered_map<ENetPeer*,Session>sessions_;std::unordered_map<PlayerId,ENetPeer*>peers_;RemoteRegistry registry_;RemoteFunctionManager functions_;RateLimiter limiter_;std::uint32_t localPlayer_=0;std::uint32_t clientSequence_=1;std::uint64_t localToken_=0;bool localAuthenticated_=false;ENetPeer*serverPeer_=nullptr;
};
}
