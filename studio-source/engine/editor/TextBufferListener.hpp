#pragma once

#include <cstdint>
#include <string>

namespace blubox::engine::editor {

enum class TextChangeReason : std::uint8_t { Insert, Delete, Replace, Paste, Undo, Redo };

struct TextChange {
    TextChangeReason reason = TextChangeReason::Replace;
    int line = 0;
    int column = 0;
    int removedLength = 0;
    std::string insertedText;
};

class TextBufferListener {
public:
    virtual ~TextBufferListener() = default;
    virtual void onTextChanged(const TextChange& change) = 0;
    virtual void onCaretMoved(int line, int column) = 0;
};

} // namespace blubox::engine::editor
