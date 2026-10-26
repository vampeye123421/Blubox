#include "MeshCache.hpp"

#include <bit>
#include <fstream>
#include <mutex>

namespace blubox::engine::assets {
namespace {
void Mix(std::uint64_t& hash, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) { hash ^= static_cast<std::uint8_t>(value >> (i * 8)); hash *= 1099511628211ull; }
}
MeshAssetId HashMesh(const ImportedMesh& mesh) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& vertex : mesh.vertices) {
        Mix(hash, std::bit_cast<std::uint32_t>(vertex.position.x)); Mix(hash, std::bit_cast<std::uint32_t>(vertex.position.y)); Mix(hash, std::bit_cast<std::uint32_t>(vertex.position.z));
        Mix(hash, std::bit_cast<std::uint32_t>(vertex.normal.x)); Mix(hash, std::bit_cast<std::uint32_t>(vertex.normal.y)); Mix(hash, std::bit_cast<std::uint32_t>(vertex.normal.z));
        Mix(hash, std::bit_cast<std::uint32_t>(vertex.uv.x)); Mix(hash, std::bit_cast<std::uint32_t>(vertex.uv.y));
    }
    for (auto index : mesh.indices) Mix(hash, index);
    Mix(hash, static_cast<std::uint32_t>(mesh.materialId));
    return hash ? hash : 1;
}
template<class T> bool Write(std::ostream& out, T value) {
    if constexpr (std::is_floating_point_v<T>) return Write(out, std::bit_cast<std::uint32_t>(value));
    else {
        for (std::size_t i = 0; i < sizeof(T); ++i) out.put(static_cast<char>(value >> (i * 8)));
        return static_cast<bool>(out);
    }
}
template<class T> bool Read(std::istream& in, T& value) {
    if constexpr (std::is_floating_point_v<T>) {
        std::uint32_t bits = 0; if (!Read(in, bits)) return false; value = std::bit_cast<float>(bits); return std::isfinite(value);
    } else {
        value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) { const int ch = in.get(); if (ch == EOF) return false; value |= static_cast<T>(static_cast<unsigned char>(ch)) << (i * 8); }
        return true;
    }
}
}

MeshAssetId MeshCache::insert(ImportedMesh mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0) return 0;
    for (std::uint32_t index : mesh.indices) if (index >= mesh.vertices.size()) return 0;
    const MeshAssetId id = HashMesh(mesh);
    std::unique_lock lock(mutex_);
    auto [it, inserted] = entries_.try_emplace(id, std::make_shared<Entry>(Entry{std::move(mesh), 1}));
    if (!inserted) ++it->second->references;
    return id;
}

bool MeshCache::retain(MeshAssetId id) {
    std::unique_lock lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    ++found->second->references;
    return true;
}

bool MeshCache::release(MeshAssetId id) {
    std::unique_lock lock(mutex_);
    auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    if (--found->second->references == 0) entries_.erase(found);
    return true;
}

std::shared_ptr<const ImportedMesh> MeshCache::get(MeshAssetId id) const {
    std::shared_lock lock(mutex_);
    auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : std::shared_ptr<const ImportedMesh>(found->second, &found->second->mesh);
}

std::uint32_t MeshCache::referenceCount(MeshAssetId id) const {
    std::shared_lock lock(mutex_);
    auto found = entries_.find(id);
    return found == entries_.end() ? 0 : found->second->references;
}

bool MeshCache::saveAsset(MeshAssetId id, const std::filesystem::path& path, std::string& error) const {
    auto mesh = get(id);
    if (!mesh) { error = "Mesh asset is not in the cache."; return false; }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create the map asset folder."; return false; }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not open the mesh asset file for writing."; return false; }
    constexpr char magic[8] = {'B','L','M','E','S','H','1','\0'};
    out.write(magic, sizeof(magic));
    if (!Write(out, std::uint32_t{1}) || !Write(out, static_cast<std::uint32_t>(mesh->vertices.size())) ||
        !Write(out, static_cast<std::uint32_t>(mesh->indices.size())) || !Write(out, mesh->materialId)) {
        error = "Could not write mesh asset header."; return false;
    }
    for (const auto& v : mesh->vertices) {
        const float data[]{v.position.x,v.position.y,v.position.z,v.normal.x,v.normal.y,v.normal.z,v.uv.x,v.uv.y};
        for (float f : data) if (!Write(out, f)) { error = "Could not write mesh vertex data."; return false; }
    }
    for (auto index : mesh->indices) if (!Write(out, index)) { error = "Could not write mesh index data."; return false; }
    if (!out) { error = "Mesh asset file write failed."; return false; }
    error.clear(); return true;
}

MeshAssetId MeshCache::loadAsset(const std::filesystem::path& path, MeshAssetId expectedId, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    constexpr std::uintmax_t kMaximumAssetBytes = 512ull * 1024ull * 1024ull;
    if (ec || size < 24 || size > kMaximumAssetBytes) { error = "Mesh sidecar is missing or has an invalid size."; return 0; }
    std::ifstream in(path, std::ios::binary);
    char magic[8]{}; in.read(magic, sizeof(magic));
    constexpr char expectedMagic[8] = {'B','L','M','E','S','H','1','\0'};
    if (!in || !std::equal(std::begin(magic), std::end(magic), std::begin(expectedMagic))) { error = "Mesh sidecar has an unsupported signature."; return 0; }
    std::uint32_t version=0, vertices=0, indices=0; std::uint64_t material=0;
    if (!Read(in, version) || !Read(in, vertices) || !Read(in, indices) || !Read(in, material) || version != 1 ||
        vertices == 0 || vertices > 5'000'000 || indices == 0 || indices > 15'000'000 || indices % 3 != 0 ||
        size != 8 + 4 + 4 + 4 + 8 + static_cast<std::uintmax_t>(vertices) * 32 + static_cast<std::uintmax_t>(indices) * 4) {
        error = "Mesh sidecar counts or version are invalid."; return 0;
    }
    ImportedMesh mesh; mesh.vertices.resize(vertices); mesh.indices.resize(indices); mesh.materialId = material;
    for (auto& v : mesh.vertices) {
        float data[8]{}; for (float& f : data) if (!Read(in, f)) { error = "Mesh sidecar has invalid vertex data."; return 0; }
        v.position={data[0],data[1],data[2]}; v.normal={data[3],data[4],data[5]}; v.uv={data[6],data[7]};
    }
    for (auto& index : mesh.indices) if (!Read(in, index) || index >= vertices) { error = "Mesh sidecar contains an invalid index."; return 0; }
    const MeshAssetId inserted = insert(std::move(mesh));
    if (inserted != expectedId) { if (inserted) release(inserted); error = "Mesh sidecar content hash does not match the map reference."; return 0; }
    error.clear(); return inserted;
}

void MeshCache::clear() { std::unique_lock lock(mutex_); entries_.clear(); }
} // namespace blubox::engine::assets
