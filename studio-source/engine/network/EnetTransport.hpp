#pragma once
#include <enet/enet.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
namespace blubox::engine::network {
class EnetTransport {
 public:
  // ENet global state is not thread-safe. All hosts in the process must
  // serialize calls, including event handling and packet destruction.
  static std::recursive_mutex& networkMutex() noexcept;
  EnetTransport()=default;~EnetTransport();EnetTransport(const EnetTransport&)=delete;EnetTransport&operator=(const EnetTransport&)=delete;
  bool startHost(std::uint16_t port,std::size_t peers,std::size_t channels=4);
  bool startHost(std::string_view bindAddress,std::uint16_t port,std::size_t peers,std::size_t channels=4);
  bool connect(std::string_view host,std::uint16_t port,std::size_t channels=4);
  int service(ENetEvent& event,std::uint32_t timeoutMs=0);
  bool send(ENetPeer*,std::uint8_t channel,std::span<const std::byte>,bool reliable);
  void flush();void shutdown();ENetHost* nativeHost()const noexcept{return host_;}
  std::uint16_t boundPort() const noexcept { return host_ ? host_->address.port : 0; }
 private:ENetHost*host_=nullptr;bool initialized_=false;
};
}
