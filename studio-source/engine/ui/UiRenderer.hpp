#pragma once
#include "UiLayoutResolver.hpp"
namespace blubox::engine::ui {
class IUiRenderer {
 public: virtual ~IUiRenderer()=default;
  virtual void drawRectangle(const Rect&,Color,float,float,Color,float)=0;
  virtual void drawImage(const Rect&,std::uint32_t,float)=0;
  virtual void drawText(const Rect&,std::string_view,std::uint32_t,float,Color,TextX,TextY)=0;
  virtual void pushClip(const Rect&)=0; virtual void popClip()=0;
  void render(const UiDrawList& list){for(const auto& c:list.commands())switch(c.type){
    case DrawType::Rectangle:drawRectangle(c.rect,c.color,c.rotation,c.cornerRadius,c.borderColor,c.borderSize);break;
    case DrawType::Image:drawImage(c.rect,c.textureId,c.rotation);break;
    case DrawType::Text:drawText(c.rect,c.text,c.fontId,c.fontSize,c.color,c.textX,c.textY);break;
    case DrawType::PushClip:pushClip(c.clip);break;case DrawType::PopClip:popClip();break;
  }}
};
}
