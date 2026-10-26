#pragma once

#include <string_view>

namespace blubox::engine::intellisense {

struct FuzzyMatchResult {
    bool matched = false;
    int score = 0;
};

class FuzzyMatcher final {
public:
    // Candidate lower-case text is precomputed by CompletionIndexBuilder.
    static FuzzyMatchResult score(std::string_view query,
                                  std::string_view candidateLower,
                                  std::string_view originalCandidate) noexcept;
};

} // namespace blubox::engine::intellisense
