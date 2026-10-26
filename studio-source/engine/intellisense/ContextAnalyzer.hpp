#pragma once

#include "CatalogTypes.hpp"

#include <string_view>

namespace blubox::engine::intellisense {

class CompletionDatabase;

class ContextAnalyzer final {
public:
    static CompletionContext Analyze(std::string_view line, int caretColumn,
                                     const CompletionDatabase& database,
                                     const SemanticSnapshot* semantic,
                                     bool explicitInvocation = false);
};

} // namespace blubox::engine::intellisense
