#include "RaylibMaterialAdapter.hpp"

#include <algorithm>

namespace blubox::engine::render {
namespace {
void SetMap(Material& material, int index, Texture2D texture) {
    if (index < 0 || index > MATERIAL_MAP_BRDF) return;
    if (texture.id != 0) material.maps[index].texture = texture;
}
}

bool RaylibMaterialAdapter::apply(Material& target, const assets::MaterialResource& source) const {
    const auto& properties = source.properties;
    target.maps[MATERIAL_MAP_ALBEDO].color = {properties.baseColor[0], properties.baseColor[1], properties.baseColor[2], properties.baseColor[3]};
    target.maps[MATERIAL_MAP_ALBEDO].value = 1.0f;
    target.maps[MATERIAL_MAP_METALNESS].value = std::clamp(properties.metalness, 0.0f, 1.0f);
    target.maps[MATERIAL_MAP_ROUGHNESS].value = std::clamp(properties.roughness, 0.0f, 1.0f);
    target.maps[MATERIAL_MAP_EMISSION].color = {255, 255, 255, 255};
    target.maps[MATERIAL_MAP_EMISSION].value = std::max(0.0f, properties.emissiveIntensity);
    SetMap(target, MATERIAL_MAP_ALBEDO, textures_.texture(source.albedoMap.assetId));
    SetMap(target, MATERIAL_MAP_NORMAL, textures_.texture(source.normalMap.assetId));
    SetMap(target, MATERIAL_MAP_ROUGHNESS, textures_.texture(source.roughnessMap.assetId));
    SetMap(target, MATERIAL_MAP_METALNESS, textures_.texture(source.metalnessMap.assetId));
    SetMap(target, MATERIAL_MAP_EMISSION, textures_.texture(source.emissiveMap.assetId));
    SetMap(target, MATERIAL_MAP_OCCLUSION, textures_.texture(source.ambientOcclusionMap.assetId));
    return true;
}

Material RaylibMaterialAdapter::create(const assets::MaterialResource& source) const {
    Material material = LoadMaterialDefault();
    apply(material, source);
    return material;
}
} // namespace blubox::engine::render
