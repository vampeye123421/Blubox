#pragma once

#include "../assets/MaterialResource.hpp"
#include "../assets/TextureCache.hpp"

#include <raylib.h>

namespace blubox::engine::render {

class RaylibMaterialAdapter {
public:
    explicit RaylibMaterialAdapter(assets::TextureCache& textures) : textures_(textures) {}
    bool apply(Material& target, const assets::MaterialResource& source) const;
    Material create(const assets::MaterialResource& source) const;
private:
    assets::TextureCache& textures_;
};

} // namespace blubox::engine::render
