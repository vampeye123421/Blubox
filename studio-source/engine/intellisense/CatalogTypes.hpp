#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace blubox::engine::intellisense {

enum class CompletionKind : std::uint8_t {
    Global, Library, DataType, Enum, EnumItem, Class, Service, Keyword,
    Local, Property, Method, Event, Callback, Constructor, Constant,
    SignalMethod, Unknown
};

enum class CompletionContextKind : std::uint8_t {
    Global, DataModelMember, WorkspaceMember, StaticMember,
    InstanceMember, MethodCall, SignalMember, Unknown
};

struct CompletionEntry {
    std::uint64_t id = 0;
    std::uint64_t parentId = 0;
    std::string label;
    std::string lowerLabel;
    std::string insertText;
    std::string detail;
    std::string documentation;
    std::string owner;
    std::string typeName;
    CompletionKind kind = CompletionKind::Unknown;
    bool isStatic = false;
    bool isDeprecated = false;
    int scoreBias = 0;
};

struct LocalVariable {
    std::string name;
    std::string lowerName;
    std::string typeName;
    int line = 0;
    int column = 0;
};

struct WorkspaceChild {
    std::string name;
    std::string lowerName;
    std::string typeName;
};

// Published as shared_ptr<const SemanticSnapshot>; workers build a replacement
// off-thread and the editor only reads immutable snapshots.
struct SemanticSnapshot {
    std::string scriptId;
    std::uint64_t generation = 0;
    std::vector<LocalVariable> locals;
    std::unordered_map<std::string, std::string> localTypes;
    std::vector<WorkspaceChild> workspaceChildren;
};

struct CompletionContext {
    CompletionContextKind kind = CompletionContextKind::Unknown;
    std::string receiverExpression;
    std::string receiverType;
    std::string query;
    int line = 0;
    int replaceStart = 0;
    int replaceEnd = 0;
    bool afterDot = false;
    bool afterColon = false;
    bool explicitInvocation = false;
    bool invalidStringOrComment = false;
};

} // namespace blubox::engine::intellisense
