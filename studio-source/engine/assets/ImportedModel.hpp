#pragma once

#include "MaterialResource.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace blubox::engine::assets {

struct ImportedVertex {
    Vec3 position{};
    Vec3 normal{0.0f, 1.0f, 0.0f};
    Vec2 uv{0.0f, 0.0f};
};

struct ImportedMesh {
    std::string name;
    std::vector<ImportedVertex> vertices;
    std::vector<std::uint32_t> indices;
    MaterialId materialId = 0;
};

struct ImportedNode {
    std::string name;
    std::uint32_t parentIndex = UINT32_MAX;
    Vec3 position{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    std::vector<std::uint32_t> meshIndices;
    std::vector<std::uint32_t> childIndices;
};

enum class ImportErrorCode : std::uint8_t {
    None, UnsupportedFormat, FileUnavailable, FileTooLarge, InvalidFile, MissingBuffers,
    UnsafeReference, ResourceLimit, InvalidGeometry, InvalidHierarchy
};

struct ImportError {
    ImportErrorCode code = ImportErrorCode::None;
    std::string message;
};

struct ImportedModel {
    std::filesystem::path sourcePath;
    std::vector<ImportedMesh> meshes;
    std::vector<ImportedNode> nodes;
    std::vector<MaterialResource> materials;
    std::size_t sourceBytes = 0;
};

struct ImportLimits {
    std::size_t maximumFileBytes = 256u * 1024u * 1024u;
    std::size_t maximumExternalFileBytes = 128u * 1024u * 1024u;
    std::size_t maximumVertices = 5'000'000;
    std::size_t maximumIndices = 15'000'000;
    std::size_t maximumMeshes = 100'000;
    std::size_t maximumNodes = 100'000;
    std::size_t maximumMaterials = 10'000;
    std::size_t maximumNameBytes = 512;
};

} // namespace blubox::engine::assets
