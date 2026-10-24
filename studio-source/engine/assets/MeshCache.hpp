#pragma once

#include "AssetId.hpp"
#include "ImportedModel.hpp"

#include <shared_mutex>
#include <memory>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace blubox::engine::assets {

class MeshCache {
public:
    MeshAssetId insert(ImportedMesh mesh);
    bool retain(MeshAssetId id);
    bool release(MeshAssetId id);
    std::shared_ptr<const ImportedMesh> get(MeshAssetId id) const;
    std::uint32_t referenceCount(MeshAssetId id) const;
    bool saveAsset(MeshAssetId id, const std::filesystem::path& path, std::string& error) const;
    MeshAssetId loadAsset(const std::filesystem::path& path, MeshAssetId expectedId, std::string& error);
    void clear();
private:
    struct Entry { ImportedMesh mesh; std::uint32_t references = 1; };
    mutable std::shared_mutex mutex_;
    std::unordered_map<MeshAssetId, std::shared_ptr<Entry>> entries_;
};

} // namespace blubox::engine::assets
