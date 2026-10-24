#include "RemoteManager.hpp"
#include "BinaryReader.hpp"
#include "BinaryWriter.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <random>

namespace blubox::engine::network {
namespace {std::uint64_t randomToken(){std::random_device rd;std::uint64_t v=(std::uint64_t(rd())<<32)^rd();return v?v:1;}std::span<const std::byte> asBytes(const ENetPacket*p){return {reinterpret_cast<const std::byte*>(p->data),p->dataLength};}}
bool RemoteManager::startHost(std::uint16_t port,std::size_t peers,Authenticate auth){return startHost("0.0.0.0",port,peers,std::move(auth));}
bool RemoteManager::startHost(std::string_view bindAddress,std::uint16_t port,std::size_t peers,Authenticate auth){shutdown();server_=true;authenticate_=std::move(auth);return authenticate_&&transport_.startHost(bindAddress,port,peers,4);}
bool RemoteManager::connect(std::string_view host,std::uint16_t port,std::string credential){shutdown();server_=false;credential_=std::move(credential);return !credential_.empty()&&credential_.size()<=kMaxCredentialSize&&transport_.connect(host,port,4);}
RemoteManager::EventSinkToken RemoteManager::addEventSink(EventSink sink){if(!sink)return 0;const auto token=nextEventSinkToken_++;additionalEventSinks_.emplace(token,std::move(sink));return token;}
void RemoteManager::removeEventSink(EventSinkToken token){if(token)additionalEventSinks_.erase(token);}
void RemoteManager::dispatchEvent(PlayerId player,std::string_view name,std::span<const std::byte> bytes,RemoteId id){bool handled=false;if(eventSink_){eventSink_(player,name,bytes);handled=true;}for(auto& [_,sink]:additionalEventSinks_)if(sink){sink(player,name,bytes);handled=true;}if(!handled)registry_.dispatchEvent(player,id,bytes);}
void RemoteManager::processQueuedFunctions(std::size_t maximum){
  std::size_t processed=0;
  while(!pendingFunctions_.empty()&&processed++<maximum){
    auto request=std::move(pendingFunctions_.front());pendingFunctions_.pop_front();
    std::vector<std::byte> result;const bool ok=registry_.dispatchFunction(request.player,request.id,request.payload,result)&&result.size()<=kMaxPayloadSize-32;
    std::vector<std::byte> reply;BinaryWriter writer(reply);writer.u64(request.request);writer.u8(ok?1:0);if(ok)writer.bytes(result);
    if(sessions_.contains(request.peer))sendPacket(request.peer,Channel::ReliableOrdered,PacketType::RemoteFunctionResponse,reply);
  }
}
void RemoteManager::poll(std::uint32_t timeout){
  // Server and local client have separate ENet hosts, but ENet's process-wide
  // state is not thread-safe. Keep service, callback handling and packet free
  // in one serialized critical section across every host in this process.
  std::lock_guard lock(EnetTransport::networkMutex());
  if (!transport_.nativeHost()) return;
  functions_.expire();
  ENetEvent event{};
  int result = transport_.service(event, timeout);
  while (result > 0) {
    switch (event.type) {
      case ENET_EVENT_TYPE_CONNECT:
        if (server_) {
          connected(event.peer);
        } else {
          serverPeer_ = event.peer;
          sessions_[event.peer] = Session{};
          std::vector<std::byte> hello;
          BinaryWriter writer(hello);
          writer.u16(kProtocolVersion);
          writer.string(credential_, kMaxCredentialSize);
          sendPacket(event.peer, Channel::Control, PacketType::Hello, hello, false);
        }
        break;
      case ENET_EVENT_TYPE_RECEIVE:
        receive(event.peer, event.packet);
        enet_packet_destroy(event.packet);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        disconnected(event.peer, event.data);
        break;
      default:
        break;
    }
    result = transport_.service(event, 0);
  }
  transport_.flush();
}
void RemoteManager::connected(ENetPeer*p){sessions_[p]=Session{};}
void RemoteManager::disconnected(ENetPeer*p,std::uint32_t data){
  lastDiagnostic_ = "ENet peer disconnected (data=" + std::to_string(data) + ").";
  if(log_)log_(lastDiagnostic_);
  auto i=sessions_.find(p);if(i!=sessions_.end()){if(i->second.player)peers_.erase(i->second.player);sessions_.erase(i);}if(p==serverPeer_){serverPeer_=nullptr;localAuthenticated_=false;localPlayer_=0;localToken_=0;functions_.cancelAll();}
}
void RemoteManager::shutdown(){functions_.cancelAll();pendingFunctions_.clear();transport_.shutdown();sessions_.clear();peers_.clear();serverPeer_=nullptr;localAuthenticated_=false;localPlayer_=0;localToken_=0;clientSequence_=1;}
void RemoteManager::disconnect(){std::lock_guard lock(EnetTransport::networkMutex());if(serverPeer_){enet_peer_disconnect_later(serverPeer_,0);transport_.flush();}}
bool RemoteManager::sendPacket(ENetPeer*p,Channel channel,PacketType type,std::span<const std::byte>body,bool withSession){if(!p||body.size()>kMaxPayloadSize-8)return false;std::vector<std::byte>payload;payload.reserve(body.size()+8);if(withSession){std::uint64_t token=server_?sessions_[p].token:localToken_;if(!token)return false;BinaryWriter sw(payload);sw.u64(token);}payload.insert(payload.end(),body.begin(),body.end());if(payload.size()>kMaxPayloadSize)return false;
  std::uint32_t seq=server_?sessions_[p].sendSequence++:clientSequence_++;std::vector<std::byte>wire;wire.reserve(kHeaderSize+payload.size());BinaryWriter w(wire);w.u8(static_cast<std::uint8_t>(type));w.u8(0);w.u16(static_cast<std::uint16_t>(payload.size()));w.u32(seq);w.u64(unixTimeMs());w.bytes(payload);
  bool reliable=channel!=Channel::UnreliableSequenced;return transport_.send(p,static_cast<std::uint8_t>(channel),wire,reliable);
}
void RemoteManager::receive(ENetPeer*p,ENetPacket*packet){if(!packet||packet->dataLength<kHeaderSize||packet->dataLength>kHeaderSize+kMaxPayloadSize){reject(p,"invalid packet size");return;}auto wire=asBytes(packet);BinaryReader r(wire);std::uint8_t typeByte,flags;std::uint16_t len;std::uint32_t seq;std::uint64_t timestamp;if(!r.u8(typeByte)||!r.u8(flags)||!r.u16(len)||!r.u32(seq)||!r.u64(timestamp)||len!=r.remaining()||len>kMaxPayloadSize){reject(p,"malformed packet header");return;}auto type=static_cast<PacketType>(typeByte);if(typeByte>static_cast<std::uint8_t>(PacketType::PropertyDelta)){reject(p,"unsupported packet type");return;}auto i=sessions_.find(p);if(i==sessions_.end()){reject(p,"unknown peer");return;}auto&s=i->second;auto payload=wire.subspan(kHeaderSize);
  if(server_&&!s.authenticated){if(type!=PacketType::Hello){reject(p,"hello required");return;}BinaryReader hello(payload);std::uint16_t version;std::string_view credential;if(!hello.u16(version)||version!=kProtocolVersion||!hello.string(credential,kMaxCredentialSize)||hello.remaining()!=0){reject(p,"invalid hello");return;}auto player=authenticate_(credential);if(!player||!*player){reject(p,"authentication failed");return;}s.player=*player;s.token=randomToken();s.authenticated=true;peers_[s.player]=p;std::vector<std::byte>body;BinaryWriter w(body);w.u32(s.player);w.u64(s.token);sendPacket(p,Channel::Control,PacketType::Welcome,body,false);return;}
  if(!server_&&p==serverPeer_&&!localAuthenticated_){if(type!=PacketType::Welcome){reject(p,"welcome required");return;}BinaryReader welcome(payload);if(!welcome.u32(localPlayer_)||!welcome.u64(localToken_)||welcome.remaining()!=0||!localPlayer_||!localToken_){reject(p,"invalid welcome");return;}s.player=localPlayer_;s.token=localToken_;s.authenticated=true;localAuthenticated_=true;return;}
  if(!s.authenticated){reject(p,"unauthenticated peer");return;}std::uint64_t token;if(payload.size()<8){reject(p,"missing session token");return;}BinaryReader tr(payload);if(!tr.u64(token)||token!=s.token){reject(p,"invalid session token");return;}if(!s.replay.accept(seq,timestamp,unixTimeMs())){reject(p,"replay or stale packet");return;}auto body=payload.subspan(8);bool rateAllowed=false;if(server_&&type==PacketType::RemoteEvent){BinaryReader eventHeader(body);std::uint16_t eventId;std::string_view eventName;if(!eventHeader.u16(eventId)||!eventHeader.string(eventName,kMaxRemoteNameSize)){reject(p,"malformed remote event");return;}rateAllowed=eventName=="__BluboxInput"?limiter_.allowInput(s.player):limiter_.allow(s.player,type);}else rateAllowed=limiter_.allow(s.player,type);if(!rateAllowed){reject(p,"rate limit exceeded");return;}BinaryReader b(body);
  if(server_){
    if(type==PacketType::RemoteEvent){std::uint16_t id;std::string_view wireName;std::span<const std::byte>data;if(!b.u16(id)||!b.string(wireName,kMaxRemoteNameSize)||!b.bytes(b.remaining(),data)){reject(p,"malformed remote event");return;}auto known=registry_.name(id);auto mapped=registry_.ensureEvent(std::string(wireName));if(mapped!=id||(known&&*known!=wireName)){reject(p,"RemoteEvent id/name mismatch");return;}dispatchEvent(s.player,wireName,data,id);}
    else if(type==PacketType::RemoteFunctionResponse){std::uint64_t req;std::uint8_t ok;std::span<const std::byte>data;if(!b.u64(req)||!b.u8(ok)||!b.bytes(b.remaining(),data)){reject(p,"malformed function response");return;}functions_.complete(req,ok!=0,data);}
    else if(type==PacketType::RemoteFunctionRequest){std::uint64_t req;std::uint16_t id;std::span<const std::byte>data;if(!b.u64(req)||!b.u16(id)||!b.bytes(b.remaining(),data)){reject(p,"malformed function request");return;}if(pendingFunctions_.size()>=256){reject(p,"remote function queue full");return;}pendingFunctions_.push_back(PendingFunction{p,s.player,req,id,{data.begin(),data.end()}});}
    else if(type==PacketType::Ping){sendPacket(p,Channel::Control,PacketType::Pong,body);}
    else if(type==PacketType::PropertyDelta){reject(p,"clients cannot send authoritative property deltas");}
    else reject(p,"packet type not valid from client");
  }else{
    if(type==PacketType::RemoteEvent){std::uint16_t id;std::string_view wireName;std::span<const std::byte>data;if(!b.u16(id)||!b.string(wireName,kMaxRemoteNameSize)||!b.bytes(b.remaining(),data)){reject(p,"malformed remote event");return;}auto known=registry_.name(id);auto mapped=registry_.ensureEvent(std::string(wireName));if(mapped!=id||(known&&*known!=wireName)){reject(p,"RemoteEvent id/name mismatch");return;}dispatchEvent(s.player,wireName,data,id);}
    else if(type==PacketType::RemoteFunctionRequest){std::uint64_t req;std::uint16_t id;std::span<const std::byte>data;if(!b.u64(req)||!b.u16(id)||!b.bytes(b.remaining(),data)){reject(p,"malformed function request");return;}std::vector<std::byte>result;bool ok=registry_.dispatchFunction(s.player,id,data,result);std::vector<std::byte>reply;BinaryWriter w(reply);w.u64(req);w.u8(ok?1:0);w.bytes(result);sendPacket(p,Channel::ReliableOrdered,PacketType::RemoteFunctionResponse,reply);}
    else if(type==PacketType::RemoteFunctionResponse){std::uint64_t req;std::uint8_t ok;std::span<const std::byte>data;if(!b.u64(req)||!b.u8(ok)||!b.bytes(b.remaining(),data)){reject(p,"malformed function response");return;}functions_.complete(req,ok!=0,data);}
    else if(type==PacketType::Pong){}
    else if(type==PacketType::PropertyDelta){if(propertyDeltaSink_)propertyDeltaSink_(body);}
    else reject(p,"packet type not valid from server");
  }
}
bool RemoteManager::sendRemoteEvent(PlayerId player,RemoteId id,std::span<const std::byte>data,Channel channel){auto name=registry_.name(id);if(!name||data.size()>kMaxPayloadSize-kMaxRemoteNameSize-12)return false;ENetPeer*p=nullptr;if(server_){auto i=peers_.find(player);if(i==peers_.end())return false;p=i->second;}else{if(!localAuthenticated_)return false;p=serverPeer_;}std::vector<std::byte>b;BinaryWriter w(b);w.u16(id);if(!w.string(*name,kMaxRemoteNameSize))return false;w.bytes(data);return sendPacket(p,channel,PacketType::RemoteEvent,b);}
bool RemoteManager::broadcastRemoteEvent(RemoteId id,std::span<const std::byte>data,Channel channel){if(!server_)return false;bool ok=true;for(auto&[player,_]:peers_)ok=sendRemoteEvent(player,id,data,channel)&&ok;return ok;}
bool RemoteManager::broadcastPropertyDelta(std::span<const std::byte> bytes){if(!server_||bytes.size()>kMaxPayloadSize-8)return false;bool ok=true;for(auto&[_,peer]:peers_)ok=sendPacket(peer,Channel::UnreliableSequenced,PacketType::PropertyDelta,bytes)&&ok;return ok;}
void RemoteManager::bindLegacy(blubox::engine::RemoteManager& manager){legacy_=&manager;manager.setTransport([this](const blubox::engine::RemoteMessage&m){auto id=registry_.ensureEvent(m.name);if(!id)return;auto bytes=std::span<const std::byte>(reinterpret_cast<const std::byte*>(m.payload.data()),m.payload.size());if(server_){if(m.recipient)sendRemoteEvent(m.recipient,id,bytes);else broadcastRemoteEvent(id,bytes);}else sendRemoteEvent(0,id,bytes);});setEventSink([this](PlayerId player,std::string_view name,std::span<const std::byte>bytes){if(legacy_&&server_){blubox::engine::RemoteMessage message;message.id=++legacyReceiveIds_[player];message.sender=player;message.name=std::string(name);message.payload.resize(bytes.size());if(!bytes.empty())std::memcpy(message.payload.data(),bytes.data(),bytes.size());std::string error;if(!legacy_->receiveEvent(std::move(message),error)&&log_)log_(error);} });}
std::uint64_t RemoteManager::invokeServer(std::string_view name,std::span<const std::byte>data,RemoteFunctionManager::Completion cb){if(server_||!localAuthenticated_||data.size()>kMaxPayloadSize-24)return 0;auto id=registry_.find(name);if(!id)return 0;std::vector<std::byte>b;BinaryWriter w(b);auto req=functions_.begin(localPlayer_,*id,std::move(cb));if(!req)return 0;w.u64(req);w.u16(*id);w.bytes(data);if(!sendPacket(serverPeer_,Channel::ReliableOrdered,PacketType::RemoteFunctionRequest,b)){functions_.complete(req,false,{});return 0;}return req;}
std::uint64_t RemoteManager::invokeClient(PlayerId player,std::string_view name,std::span<const std::byte>data,RemoteFunctionManager::Completion cb){if(!server_||data.size()>kMaxPayloadSize-24)return 0;auto peer=peers_.find(player);auto id=registry_.find(name);if(peer==peers_.end()||!id)return 0;std::vector<std::byte>b;BinaryWriter w(b);auto req=functions_.begin(player,*id,std::move(cb));if(!req)return 0;w.u64(req);w.u16(*id);w.bytes(data);if(!sendPacket(peer->second,Channel::ReliableOrdered,PacketType::RemoteFunctionRequest,b)){functions_.complete(req,false,{});return 0;}return req;}
void RemoteManager::reject(ENetPeer*p,std::string_view why){lastDiagnostic_=std::string(why);if(log_)log_(lastDiagnostic_);if(p){std::lock_guard lock(EnetTransport::networkMutex());enet_peer_disconnect_later(p,1);}}
}


