#pragma once

#include "AssetId.hpp"
#include "../EngineCore.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace blubox::engine::assets {

enum class TextureType : std::uint8_t { Albedo, Normal, Roughness, Metalness, Emissive, AmbientOcclusion, Decal };

struct TextureReference {
    AssetId assetId = kInvalidAssetId;
    TextureType type = TextureType::Albedo;
};

struct Vec2 {
    float x = 1.0f;
    float y = 1.0f;
};

struct MaterialProperties {
    std::array<std::uint8_t, 4> baseColor{255, 255, 255, 255};
    float roughness = 0.8f;
    float metalness = 0.0f;
    float emissiveIntensity = 0.0f;
    float normalScale = 1.0f;
    Vec2 uvTiling{1.0f, 1.0f};
    Vec2 uvOffset{0.0f, 0.0f};
    float uvRotationDegrees = 0.0f;
};

struct MaterialResource {
    MaterialId id = kInvalidAssetId;
    std::string name;
    TextureReference albedoMap{0, TextureType::Albedo};
    TextureReference normalMap{0, TextureType::Normal};
    TextureReference roughnessMap{0, TextureType::Roughness};
    TextureReference metalnessMap{0, TextureType::Metalness};
    TextureReference emissiveMap{0, TextureType::Emissive};
    TextureReference ambientOcclusionMap{0, TextureType::AmbientOcclusion};
    MaterialProperties properties{};
};

struct SurfaceAppearance {
    AssetId albedoMap = 0;
    AssetId normalMap = 0;
    AssetId roughnessMap = 0;
    AssetId metalnessMap = 0;
    AssetId emissiveMap = 0;
    float roughnessFactor = 0.8f;
    float metalnessFactor = 0.0f;
    float normalScale = 1.0f;
    float emissiveIntensity = 1.0f;
    Vec2 uvScale{1.0f, 1.0f};
    Vec2 uvOffset{0.0f, 0.0f};
    float uvRotationDegrees = 0.0f;
};

struct DecalComponent {
    AssetId textureId = 0;
    Vec2 size{1.0f, 1.0f};
    Vec2 uvOffset{0.0f, 0.0f};
    Vec2 uvScale{1.0f, 1.0f};
    float rotationDegrees = 0.0f;
    float transparency = 0.0f;
    float normalBlend = 0.0f;
    std::array<std::uint8_t, 4> tint{255, 255, 255, 255};
    bool depthTest = true;
};

struct MaterialReferenceComponent {
    MaterialId materialId = 0;
    bool useSurfaceAppearance = false;
    Vec2 uvScale{1.0f, 1.0f};
    Vec2 uvOffset{0.0f, 0.0f};
    float uvRotationDegrees = 0.0f;
};

inline Vec2 TransformUv(Vec2 uv, Vec2 scale, Vec2 offset, float rotationDegrees) noexcept {
    constexpr float kDegreesToRadians = 0.017453292519943295769f;
    const float radians = rotationDegrees * kDegreesToRadians;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float x = uv.x - 0.5f;
    const float y = uv.y - 0.5f;
    return {(x * cosine - y * sine) * scale.x + offset.x + 0.5f,
            (x * sine + y * cosine) * scale.y + offset.y + 0.5f};
}

} // namespace blubox::engine::assets
