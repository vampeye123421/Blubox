#include "LuauSerializer.hpp"
#include "../LuaInstanceBinding.hpp"
#include "../network/BinaryReader.hpp"
#include "../network/BinaryWriter.hpp"
#include <lua.h>
#include <cmath>

namespace blubox::engine::script {
namespace {
enum Type : std::uint8_t { Nil, Boolean, Number, String, Array, Vector3, Color3, CFrame, Instance };
bool writeValue(lua_State* L, int index, network::BinaryWriter& w, std::vector<std::byte>& out, int depth, std::string& error, InstanceRegistry* registry) {
  if (depth > LuauSerializer::kMaxDepth) { error="Remote argument nesting exceeds the depth limit"; return false; }
  switch (lua_type(L,index)) {
    case LUA_TNIL: w.u8(Nil); return true;
    case LUA_TBOOLEAN: w.u8(Boolean); w.u8(lua_toboolean(L,index)?1:0); return true;
    case LUA_TNUMBER: {
      const double n=lua_tonumber(L,index); if(!std::isfinite(n)){error="Remote numbers must be finite";return false;}
      w.u8(Number);w.f64(n);return true;
    }
    case LUA_TSTRING: {
      std::size_t n=0;const char* s=lua_tolstring(L,index,&n);if(n>LuauSerializer::kMaxString){error="Remote string exceeds 8 KiB";return false;}
      w.u8(String);if(!w.string({s,n},LuauSerializer::kMaxString)){error="Invalid remote string";return false;}return true;
    }
    case LUA_TTABLE: {
      index=lua_absindex(L,index);
      lua_getfield(L,index,"position");const bool hasPosition=lua_istable(L,-1);const int positionIndex=lua_gettop(L);
      lua_getfield(L,index,"rotation");const bool hasRotation=lua_istable(L,-1);const int rotationIndex=lua_gettop(L);
      if(hasPosition&&hasRotation){w.u8(CFrame);const bool p=writeValue(L,positionIndex,w,out,depth+1,error,registry);const bool r=p&&writeValue(L,rotationIndex,w,out,depth+1,error,registry);lua_pop(L,2);return r;}
      lua_pop(L,2);
      const char* xyz[]={"x","y","z"}; float vec[3]{}; bool isVector=true;
      for(int i=0;i<3;++i){lua_getfield(L,index,xyz[i]);if(!lua_isnumber(L,-1))isVector=false;else vec[i]=static_cast<float>(lua_tonumber(L,-1));lua_pop(L,1);}
      if(isVector){w.u8(Vector3);for(float n:vec){if(!std::isfinite(n)){error="Vector3 values must be finite";return false;}w.f32(n);}return true;}
      const char* rgb[]={"r","g","b"};float color[3]{};bool isColor=true;
      for(int i=0;i<3;++i){lua_getfield(L,index,rgb[i]);if(!lua_isnumber(L,-1))isColor=false;else color[i]=static_cast<float>(lua_tonumber(L,-1));lua_pop(L,1);}
      if(isColor){w.u8(Color3);for(float n:color){if(!std::isfinite(n)){error="Color3 values must be finite";return false;}w.f32(n);}return true;}
      const std::size_t length=lua_objlen(L,index);if(length>LuauSerializer::kMaxArguments){error="Remote arrays are limited to 32 items";return false;}
      lua_pushnil(L);
      while(lua_next(L,index)!=0){
        const bool valid=lua_isnumber(L,-2) && std::isfinite(lua_tonumber(L,-2)) &&
          lua_tonumber(L,-2)>=1 && lua_tonumber(L,-2)<=static_cast<double>(length) &&
          std::floor(lua_tonumber(L,-2))==lua_tonumber(L,-2);
        lua_pop(L,1);
        if(!valid){lua_pop(L,1);error="Remote tables must be arrays or supported value records";return false;}
      }
      for(std::size_t i=1;i<=length;++i){lua_rawgeti(L,index,static_cast<int>(i));const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(!present){error="Remote arrays cannot contain holes";return false;}}
      w.u8(Array);w.u8(static_cast<std::uint8_t>(length));
      for(std::size_t i=1;i<=length;++i){lua_rawgeti(L,index,static_cast<int>(i));const bool ok=writeValue(L,-1,w,out,depth+1,error,registry);lua_pop(L,1);if(!ok)return false;if(out.size()>LuauSerializer::kMaxPayload){error="Remote payload exceeds 16 KiB";return false;}}
      return true;
    }
    case LUA_TUSERDATA: {
      InstanceHandle handle;
      if(registry&&LuaInstanceBinding::read(L,index,handle)){w.u8(Instance);w.u32(handle.id);w.u32(handle.generation);return true;}
      error="Remote userdata is not a valid Instance reference";return false;
    }
    default: error="Remote argument type is unsupported"; return false;
  }
}
bool readValue(lua_State* L, network::BinaryReader& r, int depth, std::string& error, InstanceRegistry* registry) {
  if(depth>LuauSerializer::kMaxDepth){error="Remote argument nesting exceeds the depth limit";return false;}
  std::uint8_t type=0;if(!r.u8(type)){error="Truncated remote payload";return false;}
  switch(type){
    case Nil:lua_pushnil(L);return true;
    case Boolean:{std::uint8_t v;if(!r.u8(v)||v>1){error="Invalid remote boolean";return false;}lua_pushboolean(L,v);return true;}
    case Number:{double v;if(!r.f64(v)||!std::isfinite(v)){error="Invalid remote number";return false;}lua_pushnumber(L,v);return true;}
    case String:{std::string_view v;if(!r.string(v,LuauSerializer::kMaxString)){error="Invalid remote string";return false;}lua_pushlstring(L,v.data(),v.size());return true;}
    case Vector3:case Color3:{float v[3];for(float& n:v)if(!r.f32(n)||!std::isfinite(n)){error="Invalid remote vector/color";return false;}lua_createtable(L,0,3);const char* keys=type==Vector3?"xyz":"rgb";for(int i=0;i<3;++i){lua_pushnumber(L,v[i]);lua_setfield(L,-2,std::string(1,keys[i]).c_str());}return true;}
    case Array:{std::uint8_t n;if(!r.u8(n)||n>LuauSerializer::kMaxArguments){error="Invalid remote array length";return false;}lua_createtable(L,n,0);for(int i=1;i<=n;++i){if(!readValue(L,r,depth+1,error,registry))return false;lua_rawseti(L,-2,i);}return true;}
    case CFrame:{if(!readValue(L,r,depth+1,error,registry))return false;const int pos=lua_gettop(L);if(!readValue(L,r,depth+1,error,registry)){lua_pop(L,1);return false;}lua_createtable(L,0,2);lua_pushvalue(L,pos);lua_setfield(L,-2,"position");lua_pushvalue(L,-2);lua_setfield(L,-2,"rotation");lua_remove(L,pos);lua_remove(L,-2);return true;}
    case Instance:{std::uint32_t id=0,generation=0;if(!r.u32(id)||!r.u32(generation)||!registry){error="Invalid or unavailable Instance reference";return false;}InstanceHandle h{id,generation};if(!registry->alive(h)){error="Remote Instance reference is stale";return false;}LuaInstanceBinding::push(L,h);return true;}
    default:error="Unknown remote wire value";return false;
  }
}
}
bool LuauSerializer::encode(lua_State* L,int first,int count,std::vector<std::byte>& out,std::string& error,InstanceRegistry* registry){
  if(count<0||count>kMaxArguments){error="Remote calls are limited to 32 arguments";return false;}out.clear();network::BinaryWriter w(out);w.u8(1);w.u8(static_cast<std::uint8_t>(count));
  for(int i=0;i<count;++i)if(!writeValue(L,first+i,w,out,0,error,registry))return false;
  if(out.size()>kMaxPayload){error="Remote payload exceeds 16 KiB";return false;}error.clear();return true;
}
bool LuauSerializer::decode(lua_State* L,std::span<const std::byte> bytes,int& count,std::string& error,InstanceRegistry* registry){
  if(bytes.size()>kMaxPayload){error="Remote payload exceeds 16 KiB";return false;}network::BinaryReader r(bytes);std::uint8_t version=0,n=0;if(!r.u8(version)||version!=1||!r.u8(n)||n>kMaxArguments){error="Invalid remote payload header";return false;}
  count=n;for(int i=0;i<count;++i)if(!readValue(L,r,0,error,registry))return false;if(r.remaining()){error="Trailing bytes in remote payload";return false;}error.clear();return true;
}
}
