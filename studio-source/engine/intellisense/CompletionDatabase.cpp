#include "CompletionDatabase.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace blubox::engine::intellisense {
namespace {
using Json = nlohmann::json;

std::string Lower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text) result.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c));
    return result;
}

std::uint64_t StableId(std::string_view owner, std::string_view label, CompletionKind kind) noexcept {
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](unsigned char c, std::uint64_t& value) {
        value ^= c;
        value *= 1099511628211ull;
    };
    for (unsigned char c : owner) mix(c, hash);
    mix(0x1f, hash);
    for (unsigned char c : label) mix(c, hash);
    mix(static_cast<unsigned char>(kind), hash);
    return hash == 0 ? 1 : hash;
}

CompletionKind ParseKind(std::string_view kind) {
    if (kind == "global") return CompletionKind::Global;
    if (kind == "library") return CompletionKind::Library;
    if (kind == "dataType" || kind == "datatype") return CompletionKind::DataType;
    if (kind == "enum") return CompletionKind::Enum;
    if (kind == "class" || kind == "service") return kind == "service" ? CompletionKind::Service : CompletionKind::Class;
    if (kind == "property") return CompletionKind::Property;
    if (kind == "method" || kind == "function") return CompletionKind::Method;
    if (kind == "event" || kind == "signal") return CompletionKind::Event;
    if (kind == "callback") return CompletionKind::Callback;
    if (kind == "constructor") return CompletionKind::Constructor;
    if (kind == "constant" || kind == "enumItem") return CompletionKind::Constant;
    return CompletionKind::Unknown;
}

std::string ValueString(const Json& object, const char* key, std::string fallback = {}) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::move(fallback);
}

bool ValueBool(const Json& object, const char* key, bool fallback = false) {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

void ReadMembers(CompletionDatabase& database, const Json& array,
                 const std::string& owner, CompletionKind defaultKind,
                 bool staticMembers = false) {
    if (!array.is_array()) return;
    for (const Json& member : array) {
        if (!member.is_object()) continue;
        const std::string name = ValueString(member, "name");
        if (name.empty()) continue;
        std::string kindName = ValueString(member, "kind");
        CompletionKind kind = kindName.empty() ? defaultKind : ParseKind(kindName);
        const std::string signature = ValueString(member, "signature");
        const std::string typeName = ValueString(member, "type");
        const std::string doc = ValueString(member, "documentation");
        const bool isStatic = staticMembers || ValueBool(member, "isStatic") || ValueBool(member, "static");
        const bool deprecated = ValueBool(member, "deprecated") || ValueBool(member, "isDeprecated");
        std::string detail = signature;
        if (!typeName.empty()) {
            if (!detail.empty()) detail += "  ·  ";
            detail += typeName;
        }
        if (!doc.empty()) {
            if (!detail.empty()) detail += "\n";
            detail += doc;
        }
        CompletionEntry entry;
        entry.label = name;
        entry.insertText = name;
        entry.owner = owner;
        entry.typeName = typeName;
        entry.kind = kind;
        entry.detail = std::move(detail);
        entry.documentation = doc;
        entry.isStatic = isStatic;
        entry.isDeprecated = deprecated;
        database.add(std::move(entry));
    }
}

} // namespace

void CompletionDatabase::add(CompletionEntry entry) {
    if (entry.label.empty()) return;
    if (entry.insertText.empty()) entry.insertText = entry.label;
    entry.lowerLabel = Lower(entry.label);
    if (entry.id == 0) entry.id = StableId(entry.owner, entry.label, entry.kind);
    const auto existing = idToIndex_.find(entry.id);
    if (existing != idToIndex_.end()) {
        // Bundled metadata can enrich a fallback entry without creating
        // duplicates or changing its stable ID.
        CompletionEntry& target = entries_[existing->second];
        if (!entry.detail.empty()) target.detail = std::move(entry.detail);
        if (!entry.documentation.empty()) target.documentation = std::move(entry.documentation);
        if (!entry.typeName.empty()) target.typeName = std::move(entry.typeName);
        target.isStatic = target.isStatic || entry.isStatic;
        target.isDeprecated = target.isDeprecated || entry.isDeprecated;
        target.scoreBias = std::max(target.scoreBias, entry.scoreBias);
        return;
    }
    const auto index = static_cast<std::uint32_t>(entries_.size());
    idToIndex_.emplace(entry.id, index);
    entries_.push_back(std::move(entry));
}

void CompletionDatabase::addBuiltin(std::string owner, std::string label, std::string insert,
                                    CompletionKind kind, std::string detail, bool isStatic,
                                    std::string typeName, int scoreBias) {
    CompletionEntry entry;
    entry.owner = std::move(owner);
    entry.label = std::move(label);
    entry.insertText = std::move(insert);
    entry.kind = kind;
    entry.detail = std::move(detail);
    entry.isStatic = isStatic;
    entry.typeName = std::move(typeName);
    entry.scoreBias = scoreBias;
    add(std::move(entry));
}

std::shared_ptr<CompletionDatabase> CompletionDatabase::CreateBuiltin() {
    auto db = std::make_shared<CompletionDatabase>();
    const auto root = [&](std::string label, CompletionKind kind, std::string type = {}, std::string detail = {}) {
        db->addBuiltin("", label, label, kind, std::move(detail), false, type, 0);
        if (!type.empty()) db->rootTypes_[Lower(label)] = std::move(type);
    };
    root("game", CompletionKind::Global, "DataModel", "Root DataModel");
    root("workspace", CompletionKind::Global, "Workspace", "World and physics service");
    root("script", CompletionKind::Global, "Script", "Currently executing Script or LocalScript");
    root("Instance", CompletionKind::Global, "Instance", "Instance factory");
    root("Vector2", CompletionKind::DataType, "Vector2", "2D vector type");
    root("Vector3", CompletionKind::DataType, "Vector3", "3D vector type");
    root("CFrame", CompletionKind::DataType, "CFrame", "3D coordinate frame");
    root("Color3", CompletionKind::DataType, "Color3", "RGB color value");
    db->addBuiltin("", "Color3.fromRGB", "Color3.fromRGB", CompletionKind::Constructor,
                   "Color3.fromRGB(r: number, g: number, b: number): Color3", true, "Color3", 15);
    db->addBuiltin("", "GetService", "GetService", CompletionKind::Method,
                   "DataModel:GetService(className: string): Instance", false, "DataModel", 10);
    root("UDim", CompletionKind::DataType, "UDim", "Scale and offset value");
    root("UDim2", CompletionKind::DataType, "UDim2", "2D scale and offset value");
    root("Enum", CompletionKind::Global, "Enum", "Enum registry");
    root("task", CompletionKind::Library, "task", "Coroutine scheduling library");
    root("math", CompletionKind::Library, "math", "Luau math library");
    root("string", CompletionKind::Library, "string", "Luau string library");
    root("table", CompletionKind::Library, "table", "Luau table library");
    root("utf8", CompletionKind::Library, "utf8", "Luau UTF-8 library");
    root("coroutine", CompletionKind::Library, "coroutine", "Luau coroutine library");
    root("print", CompletionKind::Global, {}, "Write a message to Output");
    root("warn", CompletionKind::Global, {}, "Write a warning to Output");
    root("error", CompletionKind::Global, {}, "Raise a Luau error");
    root("assert", CompletionKind::Global, {}, "Assert a condition");
    root("pairs", CompletionKind::Global, {}, "Iterate key-value pairs");
    root("ipairs", CompletionKind::Global, {}, "Iterate array values");
    root("next", CompletionKind::Global, {}, "Iterate a table");
    root("type", CompletionKind::Global, {}, "Return a value's type");
    root("typeof", CompletionKind::Global, {}, "Return an engine-aware type");
    root("select", CompletionKind::Global, {}, "Select variadic arguments");
    root("tonumber", CompletionKind::Global, {}, "Convert a value to a number");
    root("tostring", CompletionKind::Global, {}, "Convert a value to a string");
    root("game", CompletionKind::Global, "DataModel", "Root DataModel");
    root("_G", CompletionKind::Global, "table", "Global environment table");
    root("BasePart", CompletionKind::Class, "BasePart", "Base class for physical parts");
    root("Part", CompletionKind::Class, "Part", "A physical 3D part");
    root("SpawnLocation", CompletionKind::Class, "SpawnLocation", "A player spawn point");
    root("Model", CompletionKind::Class, "Model", "A group of instances");
    root("Folder", CompletionKind::Class, "Folder", "A container for instances");
    root("Script", CompletionKind::Class, "Script", "Server Luau script");
    root("LocalScript", CompletionKind::Class, "LocalScript", "Client Luau script");
    root("ModuleScript", CompletionKind::Class, "ModuleScript", "Reusable Luau module");
    root("ScreenGui", CompletionKind::Class, "ScreenGui", "Top-level screen UI");
    root("Frame", CompletionKind::Class, "Frame", "Rectangular UI container");
    root("TextLabel", CompletionKind::Class, "TextLabel", "Text display UI");
    root("TextButton", CompletionKind::Class, "TextButton", "Clickable text UI");

    const auto member = [&](std::string owner, std::string name, CompletionKind kind,
                            std::string detail = {}, bool isStatic = false, std::string type = {}) {
        const std::string insertion = name;
        db->addBuiltin(std::move(owner), std::move(name), insertion, kind, std::move(detail), isStatic, std::move(type));
    };
    const auto service = [&](std::string name, std::string desc) {
        member("DataModel", name, CompletionKind::Service, std::move(desc), false, name);
    };
    service("Workspace", "World geometry, characters, and physics");
    service("Players", "Players in the experience");
    service("ReplicatedStorage", "Objects available to server and clients");
    service("ServerScriptService", "Server-side scripts");
    service("ServerStorage", "Server-only objects");
    service("StarterPlayer", "Player and character templates");
    service("StarterGui", "Player GUI templates");
    service("Lighting", "Lighting and atmosphere");
    service("RunService", "Frame and lifecycle events");
    service("ReplicatedFirst", "Objects replicated before the rest of the game");
    member("Players", "LocalPlayer", CompletionKind::Property, "Local client player", false, "Player");
    member("Players", "PlayerAdded", CompletionKind::Event, "PlayerAdded(player: Player)", false, "RBXScriptSignal");
    member("Player", "Character", CompletionKind::Property, "Player character", false, "Model?");
    member("Player", "CharacterAdded", CompletionKind::Event, "CharacterAdded(character: Model)", false, "RBXScriptSignal");
    member("Humanoid", "Health", CompletionKind::Property, "Current health", false, "number");
    member("Humanoid", "MoveTo", CompletionKind::Method, "MoveTo(location: Vector3): ()");
    member("GuiObject", "Position", CompletionKind::Property, "UI position", false, "UDim2");
    member("GuiObject", "Size", CompletionKind::Property, "UI size", false, "UDim2");
    member("GuiObject", "Visible", CompletionKind::Property, "Whether this UI is visible", false, "boolean");
    member("GuiObject", "BackgroundColor3", CompletionKind::Property, "Background color", false, "Color3");
    member("GuiObject", "BackgroundTransparency", CompletionKind::Property, "Background transparency", false, "number");
    member("TextLabel", "Text", CompletionKind::Property, "Displayed text", false, "string");
    member("TextLabel", "TextColor3", CompletionKind::Property, "Text color", false, "Color3");
    member("TextLabel", "TextTransparency", CompletionKind::Property, "Text transparency", false, "number");
    member("TextLabel", "TextScaled", CompletionKind::Property, "Scale text to fit", false, "boolean");
    member("TextLabel", "TextWrapped", CompletionKind::Property, "Wrap text", false, "boolean");
    member("TextButton", "Activated", CompletionKind::Event, "Activated(inputObject: InputObject)", false, "RBXScriptSignal");
    member("DataModel", "GetService", CompletionKind::Method, "GetService(className: string): Instance");
    member("DataModel", "FindService", CompletionKind::Method, "FindService(className: string): Instance?");
    member("DataModel", "GetObjects", CompletionKind::Method, "GetObjects(url: string): {Instance}");
    member("DataModel", "Name", CompletionKind::Property, "DataModel name", false, "string");

    const auto instanceMembers = [&](std::string owner) {
        member(owner, "Name", CompletionKind::Property, "Instance name", false, "string");
        member(owner, "Parent", CompletionKind::Property, "Parent Instance", false, "Instance?");
        member(owner, "ClassName", CompletionKind::Property, "Class name", false, "string");
        member(owner, "GetChildren", CompletionKind::Method, "GetChildren(): {Instance}");
        member(owner, "GetDescendants", CompletionKind::Method, "GetDescendants(): {Instance}");
        member(owner, "FindFirstChild", CompletionKind::Method, "FindFirstChild(name: string): Instance?");
        member(owner, "WaitForChild", CompletionKind::Method, "WaitForChild(name: string): Instance");
        member(owner, "IsA", CompletionKind::Method, "IsA(className: string): boolean");
        member(owner, "Clone", CompletionKind::Method, "Clone(): Instance");
        member(owner, "Destroy", CompletionKind::Method, "Destroy(): ()");
        member(owner, "Changed", CompletionKind::Event, "Changed(property: string)", false, "RBXScriptSignal");
        member(owner, "AncestryChanged", CompletionKind::Event, "AncestryChanged(child: Instance, parent: Instance?)", false, "RBXScriptSignal");
    };
    instanceMembers("Instance");
    member("Instance", "new", CompletionKind::Constructor, "Instance.new(className: string): Instance", true);
    db->baseClasses_["basepart"] = "Instance";
    db->baseClasses_["part"] = "BasePart";
    db->baseClasses_["spawnlocation"] = "Part";
    db->baseClasses_["model"] = "Instance";
    db->baseClasses_["folder"] = "Instance";
    db->baseClasses_["script"] = "Instance";
    db->baseClasses_["localscript"] = "Script";
    db->baseClasses_["screengui"] = "LayerCollector";
    db->baseClasses_["frame"] = "GuiObject";
    db->baseClasses_["textlabel"] = "GuiObject";
    db->baseClasses_["textbutton"] = "GuiObject";
    db->baseClasses_["guiobject"] = "Instance";
    db->baseClasses_["layercollector"] = "Instance";
    instanceMembers("BasePart");
    member("BasePart", "Position", CompletionKind::Property, "World position", false, "Vector3");
    member("BasePart", "CFrame", CompletionKind::Property, "World transform", false, "CFrame");
    member("BasePart", "Orientation", CompletionKind::Property, "Euler rotation", false, "Vector3");
    member("BasePart", "Size", CompletionKind::Property, "Part dimensions", false, "Vector3");
    member("BasePart", "Color", CompletionKind::Property, "Part color", false, "Color3");
    member("BasePart", "Transparency", CompletionKind::Property, "Transparency from 0 to 1", false, "number");
    member("BasePart", "Anchored", CompletionKind::Property, "Whether the part is fixed", false, "boolean");
    member("BasePart", "CanCollide", CompletionKind::Property, "Whether physical collisions are enabled", false, "boolean");
    member("BasePart", "CanTouch", CompletionKind::Property, "Whether touch events are enabled", false, "boolean");
    member("BasePart", "Material", CompletionKind::Property, "Surface material", false, "Enum.Material");
    member("BasePart", "Touched", CompletionKind::Event, "Touched(otherPart: BasePart)", false, "RBXScriptSignal");
    member("BasePart", "TouchEnded", CompletionKind::Event, "TouchEnded(otherPart: BasePart)", false, "RBXScriptSignal");
    member("Part", "Shape", CompletionKind::Property, "Part shape", false, "Enum.PartType");
    member("SpawnLocation", "Enabled", CompletionKind::Property, "Whether this spawn is enabled", false, "boolean");
    member("SpawnLocation", "Neutral", CompletionKind::Property, "Whether any player can use this spawn", false, "boolean");
    member("Model", "PrimaryPart", CompletionKind::Property, "Primary BasePart", false, "BasePart?");
    member("Model", "PivotTo", CompletionKind::Method, "PivotTo(target: CFrame): ()");
    member("Workspace", "Gravity", CompletionKind::Property, "World gravity", false, "number");
    member("Workspace", "CurrentCamera", CompletionKind::Property, "Current camera", false, "Camera");
    member("Workspace", "Raycast", CompletionKind::Method, "Raycast(origin: Vector3, direction: Vector3): RaycastResult?");

    const auto signal = [&](std::string name, std::string detail) {
        member("RBXScriptSignal", std::move(name), CompletionKind::SignalMethod, std::move(detail));
    };
    signal("Connect", "Connect(callback: (…any) -> ()): RBXScriptConnection");
    signal("Once", "Once(callback: (…any) -> ()): RBXScriptConnection");
    signal("Wait", "Wait(): …any");
    member("RBXScriptConnection", "Connected", CompletionKind::Property, "Whether the connection is active", false, "boolean");
    member("RBXScriptConnection", "Disconnect", CompletionKind::Method, "Disconnect(): ()");

    member("Color3", "new", CompletionKind::Constructor, "new(r: number, g: number, b: number): Color3", true);
    member("Color3", "fromRGB", CompletionKind::Constructor, "fromRGB(r: number, g: number, b: number): Color3", true);
    member("Color3", "fromHSV", CompletionKind::Constructor, "fromHSV(h: number, s: number, v: number): Color3", true);
    member("Color3", "fromHex", CompletionKind::Constructor, "fromHex(hex: string): Color3", true);
    member("Color3", "Lerp", CompletionKind::Method, "Lerp(goal: Color3, alpha: number): Color3", false);
    member("Vector3", "new", CompletionKind::Constructor, "new(x: number?, y: number?, z: number?): Vector3", true);
    for (const char* axis : {"zero", "one", "xAxis", "yAxis", "zAxis"})
        member("Vector3", axis, CompletionKind::Constant, "Vector3 constant", true, "Vector3");
    for (const char* method : {"Dot", "Cross", "Lerp", "Magnitude", "Unit", "FuzzyEq"})
        member("Vector3", method, CompletionKind::Method, std::string(method) + "(...): Vector3");
    member("Vector2", "new", CompletionKind::Constructor, "new(x: number?, y: number?): Vector2", true);
    for (const char* axis : {"zero", "one", "xAxis", "yAxis"}) member("Vector2", axis, CompletionKind::Constant, "Vector2 constant", true, "Vector2");
    member("CFrame", "new", CompletionKind::Constructor, "new(...): CFrame", true);
    member("CFrame", "Angles", CompletionKind::Constructor, "Angles(rx: number, ry: number, rz: number): CFrame", true);
    member("CFrame", "lookAt", CompletionKind::Constructor, "lookAt(at: Vector3, lookAt: Vector3): CFrame", true);

    for (const char* name : {"wait", "spawn", "defer", "delay", "cancel"})
        member("task", name, CompletionKind::Method, std::string(name) + "(...): thread");
    for (const char* name : {"abs", "ceil", "floor", "max", "min", "clamp", "sqrt", "sin", "cos", "random", "round", "sign"})
        member("math", name, CompletionKind::Method, std::string(name) + "(...)");
    for (const char* name : {"abs", "byte", "char", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep", "reverse", "sub", "upper"})
        member("string", name, CompletionKind::Method, std::string(name) + "(...)");
    for (const char* name : {"clear", "clone", "concat", "create", "find", "freeze", "insert", "maxn", "move", "pack", "remove", "sort", "unpack"})
        member("table", name, CompletionKind::Method, std::string(name) + "(...)");

    const char* keywords[] = {"local", "function", "if", "then", "else", "elseif", "end", "for", "while", "repeat", "until", "return", "break", "continue", "true", "false", "nil", "and", "or", "not", "do", "in"};
    for (const char* keyword : keywords) db->addBuiltin("", keyword, keyword, CompletionKind::Keyword, "Luau keyword");
    db->buildIndexes();
    return db;
}

void CompletionDatabase::mergeCatalogJson(std::string_view jsonText) {
    const Json root = Json::parse(jsonText.begin(), jsonText.end());
    if (!root.is_object() || root.value("schemaVersion", 0) != 1)
        throw std::runtime_error("Unsupported Luau catalog schema version");

    const auto addRoots = [&](const char* key, CompletionKind forcedKind) {
        const auto it = root.find(key);
        if (it == root.end() || !it->is_array()) return;
        for (const Json& object : *it) {
            if (!object.is_object()) continue;
            const std::string name = ValueString(object, "name");
            if (name.empty()) continue;
            std::string kindText = ValueString(object, "kind");
            CompletionKind kind = kindText.empty() ? forcedKind : ParseKind(kindText);
            if (kind == CompletionKind::Unknown) kind = forcedKind;
            const std::string type = ValueString(object, "type", name);
            std::string detail = ValueString(object, "signature");
            const std::string documentation = ValueString(object, "documentation");
            if (!documentation.empty()) {
                if (!detail.empty()) detail += "\n";
                detail += documentation;
            }
            CompletionEntry entry;
            entry.label = name;
            entry.insertText = name;
            entry.kind = kind;
            entry.typeName = type;
            entry.detail = std::move(detail);
            entry.documentation = documentation;
            entry.isDeprecated = ValueBool(object, "deprecated") || ValueBool(object, "isDeprecated");
            add(std::move(entry));
            rootTypes_[Lower(name)] = type;
            if (forcedKind == CompletionKind::Enum || kind == CompletionKind::Enum)
                addBuiltin("Enum", name, name, CompletionKind::EnumItem, detail, true, name);
            if (kind == CompletionKind::Service)
                addBuiltin("DataModel", name, name, CompletionKind::Service, detail, false, name, 15);
            if (forcedKind == CompletionKind::Class || forcedKind == CompletionKind::Service || kind == CompletionKind::Class || kind == CompletionKind::Service) {
                const std::string base = ValueString(object, "baseClass");
                if (!base.empty()) baseClasses_[Lower(name)] = base;
            }
            const std::string owner = name;
            if (object.contains("members")) ReadMembers(*this, object["members"], owner, CompletionKind::Method);
            if (object.contains("functions"))
                ReadMembers(*this, object["functions"], owner, CompletionKind::Method,
                            forcedKind == CompletionKind::Library);
            if (object.contains("constructors")) ReadMembers(*this, object["constructors"], owner, CompletionKind::Constructor, true);
            if (object.contains("constants")) ReadMembers(*this, object["constants"], owner, CompletionKind::Constant, true);
            if (object.contains("properties")) ReadMembers(*this, object["properties"], owner, CompletionKind::Property);
            if (object.contains("methods")) ReadMembers(*this, object["methods"], owner, CompletionKind::Method);
            if (object.contains("events")) ReadMembers(*this, object["events"], owner, CompletionKind::Event);
            if (object.contains("callbacks")) ReadMembers(*this, object["callbacks"], owner, CompletionKind::Callback);
            if (object.contains("items") && object["items"].is_array()) {
                for (const Json& item : object["items"]) {
                    const std::string itemName = ValueString(item, "name");
                    if (!itemName.empty()) addBuiltin(owner, itemName, itemName, CompletionKind::EnumItem,
                                                       ValueString(item, "documentation"), true);
                }
            }
        }
    };

    addRoots("globals", CompletionKind::Global);
    addRoots("libraries", CompletionKind::Library);
    addRoots("dataTypes", CompletionKind::DataType);
    addRoots("enums", CompletionKind::Enum);
    addRoots("classes", CompletionKind::Class);
    buildIndexes();
}

void CompletionDatabase::buildIndexes() {
    globalEntries_.clear();
    ownerEntries_.clear();
    memberNames_.clear();
    idToIndex_.clear();
    staticScopeNames_.clear();
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        const CompletionEntry& entry = entries_[i];
        idToIndex_[entry.id] = i;
        if (entry.owner.empty()) {
            globalEntries_.push_back(i);
            if (entry.kind == CompletionKind::DataType || entry.kind == CompletionKind::Enum)
                staticScopeNames_.insert(entry.lowerLabel);
        }
        else {
            ownerEntries_[Lower(entry.owner)].push_back(i);
            memberNames_[Lower(entry.owner) + "." + entry.lowerLabel] = i;
        }
    }
}

std::vector<const CompletionEntry*> CompletionDatabase::query(const CompletionContext& context) const {
    std::vector<const CompletionEntry*> result;
    const auto appendOwner = [&](std::string owner) {
        std::unordered_map<std::string, bool> visited;
        while (!owner.empty() && visited.emplace(Lower(owner), true).second) {
            const auto found = ownerEntries_.find(Lower(owner));
            if (found != ownerEntries_.end()) {
                for (std::uint32_t index : found->second) {
                    if (index >= entries_.size()) continue;
                    const CompletionEntry& item = entries_[index];
                    if ((context.kind == CompletionContextKind::StaticMember && !item.isStatic) ||
                        (context.kind == CompletionContextKind::MethodCall && item.kind != CompletionKind::Method && item.kind != CompletionKind::Event && item.kind != CompletionKind::Callback) ||
                        (context.kind == CompletionContextKind::SignalMember && item.kind != CompletionKind::SignalMethod && item.kind != CompletionKind::Method) ||
                        (context.kind == CompletionContextKind::InstanceMember && item.kind == CompletionKind::SignalMethod)) continue;
                    result.push_back(&item);
                }
            }
            const auto base = baseClasses_.find(Lower(owner));
            if (base == baseClasses_.end()) break;
            owner = base->second;
        }
    };

    switch (context.kind) {
        case CompletionContextKind::Global:
            result.reserve(globalEntries_.size());
            for (std::uint32_t index : globalEntries_)
                if (index < entries_.size()) result.push_back(&entries_[index]);
            break;
        case CompletionContextKind::DataModelMember:
            appendOwner("DataModel");
            break;
        case CompletionContextKind::WorkspaceMember:
            appendOwner("Workspace");
            break;
        case CompletionContextKind::StaticMember:
        case CompletionContextKind::InstanceMember:
        case CompletionContextKind::MethodCall:
        case CompletionContextKind::SignalMember:
            appendOwner(context.receiverType);
            break;
        default:
            break;
    }
    return result;
}

std::optional<std::string> CompletionDatabase::resolveType(std::string_view expression,
                                                            const SemanticSnapshot* snapshot) const {
    if (expression.empty()) return std::nullopt;
    const std::string normalized = Lower(expression);
    if (normalized.rfind("enum.", 0) == 0 && expression.size() > 5)
        return std::string(expression.substr(5));
    const auto root = rootTypes_.find(normalized);
    if (root != rootTypes_.end()) return root->second;
    if (normalized == "workspace") return "Workspace";
    if (normalized == "game") return "DataModel";
    if (normalized == "script") return "Script";
    if (snapshot) {
        const auto local = snapshot->localTypes.find(std::string(expression));
        if (local != snapshot->localTypes.end()) return local->second;
        for (const WorkspaceChild& child : snapshot->workspaceChildren)
            if (child.name == expression) return child.typeName;
    }
    const auto dot = expression.rfind('.');
    if (dot != std::string_view::npos) {
        const std::string_view prefix = expression.substr(0, dot);
        const std::string_view member = expression.substr(dot + 1);
        if (prefix == "game") {
            if (const CompletionEntry* service = findMember("DataModel", member);
                service && service->kind == CompletionKind::Service)
                return service->typeName.empty() ? service->label : service->typeName;
        }
        if (prefix == "game" && member == "Workspace") return "Workspace";
        if (prefix == "game" && member == "Players") return "Players";
        if (prefix == "game" && member == "ReplicatedStorage") return "ReplicatedStorage";
        if (prefix == "game" && member == "ServerScriptService") return "ServerScriptService";
        if (prefix == "game" && member == "ServerStorage") return "ServerStorage";
        if (prefix == "game" && member == "StarterPlayer") return "StarterPlayer";
        if (prefix == "game" && member == "StarterGui") return "StarterGui";
        if (prefix == "game" && member == "Lighting") return "Lighting";
        if (prefix == "workspace" && snapshot) {
            for (const WorkspaceChild& child : snapshot->workspaceChildren)
                if (child.name == member) return child.typeName;
        }
        if (member == "Character") return "Model";
        if (member == "Humanoid") return "Humanoid";
        if (member == "Color") return "Color3";
        if (member == "Position" || member == "Size" || member == "Orientation") return "Vector3";
    }
    if (normalized.find("part") != std::string::npos || normalized.find("block") != std::string::npos) return "Part";
    return std::nullopt;
}

const CompletionEntry* CompletionDatabase::findMember(std::string_view owner,
                                                       std::string_view name) const {
    std::string type(owner);
    std::unordered_map<std::string, bool> visited;
    while (!type.empty() && visited.emplace(Lower(type), true).second) {
        const auto found = memberNames_.find(Lower(type) + "." + Lower(name));
        if (found != memberNames_.end() && found->second < entries_.size()) return &entries_[found->second];
        const auto base = baseClasses_.find(Lower(type));
        if (base == baseClasses_.end()) break;
        type = base->second;
    }
    return nullptr;
}

bool CompletionDatabase::isDataType(std::string_view name) const {
    return staticScopeNames_.contains(Lower(name));
}

std::shared_ptr<const CompletionDatabase> CompletionIndexBuilder::Build(
    std::string_view jsonText, const CompletionDatabase& base, std::string* error) {
    try {
        auto result = std::make_shared<CompletionDatabase>(base);
        result->mergeCatalogJson(jsonText);
        return result;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return {};
    }
}

} // namespace blubox::engine::intellisense
