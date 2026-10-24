#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace blubox::engine::ui {

struct InstanceHandle {
  std::uint32_t id = 0;
  std::uint32_t generation = 0;
  constexpr bool operator==(const InstanceHandle&) const = default;
  constexpr explicit operator bool() const noexcept { return id != 0; }
};

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Rect { Vec2 position{}, size{}; };
struct Color { std::uint8_t r=255, g=255, b=255, a=255; };
struct UDim { float scale=0.0f, offset=0.0f; };
struct UDim2 { UDim x{}, y{}; };

inline Vec2 resolve(const UDim2& v, Vec2 parent) noexcept {
  return {v.x.scale * parent.x + v.x.offset,
          v.y.scale * parent.y + v.y.offset};
}

enum class Element : std::uint8_t {
  ScreenGui, Frame, TextLabel, TextButton, TextBox,
  ImageLabel, ImageButton, ScrollingFrame
};
enum class Direction : std::uint8_t { Horizontal, Vertical };
enum class Alignment : std::uint8_t { Begin, Center, End };
enum class TextX : std::uint8_t { Left, Center, Right };
enum class TextY : std::uint8_t { Top, Center, Bottom };

struct Padding { UDim left{}, right{}, top{}, bottom{}; };
struct ListLayout {
  Direction direction = Direction::Vertical;
  Alignment horizontal = Alignment::Begin, vertical = Alignment::Begin;
  float spacing = 0.0f;
  bool wraps = false;
};
struct GridLayout { std::uint32_t columns=1; Vec2 cell{100,100}; float horizontalSpacing=0, verticalSpacing=0; };
struct AspectRatio { float ratio=1.0f; bool widthDominant=true; };
struct Style {
  Color background{0,0,0,0}, border{0,0,0,255}, text{255,255,255,255};
  float backgroundTransparency=0.0f, textTransparency=0.0f;
  float borderSize=0.0f, cornerRadius=0.0f, fontSize=16.0f;
  std::uint32_t textureId=0, fontId=0;
  TextX textX=TextX::Left;
  TextY textY=TextY::Center;
};
struct Node {
  InstanceHandle handle{}, parent{};
  Element type=Element::Frame;
  UDim2 position{}, size{UDim{0,100}, UDim{0,100}};
  Vec2 anchor{};
  float rotation=0.0f, transparency=0.0f;
  bool visible=true, clipsDescendants=false, active=true;
  std::uint32_t zIndex=0, layoutOrder=0;
  std::string text, imagePath;
  Style style{};
};

} // namespace blubox::engine::ui
