#pragma once

#include "../assets/ImportedModel.hpp"

#include <raylib.h>

#include <unordered_map>
#include <vector>

namespace blubox::engine::render {

class RaylibMeshAdapter {
public:
    RaylibMeshAdapter() = default;
    ~RaylibMeshAdapter();
    RaylibMeshAdapter(const RaylibMeshAdapter&) = delete;
    RaylibMeshAdapter& operator=(const RaylibMeshAdapter&) = delete;

    bool upload(assets::MeshAssetId id, const assets::ImportedMesh& source, std::string* error = nullptr);
    const Mesh* get(assets::MeshAssetId id) const;
    void release(assets::MeshAssetId id);
    void clear(); // must be called while the Raylib graphics context is alive
private:
    std::unordered_map<assets::MeshAssetId, Mesh> meshes_;
};

} // namespace blubox::engine::render
