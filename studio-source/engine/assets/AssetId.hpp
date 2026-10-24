#pragma once

#include <cstdint>

namespace blubox::engine::assets {

using AssetId = std::uint64_t;
using MaterialId = AssetId;
using TextureId = AssetId;
using MeshAssetId = AssetId;

constexpr AssetId kInvalidAssetId = 0;

} // namespace blubox::engine::assets
