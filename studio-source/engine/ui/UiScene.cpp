#include "UiScene.hpp"
#include <stdexcept>

namespace blubox::engine::ui {
UiScene::Record& UiScene::checked(InstanceHandle h) {
  if (!alive(h)) throw std::out_of_range("stale UI instance handle");
  return records_[h.id];
}
const UiScene::Record& UiScene::checked(InstanceHandle h) const {
  if (!alive(h)) throw std::out_of_range("stale UI instance handle");
  return records_[h.id];
}
bool UiScene::alive(InstanceHandle h) const noexcept {
  return h.id && h.id < records_.size() && records_[h.id].alive && records_[h.id].generation==h.generation;
}
InstanceHandle UiScene::create(Element type, InstanceHandle parent) {
  if (parent && !alive(parent)) throw std::out_of_range("invalid UI parent");
  std::uint32_t id;
  if (free_.empty()) { id=static_cast<std::uint32_t>(records_.size()); records_.emplace_back(); }
  else { id=free_.back(); free_.pop_back(); }
  auto& r=records_[id]; r.alive=true; r.node=Node{}; r.node.type=type;
  r.node.handle={id,r.generation}; r.node.parent=parent; r.children.clear();
  r.padding.reset(); r.list.reset(); r.grid.reset(); r.aspect.reset();
  if (parent) checked(parent).children.push_back(r.node.handle); else roots_.push_back(r.node.handle);
  markDirty(r.node.handle); return r.node.handle;
}
bool UiScene::destroy(InstanceHandle h) {
  if (!alive(h)) return false;
  auto kids=records_[h.id].children;
  for (auto child:kids) destroy(child);
  auto& r=records_[h.id];
  auto& siblings=r.node.parent ? checked(r.node.parent).children : roots_;
  for (auto it=siblings.begin();it!=siblings.end();++it) if (*it==h) { siblings.erase(it); break; }
  r.alive=false; r.children.clear(); r.generation = r.generation==UINT32_MAX ? 1 : r.generation+1;
  free_.push_back(h.id); dirty_=true; return true;
}
Node& UiScene::get(InstanceHandle h) { markDirty(h); return checked(h).node; }
const Node& UiScene::get(InstanceHandle h) const { return checked(h).node; }
const std::vector<InstanceHandle>& UiScene::children(InstanceHandle h) const { return checked(h).children; }
void UiScene::setPadding(InstanceHandle h,Padding v){checked(h).padding=v;markDirty(h);}
void UiScene::setListLayout(InstanceHandle h,ListLayout v){checked(h).list=v;markDirty(h);}
void UiScene::setGridLayout(InstanceHandle h,GridLayout v){checked(h).grid=v;markDirty(h);}
void UiScene::setAspectRatio(InstanceHandle h,AspectRatio v){checked(h).aspect=v;markDirty(h);}
const std::optional<Padding>& UiScene::padding(InstanceHandle h)const{return checked(h).padding;}
const std::optional<ListLayout>& UiScene::listLayout(InstanceHandle h)const{return checked(h).list;}
const std::optional<GridLayout>& UiScene::gridLayout(InstanceHandle h)const{return checked(h).grid;}
const std::optional<AspectRatio>& UiScene::aspectRatio(InstanceHandle h)const{return checked(h).aspect;}
void UiScene::markDirty(InstanceHandle h) {
  if (!alive(h)) return;
  dirty_=true;
  // Dirty propagation is explicit; the current resolver rebuilds a flat immutable snapshot.
  for (auto child:records_[h.id].children) markDirty(child);
}
}
