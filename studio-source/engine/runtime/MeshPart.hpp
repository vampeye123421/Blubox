#pragma once

#include "../assets/AssetId.hpp"

#include <cstdint>

namespace blubox::engine::runtime {

struct MeshPartComponent {
    assets::MeshAssetId meshAssetId = 0;
    assets::MaterialId materialId = 0;
    bool doubleSided = false;
    bool castShadows = true;
    bool receiveShadows = true;
};

} // namespace blubox::engine::runtime
