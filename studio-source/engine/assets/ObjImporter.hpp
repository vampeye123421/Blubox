#pragma once

#include "ImportedModel.hpp"

namespace blubox::engine::assets {

class TextureCache;

class ObjImporter {
public:
    explicit ObjImporter(TextureCache* textures = nullptr, ImportLimits limits = {});
    bool import(const std::filesystem::path& file, ImportedModel& output, ImportError& error) const;
private:
    TextureCache* textures_ = nullptr;
    ImportLimits limits_{};
};

} // namespace blubox::engine::assets
