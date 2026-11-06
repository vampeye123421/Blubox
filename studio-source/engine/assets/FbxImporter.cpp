#include "FbxImporter.hpp"

#include <ufbx.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace blubox::engine::assets {
namespace {

std::string ToString(ufbx_string value, std::size_t maximumBytes) {
    if (!value.data || value.length == 0) return {};
    return std::string(value.data, std::min(value.length, maximumBytes));
}

bool Finite(ufbx_vec2 value) {
    return std::isfinite(static_cast<double>(value.x)) &&
           std::isfinite(static_cast<double>(value.y));
}

bool Finite(ufbx_vec3 value) {
    return std::isfinite(static_cast<double>(value.x)) &&
           std::isfinite(static_cast<double>(value.y)) &&
           std::isfinite(static_cast<double>(value.z));
}

bool Finite(ufbx_vec4 value) {
    return std::isfinite(static_cast<double>(value.x)) &&
           std::isfinite(static_cast<double>(value.y)) &&
           std::isfinite(static_cast<double>(value.z)) &&
           std::isfinite(static_cast<double>(value.w));
}

float ToFloat(ufbx_real value) {
    return static_cast<float>(value);
}

bool Finite(const ImportedNode& node) {
    return std::isfinite(node.position.x) && std::isfinite(node.position.y) &&
           std::isfinite(node.position.z) && std::isfinite(node.scale.x) &&
           std::isfinite(node.scale.y) && std::isfinite(node.scale.z) &&
           std::all_of(node.rotation.begin(), node.rotation.end(),
                       [](float value) { return std::isfinite(value); });
}

bool ReadFileSize(const std::filesystem::path& file, std::size_t maximumBytes,
                  std::size_t& size, ImportError& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || ec) {
        error = {ImportErrorCode::FileUnavailable, "FBX import file is not a regular file."};
        return false;
    }
    const auto fileSize = std::filesystem::file_size(file, ec);
    if (ec || fileSize == 0) {
        error = {ImportErrorCode::InvalidFile, "FBX import file is empty or unreadable."};
        return false;
    }
    if (fileSize > maximumBytes || fileSize > std::numeric_limits<std::size_t>::max()) {
        error = {ImportErrorCode::FileTooLarge, "FBX import file exceeds the configured size limit."};
        return false;
    }
    size = static_cast<std::size_t>(fileSize);
    return true;
}

bool MaterialResourceFromFbx(const ufbx_material& source, MaterialId id,
                             std::size_t maximumNameBytes, MaterialResource& output,
                             ImportError& error) {
    MaterialResource material;
    material.id = id;
    material.name = ToString(source.name, maximumNameBytes);
    if (material.name.empty()) material.name = "Material";

    ufbx_vec4 baseColor{1.0, 1.0, 1.0, 1.0};
    if (source.pbr.base_color.has_value) {
        baseColor = source.pbr.base_color.value_vec4;
    } else if (source.fbx.diffuse_color.has_value) {
        const auto diffuse = source.fbx.diffuse_color.value_vec3;
        baseColor = {diffuse.x, diffuse.y, diffuse.z, 1.0};
    }
    if (!Finite(baseColor)) {
        error = {ImportErrorCode::InvalidGeometry, "FBX material contains a non-finite base color."};
        return false;
    }
    for (std::size_t component = 0; component < material.properties.baseColor.size(); ++component) {
        const double value = component == 0 ? baseColor.x : component == 1 ? baseColor.y :
                             component == 2 ? baseColor.z : baseColor.w;
        material.properties.baseColor[component] = static_cast<std::uint8_t>(
            std::clamp(value * 255.0, 0.0, 255.0));
    }

    const double roughness = source.pbr.roughness.has_value
        ? static_cast<double>(source.pbr.roughness.value_real)
        : static_cast<double>(material.properties.roughness);
    const double metalness = source.pbr.metalness.has_value
        ? static_cast<double>(source.pbr.metalness.value_real)
        : static_cast<double>(material.properties.metalness);
    if (!std::isfinite(roughness) || !std::isfinite(metalness)) {
        error = {ImportErrorCode::InvalidGeometry, "FBX material contains non-finite PBR values."};
        return false;
    }
    material.properties.roughness = static_cast<float>(std::clamp(roughness, 0.0, 1.0));
    material.properties.metalness = static_cast<float>(std::clamp(metalness, 0.0, 1.0));
    output = std::move(material);
    return true;
}

const ufbx_material* MaterialAt(const ufbx_node& node, const ufbx_mesh& mesh,
                                std::size_t index) {
    if (node.materials.count > 0) {
        return index < node.materials.count ? node.materials.data[index] : nullptr;
    }
    return index < mesh.materials.count ? mesh.materials.data[index] : nullptr;
}

} // namespace

FbxImporter::FbxImporter(ImportLimits limits) : limits_(limits) {}

bool FbxImporter::import(const std::filesystem::path& file, ImportedModel& output,
                         ImportError& error) const {
    error = {};
    std::string extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension != ".fbx") {
        error = {ImportErrorCode::UnsupportedFormat, "FBX importer accepts .fbx files only."};
        return false;
    }

    std::size_t sourceBytes = 0;
    if (!ReadFileSize(file, limits_.maximumFileBytes, sourceBytes, error)) return false;

    const auto utf8Path = file.u8string();
    ufbx_load_opts options{};
    options.load_external_files = false;
    options.ignore_embedded = true;
    options.generate_missing_normals = true;
    options.node_depth_limit = 4096;
    const std::size_t minimumMemoryLimit = 64u * 1024u * 1024u;
    const std::size_t maximumSafeSize = std::numeric_limits<std::size_t>::max();
    const std::size_t memoryLimit = sourceBytes > maximumSafeSize / 4
        ? maximumSafeSize : std::max(minimumMemoryLimit, sourceBytes * 4);
    options.temp_allocator.memory_limit = memoryLimit;
    options.result_allocator.memory_limit = memoryLimit;

    ufbx_error loadError{};
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(
        ufbx_load_file_len(reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size(),
                           &options, &loadError),
        &ufbx_free_scene);
    if (!scene) {
        char message[512]{};
        const std::size_t messageLength = ufbx_format_error(message, sizeof(message), &loadError);
        const std::string detail(message, std::min(messageLength, sizeof(message) - 1));
        error = {ImportErrorCode::InvalidFile,
                 detail.empty() ? "Could not parse the FBX file." : "Could not parse the FBX file: " + detail};
        return false;
    }

    if (scene->materials.count > limits_.maximumMaterials) {
        error = {ImportErrorCode::ResourceLimit, "FBX material count exceeds the configured limit."};
        return false;
    }

    ImportedModel model;
    model.sourcePath = file;
    model.sourceBytes = sourceBytes;
    model.materials.reserve(scene->materials.count);
    std::unordered_map<const ufbx_material*, MaterialId> materialIds;
    for (std::size_t i = 0; i < scene->materials.count; ++i) {
        const auto* source = scene->materials.data[i];
        if (!source || i >= std::numeric_limits<MaterialId>::max()) {
            error = {ImportErrorCode::InvalidFile, "FBX contains an invalid material reference."};
            return false;
        }
        MaterialResource material;
        const MaterialId id = static_cast<MaterialId>(i + 1);
        if (!MaterialResourceFromFbx(*source, id, limits_.maximumNameBytes, material, error)) return false;
        model.materials.push_back(std::move(material));
        materialIds.emplace(source, id);
    }

    std::vector<const ufbx_node*> sourceNodes;
    sourceNodes.reserve(scene->nodes.count + 1);
    if (scene->root_node) sourceNodes.push_back(scene->root_node);
    for (std::size_t i = 0; i < scene->nodes.count; ++i) {
        const auto* source = scene->nodes.data[i];
        if (!source) {
            error = {ImportErrorCode::InvalidHierarchy, "FBX contains a null node reference."};
            return false;
        }
        if (sourceNodes.empty() || source != sourceNodes.front()) sourceNodes.push_back(source);
    }
    if (sourceNodes.size() > limits_.maximumNodes ||
        sourceNodes.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = {ImportErrorCode::ResourceLimit, "FBX node count exceeds the configured limit."};
        return false;
    }

    std::unordered_map<const ufbx_node*, std::uint32_t> nodeIndices;
    nodeIndices.reserve(sourceNodes.size());
    for (std::size_t i = 0; i < sourceNodes.size(); ++i) {
        if (!nodeIndices.emplace(sourceNodes[i], static_cast<std::uint32_t>(i)).second) {
            error = {ImportErrorCode::InvalidHierarchy, "FBX node table contains a duplicate node."};
            return false;
        }
    }

    model.nodes.reserve(sourceNodes.size());
    std::size_t totalVertices = 0;
    std::size_t totalIndices = 0;
    for (std::size_t nodeIndex = 0; nodeIndex < sourceNodes.size(); ++nodeIndex) {
        const ufbx_node& sourceNode = *sourceNodes[nodeIndex];
        ImportedNode node;
        node.name = ToString(sourceNode.name, limits_.maximumNameBytes);
        if (node.name.empty()) node.name = "Node " + std::to_string(nodeIndex + 1);
        if (sourceNode.parent) {
            const auto parent = nodeIndices.find(sourceNode.parent);
            if (parent == nodeIndices.end()) {
                error = {ImportErrorCode::InvalidHierarchy, "FBX node refers to a parent outside the scene."};
                return false;
            }
            if (parent->second == nodeIndex) {
                error = {ImportErrorCode::InvalidHierarchy, "FBX node refers to itself as its parent."};
                return false;
            }
            node.parentIndex = parent->second;
        }

        const auto& transform = sourceNode.local_transform;
        node.position = {ToFloat(transform.translation.x), ToFloat(transform.translation.y),
                         ToFloat(transform.translation.z)};
        node.rotation = {ToFloat(transform.rotation.x), ToFloat(transform.rotation.y),
                         ToFloat(transform.rotation.z), ToFloat(transform.rotation.w)};
        node.scale = {ToFloat(transform.scale.x), ToFloat(transform.scale.y),
                      ToFloat(transform.scale.z)};
        if (!Finite(node)) {
            error = {ImportErrorCode::InvalidGeometry, "FBX node contains a non-finite transform."};
            return false;
        }

        if (sourceNode.mesh) {
            const ufbx_mesh& sourceMesh = *sourceNode.mesh;
            if (!sourceMesh.vertex_position.exists || sourceMesh.num_faces != sourceMesh.faces.count ||
                (sourceMesh.face_material.count != 0 && sourceMesh.face_material.count != sourceMesh.num_faces)) {
                error = {ImportErrorCode::InvalidGeometry, "FBX mesh has invalid position, face, or material data."};
                return false;
            }

            std::unordered_map<MaterialId, std::uint32_t> meshIndices;
            for (std::size_t faceIndex = 0; faceIndex < sourceMesh.num_faces; ++faceIndex) {
                const ufbx_face face = sourceMesh.faces.data[faceIndex];
                if (face.num_indices < 3) continue;
                if (face.num_indices - 2 > limits_.maximumIndices / 3) {
                    error = {ImportErrorCode::ResourceLimit, "FBX polygon exceeds the configured index limit."};
                    return false;
                }
                const std::size_t triangleCapacity = (face.num_indices - 2) * 3;
                std::vector<std::uint32_t> triangleCorners(triangleCapacity);
                const std::uint32_t triangleCount = ufbx_triangulate_face(
                    triangleCorners.data(), triangleCorners.size(), &sourceMesh, face);
                if (triangleCount == 0 || triangleCount > triangleCapacity / 3) {
                    error = {ImportErrorCode::InvalidGeometry, "FBX polygon could not be triangulated."};
                    return false;
                }

                MaterialId materialId = 0;
                if (sourceMesh.face_material.count > 0) {
                    const std::uint32_t materialIndex = sourceMesh.face_material.data[faceIndex];
                    const auto* sourceMaterial = MaterialAt(sourceNode, sourceMesh, materialIndex);
                    if (!sourceMaterial) {
                        error = {ImportErrorCode::InvalidGeometry, "FBX face references an invalid material."};
                        return false;
                    }
                    const auto material = materialIds.find(sourceMaterial);
                    if (material == materialIds.end()) {
                        error = {ImportErrorCode::InvalidGeometry, "FBX face material is missing from the scene."};
                        return false;
                    }
                    materialId = material->second;
                }

                const std::size_t cornerCount = static_cast<std::size_t>(triangleCount) * 3;
                if (cornerCount > limits_.maximumVertices - std::min(totalVertices, limits_.maximumVertices) ||
                    cornerCount > limits_.maximumIndices - std::min(totalIndices, limits_.maximumIndices) ||
                    cornerCount > std::numeric_limits<std::uint32_t>::max() -
                        std::min(totalVertices, static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))) {
                    error = {ImportErrorCode::ResourceLimit, "FBX expanded geometry exceeds the configured vertex or index limit."};
                    return false;
                }

                auto meshIndex = meshIndices.find(materialId);
                if (meshIndex == meshIndices.end()) {
                    if (model.meshes.size() >= limits_.maximumMeshes ||
                        model.meshes.size() >= std::numeric_limits<std::uint32_t>::max()) {
                        error = {ImportErrorCode::ResourceLimit, "FBX mesh count exceeds the configured limit."};
                        return false;
                    }
                    ImportedMesh mesh;
                    mesh.name = ToString(sourceMesh.name, limits_.maximumNameBytes);
                    if (mesh.name.empty()) mesh.name = node.name;
                    mesh.materialId = materialId;
                    const auto newIndex = static_cast<std::uint32_t>(model.meshes.size());
                    model.meshes.push_back(std::move(mesh));
                    node.meshIndices.push_back(newIndex);
                    meshIndex = meshIndices.emplace(materialId, newIndex).first;
                }

                ImportedMesh& mesh = model.meshes[meshIndex->second];
                mesh.vertices.reserve(mesh.vertices.size() + cornerCount);
                mesh.indices.reserve(mesh.indices.size() + cornerCount);
                const ufbx_matrix normalTransform = ufbx_matrix_for_normals(&sourceNode.geometry_to_node);
                for (std::size_t corner = 0; corner < cornerCount; ++corner) {
                    const std::uint32_t vertexIndex = triangleCorners[corner];
                    if (vertexIndex >= sourceMesh.num_indices) {
                        error = {ImportErrorCode::InvalidGeometry, "FBX triangulation produced an out-of-range vertex index."};
                        return false;
                    }
                    const ufbx_vec3 sourcePosition = ufbx_get_vertex_vec3(
                        &sourceMesh.vertex_position, vertexIndex);
                    const ufbx_vec3 position = ufbx_transform_position(
                        &sourceNode.geometry_to_node, sourcePosition);
                    const ufbx_vec3 sourceNormal = sourceMesh.vertex_normal.exists
                        ? ufbx_get_vertex_vec3(&sourceMesh.vertex_normal, vertexIndex)
                        : ufbx_vec3{0.0, 1.0, 0.0};
                    const ufbx_vec3 normal = ufbx_transform_direction(&normalTransform, sourceNormal);
                    const ufbx_vec2 uv = sourceMesh.vertex_uv.exists
                        ? ufbx_get_vertex_vec2(&sourceMesh.vertex_uv, vertexIndex)
                        : ufbx_vec2{0.0, 0.0};
                    if (!Finite(position) || !Finite(normal) || !Finite(uv)) {
                        error = {ImportErrorCode::InvalidGeometry, "FBX contains a non-finite vertex attribute."};
                        return false;
                    }

                    const double normalLength = std::sqrt(
                        static_cast<double>(normal.x) * normal.x +
                        static_cast<double>(normal.y) * normal.y +
                        static_cast<double>(normal.z) * normal.z);
                    ImportedVertex vertex;
                    vertex.position = {ToFloat(position.x), ToFloat(position.y), ToFloat(position.z)};
                    if (normalLength > 1e-12 && std::isfinite(normalLength)) {
                        vertex.normal = {static_cast<float>(normal.x / normalLength),
                                         static_cast<float>(normal.y / normalLength),
                                         static_cast<float>(normal.z / normalLength)};
                    }
                    vertex.uv = {ToFloat(uv.x), 1.0f - ToFloat(uv.y)};
                    if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
                        !std::isfinite(vertex.position.z) || !std::isfinite(vertex.normal.x) ||
                        !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
                        !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) {
                        error = {ImportErrorCode::InvalidGeometry, "FBX vertex values exceed the supported numeric range."};
                        return false;
                    }
                    mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
                    mesh.vertices.push_back(vertex);
                }
                totalVertices += cornerCount;
                totalIndices += cornerCount;
            }
        }
        model.nodes.push_back(std::move(node));
    }

    if (model.meshes.empty()) {
        error = {ImportErrorCode::InvalidGeometry, "FBX contains no renderable triangle geometry."};
        return false;
    }
    output = std::move(model);
    return true;
}

} // namespace blubox::engine::assets
