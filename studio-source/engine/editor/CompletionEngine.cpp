#include "CompletionEngine.hpp"

#include "../intellisense/ContextAnalyzer.hpp"
#include "../intellisense/FuzzyMatcher.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace blubox::engine::editor {
namespace {
using namespace intellisense;

std::string Lower(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char c : value) result.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    return result;
}

std::string KindName(CompletionKind kind) {
    switch (kind) {
        case CompletionKind::Global: return "Global";
        case CompletionKind::Library: return "Library";
        case CompletionKind::DataType: return "Data type";
        case CompletionKind::Enum: return "Enum";
        case CompletionKind::EnumItem: return "Enum item";
        case CompletionKind::Class: return "Class";
        case CompletionKind::Service: return "Service";
        case CompletionKind::Keyword: return "Keyword";
        case CompletionKind::Local: return "Local";
        case CompletionKind::Property: return "Property";
        case CompletionKind::Method: return "Method";
        case CompletionKind::Event: return "Event";
        case CompletionKind::Callback: return "Callback";
        case CompletionKind::Constructor: return "Constructor";
        case CompletionKind::Constant: return "Constant";
        case CompletionKind::SignalMethod: return "Signal";
        default: return "Luau symbol";
    }
}

std::uint64_t DynamicId(std::string_view scope, std::string_view label) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : scope) { hash ^= c; hash *= 1099511628211ull; }
    hash ^= 0xff; hash *= 1099511628211ull;
    for (unsigned char c : label) { hash ^= c; hash *= 1099511628211ull; }
    return hash == 0 ? 1 : hash;
}

std::string InferExpressionType(std::string_view expression, std::string_view variableName,
                                const CompletionDatabase& database,
                                const SemanticSnapshot& snapshot) {
    static const std::regex instanceNew(R"(Instance\s*\.\s*new\s*\(\s*[\"']([A-Za-z_][A-Za-z0-9_]*)[\"'])");
    static const std::regex getService(R"(GetService\s*\(\s*[\"']([A-Za-z_][A-Za-z0-9_]*)[\"'])");
    const std::string value(expression);
    std::smatch match;
    if (std::regex_search(value, match, instanceNew)) return match[1].str();
    if (std::regex_search(value, match, getService)) return match[1].str();
    if (value.find("Color3.") != std::string::npos) return "Color3";
    if (value.find("Vector3.") != std::string::npos) return "Vector3";
    if (value.find("Vector2.") != std::string::npos) return "Vector2";
    if (value.find("CFrame.") != std::string::npos) return "CFrame";
    if (value.find("Humanoid") != std::string::npos) return "Humanoid";
    if (value.find(".Touched") != std::string::npos || value.find(".Changed") != std::string::npos ||
        value.find(".AncestryChanged") != std::string::npos || value.find(".Activated") != std::string::npos)
        return "RBXScriptSignal";
    if (value.find("Character") != std::string::npos) return "Model";
    if (value.find("workspace.") != std::string::npos) {
        const std::size_t dot = value.find("workspace.") + 10;
        const std::size_t end = value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_", dot);
        const std::string name = value.substr(dot, end == std::string::npos ? end : end - dot);
        for (const WorkspaceChild& child : snapshot.workspaceChildren)
            if (child.name == name) return child.typeName;
        return "Part";
    }
    if (value.find("LocalPlayer") != std::string::npos) return "Player";
    if (const auto inferred = database.resolveType(value, &snapshot)) return *inferred;
    const std::string lowerName = Lower(variableName);
    if (lowerName.find("part") != std::string::npos || lowerName.find("block") != std::string::npos) return "Part";
    if (lowerName.find("player") != std::string::npos) return "Player";
    if (lowerName.find("humanoid") != std::string::npos) return "Humanoid";
    return {};
}

std::string ReadCatalogFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Unable to open Luau API catalog: " + path.string());
    file.seekg(0, std::ios::end);
    const auto length = file.tellg();
    if (length < 0 || length > 64 * 1024 * 1024) throw std::runtime_error("Luau API catalog exceeds the 64 MiB limit");
    file.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(length), '\0');
    if (!contents.empty()) file.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!file && !contents.empty()) throw std::runtime_error("Unable to read Luau API catalog");
    return contents;
}

struct RankedItem {
    std::string_view label;
    std::string_view lowerLabel;
    std::string_view insertion;
    std::string_view detail;
    CompletionKind kind = CompletionKind::Unknown;
    std::uint64_t id = 0;
    bool deprecated = false;
    int score = 0;
};

} // namespace

CompletionEngine::CompletionEngine() {
    database_.store(CompletionDatabase::CreateBuiltin(), std::memory_order_release);
    semanticSnapshots_.store(std::make_shared<const SemanticSnapshotMap>(), std::memory_order_release);
    workspaceChildren_.store(std::make_shared<const std::vector<WorkspaceChild>>(), std::memory_order_release);
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

CompletionEngine::~CompletionEngine() {
    worker_.request_stop();
    wake_.notify_all();
}

CompletionResponse CompletionEngine::completeLine(std::string_view line, int lineNumber,
                                                   int cursorColumn, std::string_view scriptId,
                                                   bool explicitInvocation) const {
    CompletionResponse response;
    response.line = lineNumber;
    const auto database = database_.load(std::memory_order_acquire);
    if (!database) return response;
    const auto semantic = semanticSnapshot(scriptId);
    const CompletionContext context = ContextAnalyzer::Analyze(line, cursorColumn, *database,
                                                               semantic.get(), explicitInvocation);
    response.startColumn = context.replaceStart;
    response.endColumn = context.replaceEnd;
    response.query = context.query;
    response.receiverType = context.receiverType;
    response.contextKind = static_cast<int>(context.kind);
    if (context.invalidStringOrComment) return response;

    const bool memberTrigger = context.afterDot || context.afterColon;
    if (context.query.empty() && !memberTrigger && !explicitInvocation) return response;
    response.active = true;

    std::vector<const CompletionEntry*> candidates = database->query(context);
    const auto workspaceChildren = workspaceChildren_.load(std::memory_order_acquire);
    std::vector<RankedItem> ranked;
    ranked.reserve(std::min<std::size_t>(candidates.size() + 64, 2048));

    const auto append = [&](std::string_view label, std::string_view lowerLabel,
                            std::string_view insertion, std::string_view detail,
                            CompletionKind kind, std::uint64_t id, bool deprecated,
                            int scoreBias) {
        const FuzzyMatchResult match = FuzzyMatcher::score(context.query, lowerLabel, label);
        if (!match.matched) return;
        int score = match.score + scoreBias;
        if (context.kind == CompletionContextKind::StaticMember && kind == CompletionKind::Constructor) score += 80;
        if ((context.kind == CompletionContextKind::DataModelMember || context.kind == CompletionContextKind::WorkspaceMember) && kind == CompletionKind::Service) score += 25;
        if (deprecated) score -= 100;
        ranked.push_back({label, lowerLabel, insertion, detail, kind, id, deprecated, score});
    };

    for (const CompletionEntry* entry : candidates) {
        if (!entry) continue;
        append(entry->label, entry->lowerLabel, entry->insertText, entry->detail,
               entry->kind, entry->id, entry->isDeprecated, entry->scoreBias);
    }
    if (context.kind == CompletionContextKind::WorkspaceMember && workspaceChildren) {
        for (const WorkspaceChild& child : *workspaceChildren) {
            const std::string_view lower = child.lowerName.empty() ? std::string_view(child.name) : std::string_view(child.lowerName);
            append(child.name, lower, child.name,
                   child.typeName.empty() ? "Workspace child" : child.typeName,
                   CompletionKind::Class, DynamicId("workspace", child.name), false, 30);
        }
    }
    if (context.kind == CompletionContextKind::Global && semantic) {
        for (const LocalVariable& local : semantic->locals) {
            const std::string lower = local.lowerName.empty() ? Lower(local.name) : local.lowerName;
            const std::string detail = local.typeName.empty() ? "Local variable" : local.typeName;
            append(local.name, lower, local.name, detail, CompletionKind::Local,
                   DynamicId(scriptId, local.name), false, 12);
        }
    }

    const std::size_t keep = std::min<std::size_t>(ranked.size(), 64);
    if (keep < ranked.size()) {
        std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(keep), ranked.end(),
            [](const RankedItem& a, const RankedItem& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.lowerLabel < b.lowerLabel;
            });
        ranked.resize(keep);
    } else {
        std::sort(ranked.begin(), ranked.end(), [](const RankedItem& a, const RankedItem& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.lowerLabel < b.lowerLabel;
        });
    }
    constexpr std::size_t maxResults = 24;
    response.items.reserve(std::min(maxResults, ranked.size()));
    std::array<std::string_view, maxResults> emittedLabels{};
    std::size_t emittedCount = 0;
    for (const RankedItem& rankedItem : ranked) {
        bool duplicate = false;
        for (std::size_t i = 0; i < emittedCount; ++i) {
            if (emittedLabels[i] == rankedItem.lowerLabel) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        emittedLabels[emittedCount++] = rankedItem.lowerLabel;
        CompletionItem item;
        item.id = rankedItem.id;
        item.label.assign(rankedItem.label);
        item.insertion.assign(rankedItem.insertion);
        item.detail.assign(rankedItem.detail);
        item.kind = KindName(rankedItem.kind);
        item.deprecated = rankedItem.deprecated;
        item.score = rankedItem.score;
        response.items.push_back(std::move(item));
        if (emittedCount == maxResults) break;
    }
    if (response.items.empty()) response.active = false;
    return response;
}

void CompletionEngine::requestSemanticRefresh(std::string scriptId, std::uint64_t generation,
                                              std::string source) {
    if (scriptId.empty()) return;
    std::lock_guard lock(mutex_);
    pendingSemantic_[scriptId] = SemanticJob{scriptId, generation, std::move(source),
        std::chrono::steady_clock::now() + std::chrono::milliseconds(30)};
    ++pendingRevision_;
    wake_.notify_all();
}

void CompletionEngine::requestCatalogLoad(std::filesystem::path path) {
    if (path.empty()) return;
    std::lock_guard lock(mutex_);
    pendingCatalogPath_ = std::move(path);
    catalogLoadPending_ = true;
    ++pendingRevision_;
    wake_.notify_all();
}

std::shared_ptr<const SemanticSnapshot> CompletionEngine::semanticSnapshot(std::string_view scriptId) const {
    if (scriptId.empty()) return {};
    const auto snapshots = semanticSnapshots_.load(std::memory_order_acquire);
    if (!snapshots) return {};
    const auto found = snapshots->find(scriptId);
    return found == snapshots->end() ? std::shared_ptr<const SemanticSnapshot>{} : found->second;
}

void CompletionEngine::setWorkspaceChildren(std::vector<WorkspaceChild> children) {
    for (WorkspaceChild& child : children) {
        if (child.lowerName.empty()) child.lowerName = Lower(child.name);
        if (child.typeName.empty()) child.typeName = "Part";
    }
    workspaceChildren_.store(std::make_shared<const std::vector<WorkspaceChild>>(std::move(children)),
                             std::memory_order_release);
    candidateGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

std::string CompletionEngine::lastCatalogError() const {
    std::lock_guard lock(mutex_);
    return catalogError_;
}

void CompletionEngine::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        std::filesystem::path catalogPath;
        SemanticJob semanticJob;
        bool loadCatalog = false;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, stop, [&] { return catalogLoadPending_ || !pendingSemantic_.empty(); });
            if (stop.stop_requested()) return;
            if (catalogLoadPending_) {
                catalogPath = std::move(pendingCatalogPath_);
                catalogLoadPending_ = false;
                loadCatalog = true;
            } else {
                auto earliest = std::min_element(pendingSemantic_.begin(), pendingSemantic_.end(),
                    [](const auto& a, const auto& b) { return a.second.due < b.second.due; });
                if (earliest == pendingSemantic_.end()) continue;
                const auto due = earliest->second.due;
                const std::uint64_t revision = pendingRevision_;
                if (std::chrono::steady_clock::now() < due) {
                    wake_.wait_until(lock, stop, due, [&] {
                        return catalogLoadPending_ || pendingRevision_ != revision;
                    });
                    continue;
                }
                semanticJob = std::move(earliest->second);
                pendingSemantic_.erase(earliest);
            }
        }

        if (loadCatalog) {
            try {
                const std::string source = ReadCatalogFile(catalogPath);
                const auto base = database_.load(std::memory_order_acquire);
                std::string error;
                auto indexed = CompletionIndexBuilder::Build(source, *base, &error);
                if (!indexed) throw std::runtime_error(error.empty() ? "Catalog indexing failed" : error);
                database_.store(std::move(indexed), std::memory_order_release);
                candidateGeneration_.fetch_add(1, std::memory_order_acq_rel);
                std::lock_guard lock(mutex_);
                catalogError_.clear();
            } catch (const std::exception& exception) {
                std::lock_guard lock(mutex_);
                catalogError_ = exception.what();
            }
            continue;
        }
        analyzeSource(std::move(semanticJob));
    }
}

void CompletionEngine::analyzeSource(SemanticJob job) {
    const auto database = database_.load(std::memory_order_acquire);
    if (!database) return;
    auto snapshot = std::make_shared<SemanticSnapshot>();
    snapshot->scriptId = job.scriptId;
    snapshot->generation = job.generation;
    const auto children = workspaceChildren_.load(std::memory_order_acquire);
    if (children) snapshot->workspaceChildren = *children;

    static const std::regex localDeclaration(
        R"(^\s*local\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([A-Za-z_][A-Za-z0-9_.]*))?\s*=\s*(.*?)\s*(?:--.*)?$)");
    std::istringstream input(job.source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, localDeclaration)) {
            LocalVariable local;
            local.name = match[1].str();
            local.lowerName = Lower(local.name);
            local.line = lineNumber;
            const std::string annotation = match[2].str();
            const std::string expression = match[3].str();
            local.typeName = annotation.empty() ? InferExpressionType(expression, local.name, *database, *snapshot) : annotation;
            snapshot->locals.push_back(local);
            if (!local.typeName.empty()) snapshot->localTypes[local.name] = local.typeName;
        }
        ++lineNumber;
    }

    const auto previous = semanticSnapshots_.load(std::memory_order_acquire);
    if (previous) {
        const auto existing = previous->find(job.scriptId);
        if (existing != previous->end() && existing->second->generation > job.generation) return;
    }
    auto updated = std::make_shared<SemanticSnapshotMap>(previous ? *previous : SemanticSnapshotMap{});
    (*updated)[job.scriptId] = std::move(snapshot);
    semanticSnapshots_.store(std::move(updated), std::memory_order_release);
}

} // namespace blubox::engine::editor
