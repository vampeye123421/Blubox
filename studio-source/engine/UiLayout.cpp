#include "UiLayout.hpp"
#include <algorithm>
namespace blubox::engine::ui::legacy {
namespace {
float clamp(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
}  // namespace
void LayoutEngine::layout(Node& root, Vec2 viewport) {
  measure(root, viewport);
  arrange(root, {}, viewport);
}
Vec2 LayoutEngine::measure(Node& n, Vec2 available) {
  if (!n.visible) {
    n.measured = {};
    return n.measured;
  }
  float w = 0, h = 0;
  Node* layoutNode = nullptr;
  for (auto& c : n.children) {
    if (c.kind == Kind::ListLayout || c.kind == Kind::GridLayout) {
      layoutNode = &c;
      continue;
    }
    if (c.kind == Kind::Padding) {
      n.padding = c.padding;
      continue;
    }
    auto s = measure(c, available);
    w = std::max(w, s.x);
    h = std::max(h, s.y);
  }
  const float contentW = std::max(0.0f, available.x - n.padding.left - n.padding.right);
  if (layoutNode && layoutNode->kind == Kind::ListLayout) {
    w = 0;
    h = 0;
    unsigned count = 0;
    for (auto& c : n.children)
      if (c.kind != Kind::ListLayout && c.kind != Kind::GridLayout && c.kind != Kind::Padding) {
        auto s = c.measured;
        if (layoutNode->direction == Direction::Vertical) {
          w = std::max(w, s.x);
          h += s.y;
        } else {
          w += s.x;
          h = std::max(h, s.y);
        }
        ++count;
      }
    if (count > 1) {
      if (layoutNode->direction == Direction::Vertical)
        h += layoutNode->gap * (count - 1);
      else
        w += layoutNode->gap * (count - 1);
    }
  }
  if (layoutNode && layoutNode->kind == Kind::GridLayout) {
    unsigned count = 0;
    for (auto& c : n.children)
      if (c.kind != Kind::ListLayout && c.kind != Kind::GridLayout && c.kind != Kind::Padding) ++count;
    const int cols = std::max(1, static_cast<int>(contentW / std::max(1.0f, layoutNode->cell.x + layoutNode->gap)));
    w = std::min(contentW, cols * layoutNode->cell.x + std::max(0, cols - 1) * layoutNode->gap);
    const int rows = (count + cols - 1) / cols;
    h = rows * layoutNode->cell.y + std::max(0, rows - 1) * layoutNode->gap;
  }
  if (n.kind == Kind::Frame || n.kind == Kind::TextButton) {
    w = std::max(w, n.size.x);
    h = std::max(h, n.size.y);
  }
  n.measured = {clamp(w + n.padding.left + n.padding.right, n.minimum.x, n.maximum.x),
                clamp(h + n.padding.top + n.padding.bottom, n.minimum.y, n.maximum.y)};
  n.dirty = false;
  return n.measured;
}
void LayoutEngine::arrange(Node& n, Vec2 p, Vec2 s) {
  n.arrangedPosition = p;
  n.arrangedSize = s;
  if (!n.visible) return;
  Node* lm = nullptr;
  for (auto& c : n.children)
    if (c.kind == Kind::ListLayout || c.kind == Kind::GridLayout) lm = &c;
  Vec2 origin{p.x + n.padding.left, p.y + n.padding.top};
  Vec2 content{std::max(0.0f, s.x - n.padding.left - n.padding.right),
               std::max(0.0f, s.y - n.padding.top - n.padding.bottom)};
  unsigned i = 0;
  float cursor = 0;
  int cols = 1;
  if (lm && lm->kind == Kind::GridLayout)
    cols = std::max(1, static_cast<int>(content.x / std::max(1.0f, lm->cell.x + lm->gap)));
  for (auto& c : n.children) {
    if (c.kind == Kind::Padding || c.kind == Kind::ListLayout || c.kind == Kind::GridLayout) continue;
    Vec2 cp = origin, cs = c.measured;
    if (lm && lm->kind == Kind::ListLayout) {
      if (lm->direction == Direction::Vertical) {
        cp.y += cursor;
        cursor += cs.y + lm->gap;
      } else {
        cp.x += cursor;
        cursor += cs.x + lm->gap;
      }
    } else if (lm && lm->kind == Kind::GridLayout) {
      cp.x += (i % cols) * (lm->cell.x + lm->gap);
      cp.y += (i / cols) * (lm->cell.y + lm->gap);
      cs = lm->cell;
    } else {
      cp.x += c.position.x;
      cp.y += c.position.y;
      cs = c.size;
    }
    ++i;
    arrange(c, cp, cs);
  }
}
}  // namespace blubox::engine::ui::legacy
