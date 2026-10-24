#include "RaylibMeshAdapter.hpp"

#include <algorithm>
#include <limits>

namespace blubox::engine::render {
RaylibMeshAdapter::~RaylibMeshAdapter() {
    // GPU cleanup is explicit so callers can release meshes before CloseWindow().
}

bool RaylibMeshAdapter::upload(assets::MeshAssetId id, const assets::ImportedMesh& source, std::string* error) {
    if (id == 0 || source.vertices.empty() || source.indices.empty() || source.indices.size() % 3 != 0 ||
        source.vertices.size() > std::numeric_limits<unsigned short>::max()) {
        if (error) *error = "Mesh is empty, invalid, or exceeds Raylib's 16-bit index limit.";
        return false;
    }
    for (auto index : source.indices) if (index >= source.vertices.size()) {
        if (error) *error = "Mesh contains an out-of-range index.";
        return false;
    }
    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(source.vertices.size());
    mesh.triangleCount = static_cast<int>(source.indices.size() / 3);
    mesh.vertices = static_cast<float*>(MemAlloc(source.vertices.size() * 3 * sizeof(float)));
    mesh.normals = static_cast<float*>(MemAlloc(source.vertices.size() * 3 * sizeof(float)));
    mesh.texcoords = static_cast<float*>(MemAlloc(source.vertices.size() * 2 * sizeof(float)));
    mesh.indices = static_cast<unsigned short*>(MemAlloc(source.indices.size() * sizeof(unsigned short)));
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices) {
        UnloadMesh(mesh);
        if (error) *error = "Could not allocate Raylib mesh buffers.";
        return false;
    }
    for (std::size_t i = 0; i < source.vertices.size(); ++i) {
        const auto& vertex = source.vertices[i];
        const float values[]{vertex.position.x, vertex.position.y, vertex.position.z,
                             vertex.normal.x, vertex.normal.y, vertex.normal.z,
                             vertex.uv.x, vertex.uv.y};
        for (float value : values) if (!std::isfinite(value)) {
            UnloadMesh(mesh);
            if (error) *error = "Mesh contains a non-finite vertex attribute.";
            return false;
        }
        mesh.vertices[i * 3] = vertex.position.x; mesh.vertices[i * 3 + 1] = vertex.position.y; mesh.vertices[i * 3 + 2] = vertex.position.z;
        mesh.normals[i * 3] = vertex.normal.x; mesh.normals[i * 3 + 1] = vertex.normal.y; mesh.normals[i * 3 + 2] = vertex.normal.z;
        mesh.texcoords[i * 2] = vertex.uv.x; mesh.texcoords[i * 2 + 1] = vertex.uv.y;
    }
    for (std::size_t i = 0; i < source.indices.size(); ++i) mesh.indices[i] = static_cast<unsigned short>(source.indices[i]);
    UploadMesh(&mesh, false);
    if (mesh.vaoId == 0 && mesh.vboId == nullptr) {
        UnloadMesh(mesh);
        if (error) *error = "Raylib failed to upload the mesh to the GPU.";
        return false;
    }
    auto existing = meshes_.find(id);
    if (existing != meshes_.end()) UnloadMesh(existing->second);
    meshes_[id] = mesh;
    if (error) error->clear();
    return true;
}

const Mesh* RaylibMeshAdapter::get(assets::MeshAssetId id) const {
    const auto found = meshes_.find(id);
    return found == meshes_.end() ? nullptr : &found->second;
}

void RaylibMeshAdapter::release(assets::MeshAssetId id) {
    const auto found = meshes_.find(id);
    if (found == meshes_.end()) return;
    UnloadMesh(found->second);
    meshes_.erase(found);
}

void RaylibMeshAdapter::clear() {
    for (auto& [id, mesh] : meshes_) { (void)id; UnloadMesh(mesh); }
    meshes_.clear();
}
} // namespace blubox::engine::render
