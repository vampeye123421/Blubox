#pragma once
#include <cstddef>
#include <cstdint>
namespace blubox::engine::network {
enum class Channel : std::uint8_t { ReliableOrdered=0, ReliableUnordered=1, UnreliableSequenced=2, Control=3 };
enum class PacketType : std::uint8_t { Hello=0, Welcome=1, Disconnect=2, RemoteEvent=3, RemoteFunctionRequest=4, RemoteFunctionResponse=5, Ping=9, Pong=10, PropertyDelta=11 };
inline constexpr std::size_t kHeaderSize=16, kMaxPayloadSize=16*1024, kMaxCredentialSize=2048, kMaxRemoteNameSize=128;
inline constexpr std::uint16_t kProtocolVersion=1;
}
