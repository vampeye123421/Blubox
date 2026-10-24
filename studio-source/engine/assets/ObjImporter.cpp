#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "ObjImporter.hpp"
#include "TextureCache.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <numeric>
#include <unordered_map>

namespace blubox::engine::assets {
namespace {
bool ReadLimited(const std::filesystem::path& path, std::size_t maxBytes, std::string& contents, ImportError& error) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        error = {ImportErrorCode::FileUnavailable, "Import file is not a regular file."}; return false;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0) { error = {ImportErrorCode::InvalidFile, "Import file is empty or unreadable."}; return false; }
    if (size > maxBytes) { error = {ImportErrorCode::FileTooLarge, "Import file exceeds the configured size limit."}; return false; }
    std::ifstream stream(path, std::ios::binary);
    contents.assign(std::istreambuf_iterator<char>(stream), {});
    if (!stream.eof() || contents.size() != size) { error = {ImportErrorCode::FileUnavailable, "Could not read the complete import file."}; return false; }
    return true;
}

bool SafeRelativeReference(const std::filesystem::path& base, const std::string& reference,
                           std::size_t maxBytes, std::filesystem::path& resolved, ImportError& error) {
    const std::filesystem::path relative(reference);
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        error = {ImportErrorCode::UnsafeReference, "Model contains an absolute or empty external resource path."}; return false;
    }
    for (const auto& part : relative) if (part == "..") {
        error = {ImportErrorCode::UnsafeReference, "Model resource paths cannot traverse outside the model folder."}; return false;
    }
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(base, ec);
    if (ec) { error = {ImportErrorCode::FileUnavailable, "Could not resolve model folder."}; return false; }
    resolved = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec) { error = {ImportErrorCode::FileUnavailable, "Could not resolve referenced resource."}; return false; }
    auto rootIt = root.begin(), resolvedIt = resolved.begin();
    for (; rootIt != root.end() && resolvedIt != resolved.end(); ++rootIt, ++resolvedIt)
        if (*rootIt != *resolvedIt) { error = {ImportErrorCode::UnsafeReference, "Referenced resource resolves outside the model folder."}; return false; }
    if (rootIt != root.end()) { error = {ImportErrorCode::UnsafeReference, "Referenced resource resolves outside the model folder."}; return false; }
    const auto size = std::filesystem::file_size(resolved, ec);
    if (ec || size == 0 || size > maxBytes) { error = {ImportErrorCode::ResourceLimit, "Referenced resource is missing, empty, or too large."}; return false; }
    return true;
}

bool ValidIndex(int index, std::size_t count) { return index >= 0 && static_cast<std::size_t>(index) < count; }
}

ObjImporter::ObjImporter(TextureCache* textures, ImportLimits limits) : textures_(textures), limits_(limits) {}

bool ObjImporter::import(const std::filesystem::path& file, ImportedModel& output, ImportError& error) const {
    error = {};
    const auto extension = file.extension().string();
    if (extension != ".obj" && extension != ".OBJ") {
        error = {ImportErrorCode::UnsupportedFormat, "OBJ importer accepts .obj files only."}; return false;
    }
    std::string objContents;
    if (!ReadLimited(file, limits_.maximumFileBytes, objContents, error)) return false;

    // Parse text in memory. Referenced MTL data is explicitly read from a safe,
    // in-folder path rather than allowing the parser to follow arbitrary paths.
    std::string mtlContents;
    std::istringstream objLines(objContents);
    std::string line;
    while (std::getline(objLines, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 7, "mtllib ") != 0) continue;
        const std::string mtlName = line.substr(first + 7);
        if (mtlName.find_first_of("\r\n\0") != std::string::npos || mtlName.size() > limits_.maximumNameBytes) {
            error = {ImportErrorCode::UnsafeReference, "OBJ material library name is invalid."}; return false;
        }
        std::filesystem::path mtlPath;
        if (!SafeRelativeReference(file.parent_path(), mtlName, limits_.maximumExternalFileBytes, mtlPath, error)) return false;
        if (!ReadLimited(mtlPath, limits_.maximumExternalFileBytes, mtlContents, error)) return false;
        break;
    }

    tinyobj::ObjReaderConfig config;
    config.triangulate = true;
    config.vertex_color = false;
    tinyobj::ObjReader reader;
    if (!reader.ParseFromString(objContents, mtlContents, config)) {
        error = {ImportErrorCode::InvalidFile, reader.Error().empty() ? "OBJ parse failed." : reader.Error()}; return false;
    }
    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();
    const auto& sourceMaterials = reader.GetMaterials();
    if (attrib.vertices.size() / 3 > limits_.maximumVertices || sourceMaterials.size() > limits_.maximumMaterials) {
        error = {ImportErrorCode::ResourceLimit, "OBJ vertex or material count exceeds the configured limit."}; return false;
    }

    ImportedModel result;
    result.sourcePath = std::filesystem::weakly_canonical(file);
    result.sourceBytes = objContents.size() + mtlContents.size();
    result.materials.reserve(sourceMaterials.size());
    for (const auto& source : sourceMaterials) {
        MaterialResource material;
        material.id = static_cast<MaterialId>(result.materials.size() + 1);
        material.name = source.name.substr(0, limits_.maximumNameBytes);
        material.properties.baseColor = {
            static_cast<std::uint8_t>(std::clamp(source.diffuse[0] * 255.0f, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(source.diffuse[1] * 255.0f, 0.0f, 255.0f)),
            static_cast<std::uint8_t>(std::clamp(source.diffuse[2] * 255.0f, 0.0f, 255.0f)), 255};
        material.properties.metalness = std::clamp(source.metallic, 0.0f, 1.0f);
        material.properties.roughness = std::clamp(source.roughness, 0.0f, 1.0f);
        if (!source.diffuse_texname.empty() && textures_) {
            std::filesystem::path texturePath;
            if (!SafeRelativeReference(file.parent_path(), source.diffuse_texname, limits_.maximumExternalFileBytes, texturePath, error)) return false;
            const TextureId texture = textures_->acquire(texturePath, &error.message);
            if (!texture) { error.code = ImportErrorCode::FileUnavailable; return false; }
            material.albedoMap.assetId = texture;
        }
        result.materials.push_back(std::move(material));
    }

    std::size_t vertexCount = 0, indexCount = 0;
    for (const auto& shape : shapes) {
        const auto faceCount = shape.mesh.num_face_vertices.size();
        for (std::size_t face = 0; face < faceCount; ++face) {
            if (shape.mesh.num_face_vertices[face] != 3) { error = {ImportErrorCode::InvalidGeometry, "OBJ triangulation produced a non-triangle face."}; return false; }
            const int materialIndex = face < shape.mesh.material_ids.size() ? shape.mesh.material_ids[face] : -1;
            auto meshIt = std::find_if(result.meshes.begin(), result.meshes.end(), [&](const ImportedMesh& mesh) {
                return mesh.name == shape.name && mesh.materialId == (materialIndex >= 0 ? static_cast<MaterialId>(materialIndex + 1) : 0);
            });
            if (meshIt == result.meshes.end()) {
                if (result.meshes.size() >= limits_.maximumMeshes) { error = {ImportErrorCode::ResourceLimit, "OBJ mesh count exceeds the configured limit."}; return false; }
                ImportedMesh mesh; mesh.name = shape.name.substr(0, limits_.maximumNameBytes);
                mesh.materialId = materialIndex >= 0 ? static_cast<MaterialId>(materialIndex + 1) : 0;
                result.meshes.push_back(std::move(mesh)); meshIt = std::prev(result.meshes.end());
            }
            const std::size_t offset = shape.mesh.num_face_vertices.begin() == shape.mesh.num_face_vertices.end() ? 0 :
                static_cast<std::size_t>(std::accumulate(shape.mesh.num_face_vertices.begin(), shape.mesh.num_face_vertices.begin() + static_cast<std::ptrdiff_t>(face), 0));
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const auto& index = shape.mesh.indices[offset + corner];
                if (!ValidIndex(index.vertex_index, attrib.vertices.size() / 3) ||
                    (index.normal_index >= 0 && !ValidIndex(index.normal_index, attrib.normals.size() / 3)) ||
                    (index.texcoord_index >= 0 && !ValidIndex(index.texcoord_index, attrib.texcoords.size() / 2))) {
                    error = {ImportErrorCode::InvalidGeometry, "OBJ contains an out-of-range vertex, normal, or UV index."}; return false;
                }
                ImportedVertex vertex;
                vertex.position = {attrib.vertices[3 * index.vertex_index], attrib.vertices[3 * index.vertex_index + 1], attrib.vertices[3 * index.vertex_index + 2]};
                if (index.normal_index >= 0) vertex.normal = {attrib.normals[3 * index.normal_index], attrib.normals[3 * index.normal_index + 1], attrib.normals[3 * index.normal_index + 2]};
                if (index.texcoord_index >= 0) vertex.uv = {attrib.texcoords[2 * index.texcoord_index], 1.0f - attrib.texcoords[2 * index.texcoord_index + 1]};
                if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) || !std::isfinite(vertex.position.z) ||
                    !std::isfinite(vertex.normal.x) || !std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
                    !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y)) {
                    error = {ImportErrorCode::InvalidGeometry, "OBJ contains a non-finite vertex attribute."}; return false;
                }
                meshIt->indices.push_back(static_cast<std::uint32_t>(meshIt->vertices.size()));
                meshIt->vertices.push_back(vertex);
            }
            vertexCount += 3; indexCount += 3;
            if (vertexCount > limits_.maximumVertices || indexCount > limits_.maximumIndices) {
                error = {ImportErrorCode::ResourceLimit, "OBJ expanded vertex or index count exceeds the configured limit."}; return false;
            }
        }
    }
    for (const auto& mesh : result.meshes) if (mesh.vertices.empty() || mesh.indices.empty()) {
        error = {ImportErrorCode::InvalidGeometry, "OBJ has no renderable triangle geometry."}; return false;
    }
    result.nodes.push_back({"Root", UINT32_MAX});
    for (std::uint32_t i = 0; i < result.meshes.size(); ++i) result.nodes.front().meshIndices.push_back(i);
    output = std::move(result);
    return true;
}

} // namespace blubox::engine::assets
