#pragma once

#include "../reflection/ReflectionRegistry.hpp"
#include "../../studio/MapDocument.hpp"

#include <cstdint>

namespace blubox::engine::editor {

class PropertiesPanel {
public:
    explicit PropertiesPanel(const reflection::ReflectionRegistry& reflection) : reflection_(reflection) {}
    // Draws property editors from class metadata and writes edits back to the map
    // document immediately. Returns the merged engine change flags for the caller.
    std::uint32_t draw(studio::Entity& entity) const;

private:
    const reflection::ReflectionRegistry& reflection_;
};

} // namespace blubox::engine::editor
