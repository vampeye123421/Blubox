#include "ContextAnalyzer.hpp"

#include "CompletionDatabase.hpp"

#include <algorithm>
#include <cctype>

namespace blubox::engine::intellisense {
namespace {
bool IsIdentifier(unsigned char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

char Lower(unsigned char c) noexcept {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
}

std::string ExtractExpression(std::string_view text, int end) {
    int i = std::clamp(end, 0, static_cast<int>(text.size()));
    while (i > 0 && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(i - 1)]))) --i;
    const int expressionEnd = i;
    bool needIdentifier = true;
    while (i > 0) {
        if (needIdentifier) {
            const int endIdentifier = i;
            while (i > 0 && IsIdentifier(static_cast<unsigned char>(text[static_cast<std::size_t>(i - 1)]))) --i;
            if (i == endIdentifier) break;
            needIdentifier = false;
        } else {
            int beforeSpace = i;
            while (beforeSpace > 0 && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(beforeSpace - 1)]))) --beforeSpace;
            if (beforeSpace > 0 && text[static_cast<std::size_t>(beforeSpace - 1)] == '.') {
                i = beforeSpace - 1;
                needIdentifier = true;
            } else {
                break;
            }
        }
    }
    if (i == expressionEnd) return {};
    return std::string(text.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(expressionEnd - i)));
}

bool InStringOrComment(std::string_view text) noexcept {
    char quote = 0;
    bool escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') return true;
        if (c == '\'' || c == '"' || c == '`') quote = c;
    }
    return quote != 0;
}

std::string_view LastMember(std::string_view expression) noexcept {
    const std::size_t dot = expression.rfind('.');
    return dot == std::string_view::npos ? expression : expression.substr(dot + 1);
}

std::string_view ParentExpression(std::string_view expression) noexcept {
    const std::size_t dot = expression.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : expression.substr(0, dot);
}
}

CompletionContext ContextAnalyzer::Analyze(std::string_view line, int caretColumn,
                                           const CompletionDatabase& database,
                                           const SemanticSnapshot* semantic,
                                           bool explicitInvocation) {
    CompletionContext context;
    context.line = 0;
    context.explicitInvocation = explicitInvocation;
    const int cursor = std::clamp(caretColumn, 0, static_cast<int>(line.size()));
    int prefixStart = cursor;
    while (prefixStart > 0 && IsIdentifier(static_cast<unsigned char>(line[static_cast<std::size_t>(prefixStart - 1)]))) --prefixStart;
    context.replaceStart = prefixStart;
    context.replaceEnd = cursor;
    context.query.reserve(static_cast<std::size_t>(cursor - prefixStart));
    for (int i = prefixStart; i < cursor; ++i)
        context.query.push_back(Lower(static_cast<unsigned char>(line[static_cast<std::size_t>(i)])));

    const std::string_view beforeCaret = line.substr(0, static_cast<std::size_t>(cursor));
    if (InStringOrComment(beforeCaret)) {
        context.invalidStringOrComment = true;
        context.kind = CompletionContextKind::Unknown;
        return context;
    }

    int separator = prefixStart - 1;
    while (separator >= 0 && std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(separator)]))) --separator;
    if (separator < 0 || (line[static_cast<std::size_t>(separator)] != '.' && line[static_cast<std::size_t>(separator)] != ':')) {
        context.kind = CompletionContextKind::Global;
        return context;
    }

    const char op = line[static_cast<std::size_t>(separator)];
    context.afterDot = op == '.';
    context.afterColon = op == ':';
    context.receiverExpression = ExtractExpression(line, separator);
    const auto resolved = database.resolveType(context.receiverExpression, semantic);
    context.receiverType = resolved.value_or(std::string{});

    // If the expression before the operator is an event, complete the
    // RBXScriptSignal API (Connect/Once/Wait), not the owning Instance API.
    const std::string_view eventName = LastMember(context.receiverExpression);
    const std::string_view eventOwnerExpression = ParentExpression(context.receiverExpression);
    if (!eventName.empty() && !eventOwnerExpression.empty()) {
        const auto eventOwnerType = database.resolveType(eventOwnerExpression, semantic);
        if (eventOwnerType) {
            const CompletionEntry* event = database.findMember(*eventOwnerType, eventName);
            if (event && (event->kind == CompletionKind::Event || event->typeName == "RBXScriptSignal")) {
                context.kind = CompletionContextKind::SignalMember;
                context.receiverType = "RBXScriptSignal";
                return context;
            }
        }
    }

    if (context.receiverType.empty()) {
        const std::string lowerExpression = [&] {
            std::string value;
            value.reserve(context.receiverExpression.size());
            for (unsigned char c : context.receiverExpression) value.push_back(Lower(c));
            return value;
        }();
        if (lowerExpression.find("part") != std::string::npos || lowerExpression.find("block") != std::string::npos)
            context.receiverType = "Part";
        else if (!context.receiverExpression.empty()) context.receiverType = "Instance";
    }

    if (context.receiverType == "RBXScriptSignal") {
        context.kind = CompletionContextKind::SignalMember;
    } else if (context.afterColon) {
        context.kind = CompletionContextKind::MethodCall;
    } else if (context.receiverType == "DataModel") {
        context.kind = CompletionContextKind::DataModelMember;
    } else if (context.receiverType == "Workspace") {
        context.kind = CompletionContextKind::WorkspaceMember;
    } else if (database.isDataType(context.receiverType) || context.receiverExpression == "Instance") {
        context.kind = CompletionContextKind::StaticMember;
    } else {
        context.kind = CompletionContextKind::InstanceMember;
    }
    return context;
}

} // namespace blubox::engine::intellisense
