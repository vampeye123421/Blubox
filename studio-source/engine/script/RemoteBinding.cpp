#include "RemoteBinding.hpp"
#include "LuauSerializer.hpp"
#include <lua.h>
#include <lualib.h>
#include <algorithm>
#include <cmath>
#include <new>

namespace blubox::engine::script {
namespace {
constexpr int kRemoteTag=74;
struct LuaRemote { network::RemoteId id; bool function; ScriptContext context; };
RemoteBinding* binding(lua_State* L,int up=1){return static_cast<RemoteBinding*>(lua_touserdata(L,lua_upvalueindex(up)));}
LuaRemote* remote(lua_State* L,int index){return static_cast<LuaRemote*>(lua_touserdatatagged(L,index,kRemoteTag));}
}
RemoteBinding::RemoteBinding(network::RemoteManager& m,InstanceRegistry& i,TaskScheduler& scheduler,ScriptContext c):manager_(m),instances_(i),scheduler_(scheduler),context_(c),main_(nullptr){
  eventSinkToken_=manager_.addEventSink([this](network::PlayerId player,std::string_view name,std::span<const std::byte> bytes){auto id=manager_.registry().find(name);if(!id)return;queued_.push_back({*id,server(),player,{bytes.begin(),bytes.end()}});});
}
RemoteBinding::~RemoteBinding(){manager_.removeEventSink(eventSinkToken_);if(main_){for(auto& c:callbacks_)lua_unref(main_,c.reference);for(auto& [_,r]:serverInvokeRefs_)lua_unref(main_,r);}}
void RemoteBinding::install(lua_State* L,const std::vector<std::pair<std::string,bool>>& remotes,ScriptContext scriptContext){
  main_=L;
  lua_newtable(L);lua_pushlightuserdata(L,this);lua_pushcclosure(L,index,"Remote.__index",1);lua_setfield(L,-2,"__index");
  lua_pushlightuserdata(L,this);lua_pushcclosure(L,newIndex,"Remote.__newindex",1);lua_setfield(L,-2,"__newindex");
  lua_setuserdatametatable(L,kRemoteTag);
  lua_getglobal(L,"game");if(!lua_istable(L,-1)){lua_pop(L,1);lua_newtable(L);}
  lua_pushlightuserdata(L,this);lua_pushcclosure(L,getRemote,"game:GetRemote",1);lua_setfield(L,-2,"GetRemote");
  lua_setglobal(L,"game");
  lua_newtable(L);
  for(const auto& [name,isFunction]:remotes){
    auto id=isFunction?manager_.registry().ensureFunction(name):manager_.registry().ensureEvent(name);if(!id)continue;
    idsByName_[name]=id;
    auto* value=static_cast<LuaRemote*>(lua_newuserdatataggedwithmetatable(L,sizeof(LuaRemote),kRemoteTag));new(value)LuaRemote{ id,isFunction,scriptContext };
    lua_setfield(L,-2,name.c_str());
  }
  lua_setglobal(L,"Remotes");
}
int RemoteBinding::getRemote(lua_State* L){auto* self=binding(L);const char* name=luaL_checkstring(L,2);if(!self||!self->idsByName_.contains(name)){lua_pushnil(L);return 1;}lua_getglobal(L,"Remotes");lua_getfield(L,-1,name);lua_remove(L,-2);return 1;}
int RemoteBinding::index(lua_State* L){auto* self=binding(L);return self?self->handleIndex(L):0;}
int RemoteBinding::newIndex(lua_State* L){auto* self=binding(L);return self?self->handleNewIndex(L):0;}
int RemoteBinding::fire(lua_State* L){auto* self=binding(L);return self?self->handleFire(L):0;}
int RemoteBinding::connect(lua_State* L){auto* self=binding(L);return self?self->handleConnect(L):0;}
int RemoteBinding::invoke(lua_State* L){auto* self=binding(L);return self?self->handleInvoke(L):0;}
int RemoteBinding::handleIndex(lua_State* L){
  auto* r=remote(L,1);const char* key=luaL_checkstring(L,2);if(!r){luaL_error(L,"Invalid Remote reference");return 0;}
  const bool serverRole=r->context==ScriptContext::Server;
  if(!r->function){
    if((r->context==ScriptContext::Client&&std::string_view(key)=="FireServer")||(serverRole&&(std::string_view(key)=="FireClient"||std::string_view(key)=="FireAllClients"))){lua_pushlightuserdata(L,this);lua_pushinteger(L,serverRole?(std::string_view(key)=="FireClient"?1:2):0);lua_pushcclosure(L,fire,key,2);return 1;}
    if((serverRole&&std::string_view(key)=="OnServerEvent")||(r->context==ScriptContext::Client&&std::string_view(key)=="OnClientEvent")){
      lua_newtable(L);lua_pushlightuserdata(L,this);lua_pushinteger(L,r->id);lua_pushboolean(L,serverRole);lua_pushcclosure(L,connect,"RemoteSignal.Connect",3);lua_setfield(L,-2,"Connect");return 1;
    }
  }else{
    if(r->context==ScriptContext::Client&&std::string_view(key)=="InvokeServer"){lua_pushlightuserdata(L,this);lua_pushcclosure(L,invoke,"RemoteFunction.InvokeServer",1);return 1;}
  }
  lua_pushnil(L);return 1;
}
int RemoteBinding::handleNewIndex(lua_State* L){
  auto* r=remote(L,1);const char* key=luaL_checkstring(L,2);
  if(!r||!r->function||!server()||r->context!=ScriptContext::Server||std::string_view(key)!="OnServerInvoke"){luaL_error(L,"This RemoteFunction property is unavailable in this script context");return 0;}
  if(!lua_isfunction(L,3)){luaL_typeerror(L,3,"function");return 0;}
  if(auto old=serverInvokeRefs_.find(r->id);old!=serverInvokeRefs_.end()){lua_unref(main_,old->second);serverInvokeRefs_.erase(old);}
  lua_pushvalue(L,3);const int ref=lua_ref(L,-1);lua_pop(L,1);serverInvokeRefs_[r->id]=ref;
  manager_.registry().setFunctionHandler(r->id,[this,id=r->id,ref](network::PlayerId player,std::span<const std::byte> bytes){
    std::vector<std::byte> result;std::string error;lua_getref(main_,ref);lua_pushnumber(main_,static_cast<double>(player));int count=0;
    if(!LuauSerializer::decode(main_,bytes,count,error,&instances_)){lua_settop(main_,0);return result;}
    if(lua_pcall(main_,count+1,LUA_MULTRET,0)!=LUA_OK){lua_pop(main_,1);return result;}
    const int values=lua_gettop(main_);LuauSerializer::encode(main_,1,values,result,error,&instances_);lua_settop(main_,0);(void)id;return result;
  });
  return 0;
}
int RemoteBinding::handleFire(lua_State* L){
  auto* r=remote(L,1);if(!r||r->function){luaL_error(L,"Expected RemoteEvent");return 0;}
  const int mode=static_cast<int>(lua_tointeger(L,lua_upvalueindex(2)));const int first=mode==1?3:2;network::PlayerId player=0;
  if((mode==0&&r->context!=ScriptContext::Client)||(mode!=0&&r->context!=ScriptContext::Server)){luaL_error(L,"RemoteEvent method is unavailable in this script context");return 0;}
  if(mode==1){const double n=luaL_checknumber(L,2);if(n<1||n>UINT32_MAX||std::floor(n)!=n){luaL_error(L,"FireClient expects a valid player id");return 0;}player=static_cast<network::PlayerId>(n);}
  std::vector<std::byte> bytes;std::string error;if(!LuauSerializer::encode(L,first,lua_gettop(L)-first+1,bytes,error,&instances_)){luaL_error(L,"%s",error.c_str());return 0;}
  bool ok=false;if(mode==0)ok=manager_.sendRemoteEvent(0,r->id,bytes);else if(mode==1)ok=manager_.sendRemoteEvent(player,r->id,bytes);else ok=manager_.broadcastRemoteEvent(r->id,bytes);
  if(!ok){luaL_error(L,"RemoteEvent send failed (not connected, unknown remote, or oversized payload)");return 0;}return 0;
}
int RemoteBinding::handleConnect(lua_State* L){
  const auto id=static_cast<network::RemoteId>(lua_tointeger(L,lua_upvalueindex(2)));const bool serverEvent=lua_toboolean(L,lua_upvalueindex(3))!=0;
  const int callbackIndex=lua_gettop(L);if(!lua_isfunction(L,callbackIndex)){luaL_typeerror(L,callbackIndex,"function");return 0;}
  lua_pushvalue(L,callbackIndex);const int ref=lua_ref(L,-1);lua_pop(L,1);callbacks_.push_back({id,serverEvent,ref});return 0;
}
int RemoteBinding::handleInvoke(lua_State* L){
  auto* r=remote(L,1);if(!r||!r->function||server()||r->context!=ScriptContext::Client){luaL_error(L,"InvokeServer is only available to client RemoteFunctions");return 0;}
  std::vector<std::byte> bytes;std::string error;if(!LuauSerializer::encode(L,2,lua_gettop(L)-1,bytes,error,&instances_)){luaL_error(L,"%s",error.c_str());return 0;}
  const auto remoteName=manager_.registry().name(r->id);if(!remoteName){luaL_error(L,"Unknown RemoteFunction");return 0;}
  const auto waitToken=scheduler_.markExternalWait(L);
  if(!waitToken){luaL_error(L,"RemoteFunction:InvokeServer must run inside a scheduled task coroutine (task.spawn/task.delay)");return 0;}
  const auto request=manager_.invokeServer(*remoteName,bytes,[this,waitToken](bool ok,std::vector<std::byte> response){completed_.push_back({waitToken,ok,std::move(response)});});
  if(!request){scheduler_.cancelExternalWait(L);luaL_error(L,"RemoteFunction request could not be sent");return 0;}
  return lua_yield(L,0);
}
void RemoteBinding::process(){
  manager_.processQueuedFunctions();
  while(!queued_.empty()){
    auto event=std::move(queued_.front());queued_.pop_front();std::string error;
    for(auto i=callbacks_.begin();i!=callbacks_.end();){if(i->id!=event.id||i->serverEvent!=event.serverEvent){++i;continue;}
      const int base=lua_gettop(main_);lua_getref(main_,i->reference);int args=0;if(event.serverEvent)lua_pushnumber(main_,static_cast<double>(event.player));
      if(!LuauSerializer::decode(main_,event.payload,args,error,&instances_)){lua_settop(main_,base);++i;continue;}
      if(lua_pcall(main_,args+(event.serverEvent?1:0),0,0)!=LUA_OK)lua_pop(main_,1);
      lua_settop(main_,base);
      ++i;
    }
  }
  while(!completed_.empty()){
    auto result=std::move(completed_.front());completed_.pop_front();auto* thread=scheduler_.externalThread(result.waitToken);if(!thread)continue;const int base=lua_gettop(thread);int count=0;std::string error;
    if(result.ok&&LuauSerializer::decode(thread,result.payload,count,error,&instances_))scheduler_.resumeExternal(result.waitToken,count);
    else{lua_settop(thread,base);lua_pushnil(thread);lua_pushstring(thread,result.ok?error.c_str():"RemoteFunction failed or timed out");scheduler_.resumeExternal(result.waitToken,2);}
  }
}
}
