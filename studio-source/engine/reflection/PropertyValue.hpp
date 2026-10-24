#pragma once

#include "../EngineCore.hpp"
#include "../assets/AssetId.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>

namespace blubox::engine::reflection {

struct Vector2 { float x = 0.0f, y = 0.0f; };
struct Quaternion { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };
struct Color { std::uint8_t r = 255, g = 255, b = 255, a = 255; };
struct AssetReference { assets::AssetId id = 0; };
struct InstanceReference { InstanceHandle handle{}; };

using PropertyValue = std::variant<std::monostate, bool, std::int64_t, double, std::string,
                                   Vector2, Vec3, Quaternion, Color, AssetReference, InstanceReference>;

enum class PropertyType : std::uint8_t {
    None, Boolean, Integer, Float, String, Vector2, Vector3, Quaternion, Color, Enum, InstanceReference, AssetReference
};

} // namespace blubox::engine::reflection
