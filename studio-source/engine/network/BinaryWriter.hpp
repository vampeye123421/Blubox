#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
namespace blubox::engine::network {
class BinaryWriter {
 public:
  explicit BinaryWriter(std::vector<std::byte>& out):out_(out){}
  void u8(std::uint8_t v){out_.push_back(std::byte(v));}
  void u16(std::uint16_t v){for(unsigned i=0;i<2;++i)u8(std::uint8_t(v>>(i*8)));}
  void u32(std::uint32_t v){for(unsigned i=0;i<4;++i)u8(std::uint8_t(v>>(i*8)));}
  void u64(std::uint64_t v){for(unsigned i=0;i<8;++i)u8(std::uint8_t(v>>(i*8)));}
  void f32(float v){u32(std::bit_cast<std::uint32_t>(v));}
  void f64(double v){u64(std::bit_cast<std::uint64_t>(v));}
  void bytes(std::span<const std::byte> v){out_.insert(out_.end(),v.begin(),v.end());}
  bool string(std::string_view s,std::size_t max){if(s.size()>max||s.size()>UINT16_MAX)return false;u16(static_cast<std::uint16_t>(s.size()));bytes({reinterpret_cast<const std::byte*>(s.data()),s.size()});return true;}
 private:std::vector<std::byte>&out_;
};
}
