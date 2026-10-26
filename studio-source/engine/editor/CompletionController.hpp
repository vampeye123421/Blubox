#pragma once

#include "CompletionEngine.hpp"
#include "TextBufferListener.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace blubox::engine::editor {

class CompletionController final : public TextBufferListener {
public:
    void onTextChanged(const TextChange& change) override;
    void onCaretMoved(int line, int column) override;

    // Runs fast context extraction and candidate ranking only when the editor
    // buffer/caret or immutable semantic snapshot changes.
    void update(const CompletionEngine& engine, std::string_view scriptId,
                std::string_view currentLine, int line, int cursorColumn,
                bool editorFocused, bool explicitInvocation = false);
    bool needsUpdate(const CompletionEngine& engine, std::string_view scriptId) const;

    bool takeSemanticRefreshRequest(std::chrono::steady_clock::time_point now,
                                    std::string& scriptId, std::uint64_t& generation);
    void clear();

    const CompletionResponse& response() const noexcept { return response_; }
    bool isDirty() const noexcept { return semanticDirty_; }
    std::uint64_t generation() const noexcept { return generation_; }

private:
    CompletionResponse response_;
    std::string lastScriptId_;
    std::string lastLine_;
    int lastLineNumber_ = -1;
    int lastCursorColumn_ = -1;
    std::uint64_t lastSemanticGeneration_ = 0;
    std::uint64_t lastCandidateGeneration_ = 0;
    std::uint64_t generation_ = 0;
    std::chrono::steady_clock::time_point semanticDue_{};
    bool semanticDirty_ = false;
    bool queryDirty_ = true;
};

} // namespace blubox::engine::editor
