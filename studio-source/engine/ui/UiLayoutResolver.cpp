#include "UiLayoutResolver.hpp"
#include <algorithm>
#include <cmath>

namespace blubox::engine::ui {
namespace {
float clampf(float v,float lo,float hi){return std::max(lo,std::min(v,hi));}
std::uint64_t key(std::uint32_t z,std::uint16_t d,std::uint16_t s){return (std::uint64_t(z)<<32)|(std::uint64_t(d)<<16)|s;}
}
const std::vector<DrawCommand>& UiDrawList::commands()const noexcept{static const std::vector<DrawCommand> empty;return data_?data_->commands:empty;}
const std::vector<ResolvedNode>& UiDrawList::nodes()const noexcept{static const std::vector<ResolvedNode> empty;return data_?data_->nodes:empty;}
std::uint64_t UiDrawList::revision()const noexcept{return data_?data_->revision:0;}
Vec2 UiLayoutResolver::measure(const UiScene& scene,InstanceHandle h,Vec2 available){
  const auto& n=scene.get(h); auto desired=blubox::engine::ui::resolve(n.size,available);
  Vec2 aggregate{};
  for(auto c:scene.children(h)){
    const auto& cn=scene.get(c); if(!cn.visible) continue;
    auto m=measure(scene,c,desired);
    if(scene.listLayout(h)){
      auto l=*scene.listLayout(h);
      if(l.direction==Direction::Vertical){aggregate.x=std::max(aggregate.x,m.x);aggregate.y+=m.y+l.spacing;}
      else {aggregate.x+=m.x+l.spacing;aggregate.y=std::max(aggregate.y,m.y);}
    }else if(scene.gridLayout(h)){
      auto g=*scene.gridLayout(h); auto col=std::max(1u,g.columns);
      aggregate.x=std::max(aggregate.x,float(col)*g.cell.x+float(col-1)*g.horizontalSpacing);
      aggregate.y+=g.cell.y+g.verticalSpacing;
    }else {aggregate.x=std::max(aggregate.x,m.x);aggregate.y=std::max(aggregate.y,m.y);}
  }
  // Explicit dimensions win; intrinsic child extent is available when both dimensions are automatic.
  Vec2 result{n.size.x.scale==0&&n.size.x.offset==0?aggregate.x:desired.x,
              n.size.y.scale==0&&n.size.y.offset==0?aggregate.y:desired.y};
  if(auto a=scene.aspectRatio(h);a&&a->ratio>0){if(a->widthDominant)result.y=result.x/a->ratio;else result.x=result.y*a->ratio;}
  if(h.id>=measured_.size()) measured_.resize(h.id+1);
  measured_[h.id]=result; return result;
}
Rect UiLayoutResolver::padded(Rect r,const std::optional<Padding>& op)const{
  if(!op)return r;
  const auto&p=*op;
  float l=p.left.scale*r.size.x+p.left.offset, rr=p.right.scale*r.size.x+p.right.offset;
  float t=p.top.scale*r.size.y+p.top.offset, b=p.bottom.scale*r.size.y+p.bottom.offset;
  return {{r.position.x+l,r.position.y+t},{std::max(0.0f,r.size.x-l-rr),std::max(0.0f,r.size.y-t-b)}};
}
void UiLayoutResolver::arrange(const UiScene& scene,InstanceHandle h,Rect parent,std::uint16_t depth,std::uint16_t sibling){
  const auto& n=scene.get(h); if(!n.visible)return;
  auto size=blubox::engine::ui::resolve(n.size,parent.size);
  if(n.size.x.scale==0&&n.size.x.offset==0&&h.id<measured_.size())size.x=measured_[h.id].x;
  if(n.size.y.scale==0&&n.size.y.offset==0&&h.id<measured_.size())size.y=measured_[h.id].y;
  if(auto a=scene.aspectRatio(h);a&&a->ratio>0){if(a->widthDominant)size.y=size.x/a->ratio;else size.x=size.y*a->ratio;}
  auto pos=blubox::engine::ui::resolve(n.position,parent.size); Rect r{{parent.position.x+pos.x-size.x*n.anchor.x,parent.position.y+pos.y-size.y*n.anchor.y},size};
  const auto sort=key(n.zIndex,depth,sibling);
  nodes_.push_back({h,r,n.rotation,n.transparency,true,n.clipsDescendants,n.style,sort});
  if(n.style.background.a && n.style.backgroundTransparency<1.0f){
    auto c=n.style.background;
    c.a=static_cast<std::uint8_t>(c.a*(1.0f-clampf(n.style.backgroundTransparency+n.transparency,0,1)));
    DrawCommand command{DrawType::Rectangle,r,{},c,n.rotation,0,0,n.style.fontSize,n.style.textX,n.style.textY,{},sort};
    command.borderColor=n.style.border;
    command.borderSize=n.style.borderSize;
    command.cornerRadius=n.style.cornerRadius;
    commands_.push_back(std::move(command));
  }
  if(n.style.textureId) commands_.push_back({DrawType::Image,r,{},n.style.background,n.rotation,n.style.textureId,0,n.style.fontSize,n.style.textX,n.style.textY,{},sort});
  if(!n.text.empty()) {
    auto textColor = n.style.text;
    textColor.a = static_cast<std::uint8_t>(textColor.a * (1.0f - clampf(n.style.textTransparency + n.transparency, 0.0f, 1.0f)));
    commands_.push_back({DrawType::Text,r,{},textColor,n.rotation,0,n.style.fontId,n.style.fontSize,n.style.textX,n.style.textY,n.text,sort});
  }
  auto content=padded(r,scene.padding(h));
  bool clip=n.clipsDescendants || n.type==Element::ScrollingFrame;
  if(clip) { DrawCommand push{};push.type=DrawType::PushClip;push.clip=content;commands_.push_back(std::move(push)); }
  auto children=scene.children(h); std::vector<InstanceHandle> ordered(children);
  std::stable_sort(ordered.begin(),ordered.end(),[&](auto a,auto b){return scene.get(a).zIndex==scene.get(b).zIndex?scene.get(a).layoutOrder<scene.get(b).layoutOrder:scene.get(a).zIndex<scene.get(b).zIndex;});
  const auto list=scene.listLayout(h); const auto grid=scene.gridLayout(h); float cursor=0; std::uint32_t index=0;
  std::uint32_t visibleCount=0;for(auto c:ordered)if(scene.get(c).visible)++visibleCount;
  for(std::size_t i=0;i<ordered.size();++i){auto c=ordered[i];const auto& cn=scene.get(c);if(!cn.visible)continue;
    Rect childParent=content;
    if(list){auto cs=blubox::engine::ui::resolve(cn.size,content.size);if(cn.size.x.scale==0&&cn.size.x.offset==0)cs.x=measured_[c.id].x;if(cn.size.y.scale==0&&cn.size.y.offset==0)cs.y=measured_[c.id].y;
      float axis=list->direction==Direction::Vertical?content.size.y:content.size.x;
      float item=list->direction==Direction::Vertical?cs.y:cs.x;
      float cross=list->direction==Direction::Vertical?content.size.x:content.size.y;
      float crossItem=list->direction==Direction::Vertical?cs.x:cs.y;
      float available=axis-(visibleCount?list->spacing*(visibleCount-1):0);
      float offset=list->direction==Direction::Vertical?content.position.y:content.position.x;
      float group=std::max(0.0f,available-float(visibleCount)*item);
      if(list->vertical==Alignment::Center&&list->direction==Direction::Vertical)offset+=group*0.5f;
      if(list->vertical==Alignment::End&&list->direction==Direction::Vertical)offset+=group;
      if(list->horizontal==Alignment::Center&&list->direction==Direction::Horizontal)offset+=group*0.5f;
      if(list->horizontal==Alignment::End&&list->direction==Direction::Horizontal)offset+=group;
      if(list->direction==Direction::Vertical){float align=list->horizontal==Alignment::Center?(cross-crossItem)*.5f:list->horizontal==Alignment::End?cross-crossItem:0;childParent={{content.position.x+align,offset+cursor},{cs.x,cs.y}};cursor+=item+list->spacing;}
      else {float align=list->vertical==Alignment::Center?(cross-crossItem)*.5f:list->vertical==Alignment::End?cross-crossItem:0;childParent={{offset+cursor,content.position.y+align},{cs.x,cs.y}};cursor+=item+list->spacing;}
    } else if(grid){auto col=std::max(1u,grid->columns), x=index%col,y=index/col;childParent={{content.position.x+x*(grid->cell.x+grid->horizontalSpacing),content.position.y+y*(grid->cell.y+grid->verticalSpacing)},grid->cell};}
    ++index; arrange(scene,c,childParent,static_cast<std::uint16_t>(std::min<unsigned>(65535,depth+1)),static_cast<std::uint16_t>(std::min<std::size_t>(65535,i)));
  }
  if(clip) { DrawCommand pop{};pop.type=DrawType::PopClip;commands_.push_back(std::move(pop)); }
}
UiDrawList UiLayoutResolver::resolve(const UiScene& scene,Vec2 viewport){
  nodes_.clear();commands_.clear();measured_.resize(1);
  Rect root{{0,0},viewport}; for(std::size_t i=0;i<scene.roots().size();++i){auto h=scene.roots()[i];measure(scene,h,viewport);arrange(scene,h,root,0,static_cast<std::uint16_t>(std::min<std::size_t>(i,65535)));}
  auto data=std::make_shared<UiDrawList::Data>();data->commands=std::move(commands_);data->nodes=std::move(nodes_);data->revision=++revision_;commands_.clear();nodes_.clear();return UiDrawList(std::move(data));
}
}
