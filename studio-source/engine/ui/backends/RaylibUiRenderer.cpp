#include "RaylibUiRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <string>
namespace blubox::engine::ui {
namespace { ::Color cv(Color c){return {c.r,c.g,c.b,c.a};} }
void RaylibUiRenderer::drawRectangle(const Rect&r,Color c,float rotation,float radius,Color border,float borderSize){
  const Rectangle rect{r.position.x,r.position.y,r.size.x,r.size.y};
  if(radius<=0.0f||rotation!=0.0f){
    DrawRectanglePro(rect,{0,0},rotation,cv(c));
    if(borderSize>0.0f&&rotation==0.0f)DrawRectangleLinesEx(rect,borderSize,cv(border));
    return;
  }
  const float minSide=std::max(1.0f,std::min(r.size.x,r.size.y));
  const float roundness=std::clamp((2.0f*radius)/minSide,0.0f,1.0f);
  const int segments=std::clamp(static_cast<int>(radius*1.5f),4,16);
  DrawRectangleRounded(rect,roundness,segments,cv(c));
  if(borderSize>0.0f)DrawRectangleRoundedLinesEx(rect,roundness,segments,borderSize,cv(border));
}
void RaylibUiRenderer::drawImage(const Rect&r,std::uint32_t id,float rotation){auto i=textures_.find(id);if(i==textures_.end())return;auto t=i->second;DrawTexturePro(t,{0,0,float(t.width),float(t.height)},{r.position.x,r.position.y,r.size.x,r.size.y},{0,0},rotation,::Color{255,255,255,255});}
void RaylibUiRenderer::drawText(const Rect&r,std::string_view text,std::uint32_t id,float size,Color color,TextX ax,TextY ay){std::string owned(text);Font f=GetFontDefault();if(auto i=fonts_.find(id);i!=fonts_.end())f=i->second;Vector2 m=MeasureTextEx(f,owned.c_str(),size,1);float x=r.position.x,y=r.position.y;if(ax==TextX::Center)x+=(r.size.x-m.x)*.5f;else if(ax==TextX::Right)x+=r.size.x-m.x;if(ay==TextY::Center)y+=(r.size.y-m.y)*.5f;else if(ay==TextY::Bottom)y+=r.size.y-m.y;DrawTextEx(f,owned.c_str(),{x,y},size,1,cv(color));}
void RaylibUiRenderer::pushClip(const Rect&r){
  const float left=std::clamp(r.position.x,0.0f,static_cast<float>(GetScreenWidth()));
  const float top=std::clamp(r.position.y,0.0f,static_cast<float>(GetScreenHeight()));
  const float right=std::clamp(r.position.x+std::max(0.0f,r.size.x),left,static_cast<float>(GetScreenWidth()));
  const float bottom=std::clamp(r.position.y+std::max(0.0f,r.size.y),top,static_cast<float>(GetScreenHeight()));
  Rect effective{{left,top},{right-left,bottom-top}};
  if(!clipStack_.empty()){
    const auto&p=clipStack_.back();
    const float x=std::max(effective.position.x,p.position.x),y=std::max(effective.position.y,p.position.y);
    const float right=std::min(effective.position.x+effective.size.x,p.position.x+p.size.x);
    const float bottom=std::min(effective.position.y+effective.size.y,p.position.y+p.size.y);
    effective={{x,y},{std::max(0.0f,right-x),std::max(0.0f,bottom-y)}};
  }
  clipStack_.push_back(effective);
  BeginScissorMode(static_cast<int>(std::floor(effective.position.x)),static_cast<int>(std::floor(effective.position.y)),
                   static_cast<int>(std::ceil(effective.size.x)),static_cast<int>(std::ceil(effective.size.y)));
}
void RaylibUiRenderer::popClip(){if(clipStack_.empty())return;clipStack_.pop_back();EndScissorMode();if(!clipStack_.empty()){const auto&r=clipStack_.back();BeginScissorMode(int(r.position.x),int(r.position.y),int(r.size.x),int(r.size.y));}}
}
