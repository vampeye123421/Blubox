#pragma once

#include "ImportedModel.hpp"

namespace blubox::engine::assets {

class FbxImporter {
public:
    explicit FbxImporter(ImportLimits limits = {});
    bool import(const std::filesystem::path& file, ImportedModel& output, ImportError& error) const;

private:
    ImportLimits limits_{};
};

} // namespace blubox::engine::assets
