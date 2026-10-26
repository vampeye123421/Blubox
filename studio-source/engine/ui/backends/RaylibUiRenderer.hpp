#pragma once
#include "../UiRenderer.hpp"
#include <raylib.h>
#include <unordered_map>
namespace blubox::engine::ui {
class RaylibUiRenderer final:public IUiRenderer {
 public:
  void setTexture(std::uint32_t id,Texture2D t){textures_[id]=t;}
  void setFont(std::uint32_t id,Font f){fonts_[id]=f;}
  void drawRectangle(const Rect&,Color,float,float,Color,float) override;
  void drawImage(const Rect&,std::uint32_t,float) override;
  void drawText(const Rect&,std::string_view,std::uint32_t,float,Color,TextX,TextY) override;
  void pushClip(const Rect&) override; void popClip() override;
 private: std::unordered_map<std::uint32_t,Texture2D> textures_; std::unordered_map<std::uint32_t,Font> fonts_; std::vector<Rect> clipStack_;
};
}
