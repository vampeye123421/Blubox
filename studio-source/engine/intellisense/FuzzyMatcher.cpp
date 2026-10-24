#include "FuzzyMatcher.hpp"

#include <cctype>
#include <algorithm>

namespace blubox::engine::intellisense {
namespace {
constexpr unsigned char Lower(unsigned char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<unsigned char>(c + ('a' - 'A')) : c;
}

bool IsBoundary(std::string_view original, std::size_t index) noexcept {
    if (index == 0) return true;
    const unsigned char current = static_cast<unsigned char>(original[index]);
    const unsigned char previous = static_cast<unsigned char>(original[index - 1]);
    return previous == '_' || previous == '-' || previous == ' ' || previous == '.' ||
           previous == ':' || (std::islower(previous) && std::isupper(current));
}
}

FuzzyMatchResult FuzzyMatcher::score(std::string_view query,
                                     std::string_view candidateLower,
                                     std::string_view originalCandidate) noexcept {
    if (query.empty()) return {true, 0};
    if (query.size() > candidateLower.size()) return {};

    // Fast reject and score in one pass. No normalization/allocation occurs in
    // the hot loop: both labels and UI queries are already normalized.
    std::size_t qi = 0;
    int score = 0;
    int run = 0;
    std::size_t first = candidateLower.size();
    for (std::size_t ci = 0; ci < candidateLower.size() && qi < query.size(); ++ci) {
        if (query[qi] != candidateLower[ci]) {
            run = 0;
            continue;
        }
        if (first == candidateLower.size()) first = ci;
        score += 10;
        if (IsBoundary(originalCandidate, ci)) score += 38;
        if (ci == qi) score += 18;
        if (run > 0) score += 14 + run * 2;
        ++run;
        ++qi;
    }
    if (qi != query.size()) return {};

    if (first == 0) score += 190;
    if (candidateLower.size() >= query.size() && candidateLower.substr(0, query.size()) == query)
        score += 360;
    if (candidateLower == query) score += 800;
    score -= static_cast<int>(candidateLower.size() - query.size());
    return {true, score};
}

} // namespace blubox::engine::intellisense
