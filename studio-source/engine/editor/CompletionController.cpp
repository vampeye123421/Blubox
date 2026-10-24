#include "CompletionController.hpp"

#include <utility>

namespace blubox::engine::editor {

void CompletionController::onTextChanged(const TextChange&) {
    ++generation_;
    semanticDue_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(140);
    semanticDirty_ = true;
    queryDirty_ = true;
}

void CompletionController::onCaretMoved(int, int) {
    queryDirty_ = true;
}

bool CompletionController::needsUpdate(const CompletionEngine& engine, std::string_view scriptId) const {
    if (queryDirty_ || scriptId != lastScriptId_ || engine.candidateGeneration() != lastCandidateGeneration_)
        return true;
    const auto snapshot = engine.semanticSnapshot(scriptId);
    return (snapshot ? snapshot->generation : 0) != lastSemanticGeneration_;
}

void CompletionController::update(const CompletionEngine& engine, std::string_view scriptId,
                                  std::string_view currentLine, int line, int cursorColumn,
                                  bool editorFocused, bool explicitInvocation) {
    if (!editorFocused) {
        response_ = {};
        lastScriptId_.assign(scriptId);
        lastLine_.assign(currentLine);
        lastLineNumber_ = line;
        lastCursorColumn_ = cursorColumn;
        queryDirty_ = false;
        return;
    }

    const std::string id(scriptId);
    if (id != lastScriptId_ || currentLine != lastLine_ || line != lastLineNumber_ ||
        cursorColumn != lastCursorColumn_ || explicitInvocation)
        queryDirty_ = true;

    const auto snapshot = engine.semanticSnapshot(scriptId);
    const std::uint64_t semanticGeneration = snapshot ? snapshot->generation : 0;
    const std::uint64_t candidateGeneration = engine.candidateGeneration();
    if (semanticGeneration != lastSemanticGeneration_ || candidateGeneration != lastCandidateGeneration_)
        queryDirty_ = true;

    if (queryDirty_) {
        response_ = engine.completeLine(currentLine, line, cursorColumn, scriptId, explicitInvocation);
        lastScriptId_ = id;
        lastLine_.assign(currentLine);
        lastLineNumber_ = line;
        lastCursorColumn_ = cursorColumn;
        lastSemanticGeneration_ = semanticGeneration;
        lastCandidateGeneration_ = candidateGeneration;
        queryDirty_ = false;
    }
}

bool CompletionController::takeSemanticRefreshRequest(std::chrono::steady_clock::time_point now,
                                                       std::string& scriptId,
                                                       std::uint64_t& generation) {
    if (!semanticDirty_ || now < semanticDue_ || lastScriptId_.empty()) return false;
    semanticDirty_ = false;
    scriptId = lastScriptId_;
    generation = generation_;
    return true;
}

void CompletionController::clear() {
    response_ = {};
    lastLine_.clear();
    lastScriptId_.clear();
    lastLineNumber_ = -1;
    lastCursorColumn_ = -1;
    queryDirty_ = true;
}

} // namespace blubox::engine::editor
