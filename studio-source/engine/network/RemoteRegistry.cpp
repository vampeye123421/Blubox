#include "RemoteRegistry.hpp"
namespace blubox::engine::network {
namespace {RemoteId stableId(std::string_view n){std::uint32_t h=2166136261u;for(unsigned char c:n){h^=c;h*=16777619u;}auto id=static_cast<RemoteId>((h^(h>>16))&0xffff);return id?id:1;}}
RemoteId RemoteRegistry::registerEvent(std::string n,EventHandler h){if(n.empty()||n.size()>kMaxRemoteNameSize||!h||byName_.contains(n))return 0;auto id=stableId(n);if(entries_.contains(id))return 0;byName_[n]=id;entries_.emplace(id,Entry{id,std::move(n),std::move(h),{}});return id;}
RemoteId RemoteRegistry::ensureEvent(std::string n){if(auto i=byName_.find(n);i!=byName_.end())return i->second;if(n.empty()||n.size()>kMaxRemoteNameSize)return 0;auto id=stableId(n);if(entries_.contains(id))return 0;byName_[n]=id;entries_.emplace(id,Entry{id,std::move(n),{}, {}});return id;}
RemoteId RemoteRegistry::ensureFunction(std::string n){if(auto i=byName_.find(n);i!=byName_.end())return i->second;if(n.empty()||n.size()>kMaxRemoteNameSize)return 0;auto id=stableId(n);if(entries_.contains(id))return 0;byName_[n]=id;entries_.emplace(id,Entry{id,std::move(n),{}, {}});return id;}
RemoteId RemoteRegistry::registerFunction(std::string n,FunctionHandler h){if(n.empty()||n.size()>kMaxRemoteNameSize||!h||byName_.contains(n))return 0;auto id=stableId(n);if(entries_.contains(id))return 0;byName_[n]=id;entries_.emplace(id,Entry{id,std::move(n),{},std::move(h)});return id;}
bool RemoteRegistry::setFunctionHandler(RemoteId id,FunctionHandler h){auto i=entries_.find(id);if(i==entries_.end()||!h)return false;i->second.function=std::move(h);return true;}
std::optional<RemoteId> RemoteRegistry::find(std::string_view n)const{auto i=byName_.find(std::string(n));if(i==byName_.end())return{};return i->second;}
bool RemoteRegistry::dispatchEvent(PlayerId p,RemoteId id,std::span<const std::byte>b)const{auto i=entries_.find(id);if(i==entries_.end()||!i->second.event)return false;i->second.event(p,b);return true;}
bool RemoteRegistry::dispatchFunction(PlayerId p,RemoteId id,std::span<const std::byte>b,std::vector<std::byte>&out)const{auto i=entries_.find(id);if(i==entries_.end()||!i->second.function)return false;out=i->second.function(p,b);return out.size()<=kMaxPayloadSize;}
std::optional<std::string_view> RemoteRegistry::name(RemoteId id)const{auto i=entries_.find(id);if(i==entries_.end())return{};return i->second.name;}
}
