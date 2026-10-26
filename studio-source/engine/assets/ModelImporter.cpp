#include "ModelImporter.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace blubox::engine::assets {
namespace {
Vec3 QuaternionToEuler(const std::array<float, 4>& q) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float sinr = 2.0f * (w * x + y * z);
    const float cosr = 1.0f - 2.0f * (x * x + y * y);
    const float sinp = 2.0f * (w * y - z * x);
    const float siny = 2.0f * (w * z + x * y);
    const float cosy = 1.0f - 2.0f * (y * y + z * z);
    return {std::atan2(sinr, cosr), std::asin(std::clamp(sinp, -1.0f, 1.0f)), std::atan2(siny, cosy)};
}
}

bool CreateModelInstances(InstanceRegistry& world, MeshCache& meshes, const ImportedModel& imported,
                          InstanceHandle parent, ImportedInstanceTree& output, std::string& error) {
    error.clear();
    if (imported.meshes.empty() || imported.nodes.empty() || (parent && !world.alive(parent))) {
        error = "Imported model is empty or the destination parent is invalid."; return false;
    }
    for (const auto& node : imported.nodes) {
        if (node.parentIndex != UINT32_MAX && node.parentIndex >= imported.nodes.size()) {
            error = "Imported model contains an invalid node parent."; return false;
        }
        for (auto meshIndex : node.meshIndices) if (meshIndex >= imported.meshes.size()) {
            error = "Imported model contains an invalid mesh reference."; return false;
        }
    }
    const InstanceHandle root = world.create(imported.sourcePath.stem().string().empty() ? "Imported Model" : imported.sourcePath.stem().string(), ClassId::Model, parent);
    if (!root) { error = "Could not create a model instance."; return false; }
    std::vector<InstanceHandle> nodes(imported.nodes.size());
    for (std::size_t i = 0; i < imported.nodes.size(); ++i) {
        const auto& node = imported.nodes[i];
        const InstanceHandle nodeParent = node.parentIndex == UINT32_MAX ? root : nodes[node.parentIndex];
        const auto name = node.name.empty() ? "Node " + std::to_string(i + 1) : node.name;
        nodes[i] = world.create(name, ClassId::Model, nodeParent);
        if (!nodes[i]) { world.destroy(root); error = "Could not create imported node hierarchy."; return false; }
        if (auto* record = world.get(nodes[i])) {
            record->local.position = node.position;
            record->local.rotation = QuaternionToEuler(node.rotation);
            record->local.scale = node.scale;
            world.markTransformDirty(nodes[i]);
        }
    }
    std::vector<InstanceHandle> meshParts;
    for (std::size_t nodeIndex = 0; nodeIndex < imported.nodes.size(); ++nodeIndex) {
        for (std::uint32_t meshIndex : imported.nodes[nodeIndex].meshIndices) {
            const auto& source = imported.meshes[meshIndex];
            const MeshAssetId asset = meshes.insert(source);
            if (!asset) { world.destroy(root); error = "Could not cache imported mesh data."; return false; }
            const std::string name = source.name.empty() ? "MeshPart" : source.name;
            const InstanceHandle part = world.create(name, ClassId::MeshPart, nodes[nodeIndex]);
            if (!part) { meshes.release(asset); world.destroy(root); error = "Could not create an imported MeshPart."; return false; }
            if (auto* record = world.get(part)) {
                record->meshAssetId = asset;
                record->materialId = source.materialId;
                record->color = {255, 255, 255, 255};
            }
            meshParts.push_back(part);
        }
    }
    world.flushTransforms();
    output = {root, std::move(meshParts)};
    return true;
}

} // namespace blubox::engine::assets
