#pragma once
#include "UiScene.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace blubox::engine::ui {
struct ResolvedNode { InstanceHandle handle{}; Rect rect{}; float rotation=0, transparency=0; bool visible=true, clipsDescendants=false; Style style{}; std::uint64_t sortKey=0; };
enum class DrawType : std::uint8_t { Rectangle, Image, Text, PushClip, PopClip };
struct DrawCommand {
  DrawType type=DrawType::Rectangle; Rect rect{}, clip{}; Color color{};
  float rotation=0; std::uint32_t textureId=0,fontId=0; float fontSize=16;
  TextX textX=TextX::Left; TextY textY=TextY::Center; std::string text;
  std::uint64_t sortKey=0;
  Color borderColor{0,0,0,0}; float borderSize=0.0f, cornerRadius=0.0f;
};
// The produced list is an immutable snapshot: consumers receive const access only.
class UiDrawList {
 public:
  UiDrawList()=default;
  const std::vector<DrawCommand>& commands() const noexcept;
  const std::vector<ResolvedNode>& nodes() const noexcept;
  std::uint64_t revision() const noexcept;
 private:
  struct Data { std::vector<DrawCommand> commands;std::vector<ResolvedNode> nodes;std::uint64_t revision=0; };
  friend class UiLayoutResolver;
  explicit UiDrawList(std::shared_ptr<const Data> data):data_(std::move(data)){}
  std::shared_ptr<const Data> data_;
};
class UiLayoutResolver {
 public:
  UiDrawList resolve(const UiScene&, Vec2 viewport);
 private:
  Vec2 measure(const UiScene&, InstanceHandle, Vec2 available);
  void arrange(const UiScene&, InstanceHandle, Rect parent, std::uint16_t depth, std::uint16_t sibling);
  Rect padded(Rect, const std::optional<Padding>&) const;
  std::vector<DrawCommand> commands_;
  std::vector<ResolvedNode> nodes_;
  std::vector<Vec2> measured_;
  std::uint64_t revision_=0;
};
}
