#pragma once
#include "UiTypes.hpp"
#include <optional>

namespace blubox::engine::ui {

class UiScene {
 public:
  InstanceHandle create(Element type, InstanceHandle parent={});
  bool destroy(InstanceHandle handle);
  bool alive(InstanceHandle handle) const noexcept;
  Node& get(InstanceHandle handle);
  const Node& get(InstanceHandle handle) const;
  const std::vector<InstanceHandle>& children(InstanceHandle handle) const;
  void setPadding(InstanceHandle, Padding);
  void setListLayout(InstanceHandle, ListLayout);
  void setGridLayout(InstanceHandle, GridLayout);
  void setAspectRatio(InstanceHandle, AspectRatio);
  const std::optional<Padding>& padding(InstanceHandle) const;
  const std::optional<ListLayout>& listLayout(InstanceHandle) const;
  const std::optional<GridLayout>& gridLayout(InstanceHandle) const;
  const std::optional<AspectRatio>& aspectRatio(InstanceHandle) const;
  const std::vector<InstanceHandle>& roots() const noexcept { return roots_; }
  void markDirty(InstanceHandle);
  bool consumeDirty() noexcept { bool v=dirty_; dirty_=false; return v; }

 private:
  struct Record {
    Node node{}; std::uint32_t generation=1; bool alive=false;
    std::vector<InstanceHandle> children;
    std::optional<Padding> padding;
    std::optional<ListLayout> list;
    std::optional<GridLayout> grid;
    std::optional<AspectRatio> aspect;
  };
  Record& checked(InstanceHandle);
  const Record& checked(InstanceHandle) const;
  std::vector<Record> records_{1};
  std::vector<std::uint32_t> free_;
  std::vector<InstanceHandle> roots_;
  mutable std::vector<InstanceHandle> empty_;
  bool dirty_=true;
};
}
