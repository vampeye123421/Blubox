#include "EnetTransport.hpp"
#include <algorithm>
#include <mutex>
#include <string>
namespace blubox::engine::network {
namespace {
std::recursive_mutex gEnetMutex;
std::size_t gEnetUsers = 0;

bool acquireEnet() {
  if (gEnetUsers == 0 && enet_initialize() < 0) return false;
  ++gEnetUsers;
  return true;
}

void releaseEnet() {
  if (gEnetUsers == 0) return;
  if (--gEnetUsers == 0) enet_deinitialize();
}
}
std::recursive_mutex& EnetTransport::networkMutex() noexcept { return gEnetMutex; }
EnetTransport::~EnetTransport(){shutdown();}
bool EnetTransport::startHost(std::uint16_t port,std::size_t peers,std::size_t channels){return startHost("0.0.0.0",port,peers,channels);}
bool EnetTransport::startHost(std::string_view bindAddress,std::uint16_t port,std::size_t peers,std::size_t channels){std::lock_guard lock(networkMutex());shutdown();if(!acquireEnet())return false;initialized_=true;ENetAddress a{};const std::string bind(bindAddress);if(enet_address_set_host(&a,bind.c_str())<0){shutdown();return false;}a.port=port;host_=enet_host_create(&a,peers,static_cast<enet_uint32>(std::clamp<std::size_t>(channels,1,255)),0,0);if(!host_)shutdown();return host_!=nullptr;}
bool EnetTransport::connect(std::string_view host,std::uint16_t port,std::size_t channels){std::lock_guard lock(networkMutex());shutdown();if(!acquireEnet())return false;initialized_=true;host_=enet_host_create(nullptr,1,static_cast<enet_uint32>(std::clamp<std::size_t>(channels,1,255)),0,0);if(!host_){shutdown();return false;}ENetAddress a{};std::string name(host);if(enet_address_set_host(&a,name.c_str())<0){shutdown();return false;}a.port=port;if(!enet_host_connect(host_,&a,static_cast<enet_uint32>(channels),0)){shutdown();return false;}return true;}
int EnetTransport::service(ENetEvent&e,std::uint32_t timeout){std::lock_guard lock(networkMutex());return host_?enet_host_service(host_,&e,timeout):-1;}
bool EnetTransport::send(ENetPeer*p,std::uint8_t channel,std::span<const std::byte>bytes,bool reliable){std::lock_guard lock(networkMutex());if(!p||!host_||bytes.size()>65535)return false;auto packet=enet_packet_create(bytes.data(),bytes.size(),reliable?ENET_PACKET_FLAG_RELIABLE:0);if(!packet)return false;if(enet_peer_send(p,channel,packet)<0){enet_packet_destroy(packet);return false;}return true;}
void EnetTransport::flush(){std::lock_guard lock(networkMutex());if(host_)enet_host_flush(host_);}
void EnetTransport::shutdown(){std::lock_guard lock(networkMutex());if(host_){enet_host_destroy(host_);host_=nullptr;}if(initialized_){releaseEnet();initialized_=false;}}
}
