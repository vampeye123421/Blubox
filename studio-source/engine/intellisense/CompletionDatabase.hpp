#pragma once

#include "CatalogTypes.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blubox::engine::intellisense {

class CompletionDatabase final {
public:
    static std::shared_ptr<CompletionDatabase> CreateBuiltin();

    // Catalog builder entry point. Call buildIndexes() after a batch load.
    void add(CompletionEntry entry);
    void mergeCatalogJson(std::string_view jsonText);
    std::vector<const CompletionEntry*> query(const CompletionContext& context) const;
    std::optional<std::string> resolveType(std::string_view expression,
                                           const SemanticSnapshot* snapshot) const;
    const CompletionEntry* findMember(std::string_view owner,
                                      std::string_view name) const;
    bool isDataType(std::string_view name) const;

    const std::vector<CompletionEntry>& entries() const noexcept { return entries_; }

private:
    void buildIndexes();
    void addBuiltin(std::string owner, std::string label, std::string insert,
                    CompletionKind kind, std::string detail = {}, bool isStatic = false,
                    std::string typeName = {}, int scoreBias = 0);

    std::vector<CompletionEntry> entries_;
    std::vector<std::uint32_t> globalEntries_;
    std::unordered_map<std::string, std::vector<std::uint32_t>> ownerEntries_;
    std::unordered_map<std::string, std::string> baseClasses_;
    std::unordered_map<std::string, std::string> rootTypes_;
    std::unordered_set<std::string> staticScopeNames_;
    std::unordered_map<std::uint64_t, std::uint32_t> idToIndex_;
    std::unordered_map<std::string, std::uint32_t> memberNames_;
};

class CompletionIndexBuilder final {
public:
    static std::shared_ptr<const CompletionDatabase> Build(
        std::string_view jsonText,
        const CompletionDatabase& base,
        std::string* error = nullptr);
};

} // namespace blubox::engine::intellisense
