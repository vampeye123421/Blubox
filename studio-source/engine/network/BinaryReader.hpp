#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace blubox::engine::network {
enum class ReadError:std::uint8_t{None,Truncated,InvalidLength};
class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::byte> in):in_(in){}
  bool u8(std::uint8_t&v){if(remaining()<1)return fail();v=std::to_integer<std::uint8_t>(in_[pos_++]);return true;}
  bool u16(std::uint16_t&v){v=0;for(unsigned i=0;i<2;++i){std::uint8_t b;if(!u8(b))return false;v|=std::uint16_t(b)<<(i*8);}return true;}
  bool u32(std::uint32_t&v){v=0;for(unsigned i=0;i<4;++i){std::uint8_t b;if(!u8(b))return false;v|=std::uint32_t(b)<<(i*8);}return true;}
  bool u64(std::uint64_t&v){v=0;for(unsigned i=0;i<8;++i){std::uint8_t b;if(!u8(b))return false;v|=std::uint64_t(b)<<(i*8);}return true;}
  bool f32(float&v){std::uint32_t b;if(!u32(b))return false;v=std::bit_cast<float>(b);return true;}
  bool f64(double&v){std::uint64_t b;if(!u64(b))return false;v=std::bit_cast<double>(b);return true;}
  bool bytes(std::size_t n,std::span<const std::byte>&v){if(n>remaining()){error_=ReadError::InvalidLength;return false;}v=in_.subspan(pos_,n);pos_+=n;return true;}
  bool string(std::string_view&v,std::size_t max){std::uint16_t n;if(!u16(n))return false;if(n>max){error_=ReadError::InvalidLength;return false;}std::span<const std::byte>s;if(!bytes(n,s))return false;v={reinterpret_cast<const char*>(s.data()),s.size()};return true;}
  std::size_t remaining()const noexcept{return in_.size()-pos_;} ReadError error()const noexcept{return error_;}
 private:bool fail(){error_=ReadError::Truncated;return false;}std::span<const std::byte>in_;std::size_t pos_=0;ReadError error_=ReadError::None;
};
}
