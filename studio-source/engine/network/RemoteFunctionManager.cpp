#include "RemoteFunctionManager.hpp"
namespace blubox::engine::network {
std::uint64_t RemoteFunctionManager::begin(PlayerId p,RemoteId r,Completion cb,std::chrono::milliseconds timeout){if(!cb)return 0;auto id=next_++;if(id==0)id=next_++;pending_.emplace(id,Pending{p,r,std::chrono::steady_clock::now()+timeout,std::move(cb)});return id;}
bool RemoteFunctionManager::complete(std::uint64_t id,bool ok,std::span<const std::byte>bytes){auto i=pending_.find(id);if(i==pending_.end())return false;auto cb=std::move(i->second.callback);pending_.erase(i);cb(ok,{bytes.begin(),bytes.end()});return true;}
void RemoteFunctionManager::expire(){auto now=std::chrono::steady_clock::now();for(auto i=pending_.begin();i!=pending_.end();){if(i->second.deadline<=now){auto cb=std::move(i->second.callback);i=pending_.erase(i);cb(false,{});}else ++i;}}
void RemoteFunctionManager::cancelAll(){for(auto&[_,p]:pending_)p.callback(false,{});pending_.clear();}
}
