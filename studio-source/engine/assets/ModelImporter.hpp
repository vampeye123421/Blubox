#pragma once

#include "ImportedModel.hpp"
#include "MeshCache.hpp"
#include "../EngineCore.hpp"

namespace blubox::engine::assets {

struct ImportedInstanceTree {
    InstanceHandle root{};
    std::vector<InstanceHandle> meshParts;
};

// Builds a DataModel Model / node / MeshPart tree. The registry stores only asset
// IDs; mesh CPU data remains in MeshCache and GPU buffers belong to render adapters.
bool CreateModelInstances(InstanceRegistry& world, MeshCache& meshes,
                          const ImportedModel& imported, InstanceHandle parent,
                          ImportedInstanceTree& output, std::string& error);

} // namespace blubox::engine::assets
