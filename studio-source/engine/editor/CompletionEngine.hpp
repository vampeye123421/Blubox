#pragma once

#include "../intellisense/CatalogTypes.hpp"
#include "../intellisense/CompletionDatabase.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace blubox::engine::editor {

using CompletionKind = intellisense::CompletionKind;

struct CompletionItem {
    std::uint64_t id = 0;
    std::string label;
    std::string insertion;
    std::string detail;
    std::string kind;
    bool deprecated = false;
    int score = 0;
};

struct CompletionRequest {
    std::string source;
    int line = 0;
    int column = 0;
};

struct CompletionResponse {
    std::vector<CompletionItem> items;
    int line = 0;
    int startColumn = 0;
    int endColumn = 0;
    std::string query;
    std::string receiverType;
    int contextKind = 0;
    bool active = false;
};

class CompletionEngine final {
public:
    CompletionEngine();
    ~CompletionEngine();
    CompletionEngine(const CompletionEngine&) = delete;
    CompletionEngine& operator=(const CompletionEngine&) = delete;

    // Filtering and ranking are synchronous over immutable, pre-indexed data.
    // This call does no source parsing and does not wait for the worker.
    CompletionResponse completeLine(std::string_view line, int lineNumber,
                                    int cursorColumn, std::string_view scriptId,
                                    bool explicitInvocation = false) const;

    // Expensive source analysis and catalog loading are debounced/off-thread.
    void requestSemanticRefresh(std::string scriptId, std::uint64_t generation,
                                std::string source);
    void requestCatalogLoad(std::filesystem::path path);
    std::shared_ptr<const intellisense::SemanticSnapshot> semanticSnapshot(std::string_view scriptId) const;
    void setWorkspaceChildren(std::vector<intellisense::WorkspaceChild> children);
    std::uint64_t candidateGeneration() const noexcept { return candidateGeneration_.load(std::memory_order_acquire); }

    std::string lastCatalogError() const;

private:
    struct TransparentStringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };
    struct TransparentStringEqual {
        using is_transparent = void;
        bool operator()(std::string_view left, std::string_view right) const noexcept {
            return left == right;
        }
    };
    using SemanticSnapshotMap =
        std::unordered_map<std::string, std::shared_ptr<const intellisense::SemanticSnapshot>,
                           TransparentStringHash, TransparentStringEqual>;

    struct SemanticJob {
        std::string scriptId;
        std::uint64_t generation = 0;
        std::string source;
        std::chrono::steady_clock::time_point due;
    };

    void run(std::stop_token stop);
    void analyzeSource(SemanticJob job);

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::unordered_map<std::string, SemanticJob> pendingSemantic_;
    std::atomic<std::shared_ptr<const SemanticSnapshotMap>> semanticSnapshots_;
    std::filesystem::path pendingCatalogPath_;
    bool catalogLoadPending_ = false;
    std::uint64_t pendingRevision_ = 0;
    std::string catalogError_;
    std::atomic<std::shared_ptr<const intellisense::CompletionDatabase>> database_;
    std::atomic<std::shared_ptr<const std::vector<intellisense::WorkspaceChild>>> workspaceChildren_;
    std::atomic<std::uint64_t> candidateGeneration_{1};
    std::jthread worker_;
};

} // namespace blubox::engine::editor
