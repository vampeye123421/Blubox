#pragma once

#include "UiTypes.hpp"

namespace blubox::engine::ui {

// Shared editor palette and geometry tokens. Keep colors as RGBA bytes so the
// same source can feed ImGui, Raylib, and the engine draw-list renderer.
struct DarkTheme {
  Color canvas{0x0B, 0x0D, 0x10, 0xFF};
  Color surface{0x12, 0x14, 0x17, 0xFF};
  Color container{0x17, 0x1A, 0x1E, 0xFF};
  Color containerRaised{0x1B, 0x1F, 0x24, 0xFF};
  Color overlay{0x1E, 0x23, 0x29, 0xFF};
  Color hover{0x22, 0x26, 0x2C, 0xFF};
  Color pressed{0x28, 0x2D, 0x34, 0xFF};
  Color selected{0x24, 0x2B, 0x34, 0xFF};
  Color borderSubtle{0x26, 0x2B, 0x31, 0xFF};
  Color borderStrong{0x33, 0x3A, 0x42, 0xFF};
  Color accent{0x4C, 0x8D, 0xFF, 0xFF};
  Color textPrimary{0xF2, 0xF4, 0xF7, 0xFF};
  Color textSecondary{0xC9, 0xCF, 0xD6, 0xFF};
  Color textMuted{0x9A, 0xA3, 0xAD, 0xFF};
  Color textDisabled{0x6E, 0x76, 0x81, 0xFF};
  Color success{0x3E, 0xCF, 0x8E, 0xFF};
  Color warning{0xE8, 0xB3, 0x39, 0xFF};
  Color danger{0xE5, 0x53, 0x4B, 0xFF};

  float radiusControl = 4.0f;
  float radiusCard = 4.0f;
  float radiusMenu = 6.0f;
  float radiusDialog = 8.0f;
  float space2 = 2.0f;
  float space4 = 4.0f;
  float space8 = 8.0f;
  float space12 = 12.0f;
  float space16 = 16.0f;
  float space24 = 24.0f;
  float panelHeaderHeight = 32.0f;
  float explorerRowHeight = 26.0f;
  float propertyRowHeight = 28.0f;
};

inline constexpr DarkTheme kDarkTheme{};

} // namespace blubox::engine::ui
