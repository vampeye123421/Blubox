#pragma once

#include <cstdint>

namespace blubox::engine::input {

enum class InputFocusMode : std::uint8_t {
  Editor,
  GameViewport,
  Modal
};

// Keeps the editor and embedded client input paths mutually exclusive.
// The Studio layer owns OS cursor capture; this class only tracks routing.
class InputRouter {
 public:
  void focusEditor() noexcept {
    focus_ = InputFocusMode::Editor;
    pointerCaptured_ = false;
  }

  void captureGameViewport() noexcept {
    focus_ = InputFocusMode::GameViewport;
    pointerCaptured_ = true;
  }

  // Escape releases the cursor and hands UI interaction back to Studio while
  // leaving the simulation running. Clicking the viewport captures it again.
  void releaseGameViewport() noexcept { focusEditor(); }

  void focusModal() noexcept {
    focus_ = InputFocusMode::Modal;
    pointerCaptured_ = false;
  }

  [[nodiscard]] InputFocusMode focusMode() const noexcept { return focus_; }
  [[nodiscard]] bool pointerCaptured() const noexcept { return pointerCaptured_; }
  [[nodiscard]] bool routeGameInput() const noexcept {
    return focus_ == InputFocusMode::GameViewport && pointerCaptured_;
  }
  [[nodiscard]] bool routeEditorInput() const noexcept {
    return focus_ == InputFocusMode::Editor;
  }
  [[nodiscard]] bool routeModalInput() const noexcept {
    return focus_ == InputFocusMode::Modal;
  }

 private:
  InputFocusMode focus_ = InputFocusMode::Editor;
  bool pointerCaptured_ = false;
};

}  // namespace blubox::engine::input
