#pragma once
#include <cstdint>
#include <vector>
namespace blubox::engine::ui::legacy {
struct Vec2 {
  float x = 0, y = 0;
};
struct Insets {
  float left = 0, top = 0, right = 0, bottom = 0;
};
enum class Kind : std::uint8_t { ScreenGui, Frame, TextButton, ListLayout, GridLayout, Padding };
enum class Direction : std::uint8_t { Vertical, Horizontal };
struct Node {
  Kind kind = Kind::Frame;
  Vec2 position{}, size{100, 100}, minimum{}, maximum{100000, 100000};
  Insets padding{};
  Direction direction = Direction::Vertical;
  float gap = 0;
  Vec2 cell{100, 30};
  bool visible = true, dirty = true;
  std::vector<Node> children;
  Vec2 measured{}, arrangedPosition{}, arrangedSize{};
};
class LayoutEngine {
 public:
  void layout(Node& root, Vec2 viewport);

 private:
  Vec2 measure(Node& node, Vec2 available);
  void arrange(Node& node, Vec2 origin, Vec2 size);
};
}  // namespace blubox::engine::ui::legacy
