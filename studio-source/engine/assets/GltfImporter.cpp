#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "GltfImporter.hpp"
#include "TextureCache.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>

namespace blubox::engine::assets {
namespace {
struct DataDeleter { void operator()(cgltf_data* data) const noexcept { if (data) cgltf_free(data); } };

bool SafeUri(const std::filesystem::path& rootPath, const char* uri, std::size_t maxBytes,
             std::filesystem::path& resolved, ImportError& error) {
    if (!uri || !*uri) { error = {ImportErrorCode::MissingBuffers, "glTF external resource URI is empty."}; return false; }
    const std::string text(uri);
    if (text.starts_with("data:") || text.find(':') != std::string::npos || text.find('\0') != std::string::npos) {
        error = {ImportErrorCode::UnsafeReference, "Embedded data URIs and URI schemes are not accepted for external glTF resources."}; return false;
    }
    const std::filesystem::path relative(text);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        error = {ImportErrorCode::UnsafeReference, "glTF resources cannot use absolute paths."}; return false;
    }
    for (const auto& part : relative) if (part == "..") {
        error = {ImportErrorCode::UnsafeReference, "glTF resource paths cannot traverse outside the model folder."}; return false;
    }
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(rootPath, ec);
    if (ec) { error = {ImportErrorCode::FileUnavailable, "Could not resolve glTF model folder."}; return false; }
    resolved = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec) { error = {ImportErrorCode::FileUnavailable, "Could not resolve glTF resource."}; return false; }
    auto a = root.begin(), b = resolved.begin();
    for (; a != root.end() && b != resolved.end(); ++a, ++b) if (*a != *b) {
        error = {ImportErrorCode::UnsafeReference, "glTF resource resolves outside the model folder."}; return false;
    }
    if (a != root.end()) { error = {ImportErrorCode::UnsafeReference, "glTF resource resolves outside the model folder."}; return false; }
    const auto size = std::filesystem::file_size(resolved, ec);
    if (ec || size == 0 || size > maxBytes) { error = {ImportErrorCode::ResourceLimit, "glTF external resource is missing, empty, or exceeds the size limit."}; return false; }
    return true;
}

template<typename T> bool FiniteRange(const T* values, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) if (!std::isfinite(static_cast<double>(values[i]))) return false;
    return true;
}

std::size_t NodeIndex(const cgltf_data* data, const cgltf_node* node) {
    if (!node || node < data->nodes || node >= data->nodes + data->nodes_count) return SIZE_MAX;
    return static_cast<std::size_t>(node - data->nodes);
}

bool DecomposeNode(const cgltf_node& node, float translation[3], float rotation[4], float scale[3]) {
    if (!node.has_matrix) {
        if (node.has_translation) std::copy_n(node.translation, 3, translation);
        if (node.has_rotation) std::copy_n(node.rotation, 4, rotation);
        if (node.has_scale) std::copy_n(node.scale, 3, scale);
        return true;
    }
    const float* m = node.matrix;
    translation[0] = m[12]; translation[1] = m[13]; translation[2] = m[14];
    scale[0] = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
    scale[1] = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
    scale[2] = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
    if (scale[0] < 1e-8f || scale[1] < 1e-8f || scale[2] < 1e-8f) return false;
    const float r00 = m[0] / scale[0], r01 = m[4] / scale[1], r02 = m[8] / scale[2];
    const float r10 = m[1] / scale[0], r11 = m[5] / scale[1], r12 = m[9] / scale[2];
    const float r20 = m[2] / scale[0], r21 = m[6] / scale[1], r22 = m[10] / scale[2];
    const float trace = r00 + r11 + r22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        rotation[3] = 0.25f * s; rotation[0] = (r21 - r12) / s;
        rotation[1] = (r02 - r20) / s; rotation[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        rotation[3] = (r21 - r12) / s; rotation[0] = 0.25f * s;
        rotation[1] = (r01 + r10) / s; rotation[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        rotation[3] = (r02 - r20) / s; rotation[0] = (r01 + r10) / s;
        rotation[1] = 0.25f * s; rotation[2] = (r12 + r21) / s;
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        rotation[3] = (r10 - r01) / s; rotation[0] = (r02 + r20) / s;
        rotation[1] = (r12 + r21) / s; rotation[2] = 0.25f * s;
    }
    return true;
}
}

GltfImporter::GltfImporter(TextureCache* textures, ImportLimits limits) : textures_(textures), limits_(limits) {}

bool GltfImporter::import(const std::filesystem::path& input, ImportedModel& output, ImportError& error) const {
    error = {};
    const auto extension = input.extension().string();
    if (extension != ".gltf" && extension != ".glTF" && extension != ".glb" && extension != ".GLB") {
        error = {ImportErrorCode::UnsupportedFormat, "glTF importer accepts .gltf and .glb files only."}; return false;
    }
    std::error_code ec;
    const auto file = std::filesystem::weakly_canonical(input, ec);
    if (ec || !std::filesystem::is_regular_file(file, ec)) {
        error = {ImportErrorCode::FileUnavailable, "glTF source file is not a readable regular file."}; return false;
    }
    const auto bytes = std::filesystem::file_size(file, ec);
    if (ec || bytes == 0) { error = {ImportErrorCode::InvalidFile, "glTF source file is empty or unreadable."}; return false; }
    if (bytes > limits_.maximumFileBytes) { error = {ImportErrorCode::FileTooLarge, "glTF source exceeds the configured size limit."}; return false; }

    cgltf_options options{};
    cgltf_data* raw = nullptr;
    cgltf_result result = cgltf_parse_file(&options, file.string().c_str(), &raw);
    std::unique_ptr<cgltf_data, DataDeleter> data(raw);
    if (result != cgltf_result_success || !data) {
        error = {ImportErrorCode::InvalidFile, "cgltf could not parse the source document."}; return false;
    }
    if (data->nodes_count > limits_.maximumNodes || data->meshes_count > limits_.maximumMeshes ||
        data->materials_count > limits_.maximumMaterials) {
        error = {ImportErrorCode::ResourceLimit, "glTF node, mesh, or material count exceeds the configured limit."}; return false;
    }

    // Validate every external buffer before letting cgltf open it.
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        const cgltf_buffer& buffer = data->buffers[i];
        if (buffer.size > limits_.maximumExternalFileBytes) {
            error = {ImportErrorCode::ResourceLimit, "glTF buffer exceeds the configured size limit."}; return false;
        }
        if (buffer.uri) {
            std::filesystem::path resolved;
            if (!SafeUri(file.parent_path(), buffer.uri, limits_.maximumExternalFileBytes, resolved, error)) return false;
        }
    }
    result = cgltf_load_buffers(&options, data.get(), file.string().c_str());
    if (result != cgltf_result_success) {
        error = {ImportErrorCode::MissingBuffers, "glTF buffer data could not be loaded."}; return false;
    }
    if (cgltf_validate(data.get()) != cgltf_result_success) {
        error = {ImportErrorCode::InvalidFile, "glTF validation failed."}; return false;
    }

    ImportedModel model;
    model.sourcePath = file;
    model.sourceBytes = static_cast<std::size_t>(bytes);
    model.materials.reserve(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& source = data->materials[i];
        MaterialResource material;
        material.id = static_cast<MaterialId>(i + 1);
        material.name = source.name ? std::string(source.name).substr(0, limits_.maximumNameBytes) : "Material " + std::to_string(i + 1);
        if (source.has_pbr_metallic_roughness) {
            const auto& pbr = source.pbr_metallic_roughness;
            for (int c = 0; c < 4; ++c) material.properties.baseColor[static_cast<std::size_t>(c)] =
                static_cast<std::uint8_t>(std::clamp(pbr.base_color_factor[c] * 255.0f, 0.0f, 255.0f));
            material.properties.metalness = std::clamp(pbr.metallic_factor, 0.0f, 1.0f);
            material.properties.roughness = std::clamp(pbr.roughness_factor, 0.0f, 1.0f);
            const cgltf_texture_view* views[] = {&pbr.base_color_texture, &pbr.metallic_roughness_texture};
            for (const cgltf_texture_view* view : views) {
                if (!view->texture || !view->texture->image || !view->texture->image->uri || !textures_) continue;
                std::filesystem::path texturePath;
                if (!SafeUri(file.parent_path(), view->texture->image->uri, limits_.maximumExternalFileBytes, texturePath, error)) return false;
                const TextureId texture = textures_->acquire(texturePath, &error.message);
                if (!texture) { error.code = ImportErrorCode::FileUnavailable; return false; }
                if (view == views[0]) material.albedoMap.assetId = texture;
                else material.roughnessMap.assetId = material.metalnessMap.assetId = texture;
            }
        }
        if (source.normal_texture.texture && source.normal_texture.texture->image && source.normal_texture.texture->image->uri && textures_) {
            std::filesystem::path texturePath;
            if (!SafeUri(file.parent_path(), source.normal_texture.texture->image->uri, limits_.maximumExternalFileBytes, texturePath, error)) return false;
            const TextureId texture = textures_->acquire(texturePath, &error.message);
            if (!texture) { error.code = ImportErrorCode::FileUnavailable; return false; }
            material.normalMap.assetId = texture;
            material.properties.normalScale = source.normal_texture.scale;
        }
        model.materials.push_back(std::move(material));
    }

    std::size_t totalVertices = 0, totalIndices = 0;
    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const cgltf_node& source = data->nodes[nodeIndex];
        ImportedNode node;
        node.name = source.name ? std::string(source.name).substr(0, limits_.maximumNameBytes) : "Node " + std::to_string(nodeIndex + 1);
        const std::size_t parentIndex = NodeIndex(data.get(), source.parent);
        if (parentIndex != SIZE_MAX) node.parentIndex = static_cast<std::uint32_t>(parentIndex);
        float translation[3]{}, rotation[4]{0.0f, 0.0f, 0.0f, 1.0f}, scale[3]{1.0f, 1.0f, 1.0f};
        if (!DecomposeNode(source, translation, rotation, scale) || !FiniteRange(translation, 3) || !FiniteRange(rotation, 4) || !FiniteRange(scale, 3)) {
            error = {ImportErrorCode::InvalidGeometry, "glTF node has a non-finite transform."}; return false;
        }
        node.position = {translation[0], translation[1], translation[2]};
        node.rotation = {rotation[0], rotation[1], rotation[2], rotation[3]};
        node.scale = {scale[0], scale[1], scale[2]};
        node.childIndices.reserve(source.children_count);
        for (cgltf_size child = 0; child < source.children_count; ++child) {
            const auto childIndex = NodeIndex(data.get(), source.children[child]);
            if (childIndex == SIZE_MAX || childIndex >= data->nodes_count) { error = {ImportErrorCode::InvalidHierarchy, "glTF contains an invalid child node reference."}; return false; }
            node.childIndices.push_back(static_cast<std::uint32_t>(childIndex));
        }

        if (source.mesh) {
            for (cgltf_size primitiveIndex = 0; primitiveIndex < source.mesh->primitives_count; ++primitiveIndex) {
                const cgltf_primitive& primitive = source.mesh->primitives[primitiveIndex];
                if (primitive.type != cgltf_primitive_type_triangles) continue;
                const cgltf_accessor* positions = nullptr;
                const cgltf_accessor* normals = nullptr;
                const cgltf_accessor* texcoords = nullptr;
                for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                    const auto& attribute = primitive.attributes[a];
                    if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
                    else if (attribute.type == cgltf_attribute_type_normal) normals = attribute.data;
                    else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) texcoords = attribute.data;
                }
                if (!positions || positions->count == 0 || positions->count > limits_.maximumVertices - std::min(totalVertices, limits_.maximumVertices)) {
                    error = {ImportErrorCode::ResourceLimit, "glTF position accessor is missing or exceeds the vertex limit."}; return false;
                }
                ImportedMesh mesh;
                mesh.name = source.mesh->name ? std::string(source.mesh->name).substr(0, limits_.maximumNameBytes) : node.name;
                if (primitive.material) {
                    const std::size_t materialIndex = static_cast<std::size_t>(primitive.material - data->materials);
                    if (materialIndex >= data->materials_count) { error = {ImportErrorCode::InvalidGeometry, "glTF primitive references an invalid material."}; return false; }
                    mesh.materialId = static_cast<MaterialId>(materialIndex + 1);
                }
                mesh.vertices.resize(positions->count);
                float value[4]{};
                for (cgltf_size vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex) {
                    if (!cgltf_accessor_read_float(positions, vertexIndex, value, 3) || !FiniteRange(value, 3)) {
                        error = {ImportErrorCode::InvalidGeometry, "glTF position data is invalid."}; return false;
                    }
                    auto& vertex = mesh.vertices[vertexIndex];
                    vertex.position = {value[0], value[1], value[2]};
                    if (normals && (vertexIndex >= normals->count || !cgltf_accessor_read_float(normals, vertexIndex, value, 3) || !FiniteRange(value, 3))) {
                        error = {ImportErrorCode::InvalidGeometry, "glTF normal accessor is invalid."}; return false;
                    }
                    if (normals) vertex.normal = {value[0], value[1], value[2]};
                    if (texcoords && (vertexIndex >= texcoords->count || !cgltf_accessor_read_float(texcoords, vertexIndex, value, 2) || !FiniteRange(value, 2))) {
                        error = {ImportErrorCode::InvalidGeometry, "glTF UV accessor is invalid."}; return false;
                    }
                    if (texcoords) vertex.uv = {value[0], 1.0f - value[1]};
                }
                if (primitive.indices) {
                    if (primitive.indices->count % 3 != 0 || primitive.indices->count > limits_.maximumIndices - std::min(totalIndices, limits_.maximumIndices)) {
                        error = {ImportErrorCode::ResourceLimit, "glTF index count is invalid or exceeds the configured limit."}; return false;
                    }
                    mesh.indices.reserve(primitive.indices->count);
                    for (cgltf_size index = 0; index < primitive.indices->count; ++index) {
                        const cgltf_size valueIndex = cgltf_accessor_read_index(primitive.indices, index);
                        if (valueIndex >= mesh.vertices.size()) { error = {ImportErrorCode::InvalidGeometry, "glTF index is outside the position accessor."}; return false; }
                        mesh.indices.push_back(static_cast<std::uint32_t>(valueIndex));
                    }
                } else {
                    if (mesh.vertices.size() % 3 != 0) { error = {ImportErrorCode::InvalidGeometry, "Non-indexed glTF triangle vertices are not divisible by three."}; return false; }
                    mesh.indices.resize(mesh.vertices.size());
                    for (std::size_t index = 0; index < mesh.indices.size(); ++index) mesh.indices[index] = static_cast<std::uint32_t>(index);
                }
                totalVertices += mesh.vertices.size(); totalIndices += mesh.indices.size();
                const auto meshIndex = static_cast<std::uint32_t>(model.meshes.size());
                model.meshes.push_back(std::move(mesh));
                node.meshIndices.push_back(meshIndex);
            }
        }
        model.nodes.push_back(std::move(node));
    }
    if (model.meshes.empty()) { error = {ImportErrorCode::InvalidGeometry, "glTF contains no triangle meshes."}; return false; }
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        if (model.nodes[i].parentIndex != UINT32_MAX && model.nodes[i].parentIndex >= model.nodes.size()) {
            error = {ImportErrorCode::InvalidHierarchy, "glTF parent index is outside the node table."}; return false;
        }
    }
    output = std::move(model);
    return true;
}

} // namespace blubox::engine::assets
