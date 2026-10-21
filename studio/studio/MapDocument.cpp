#include "MapDocument.hpp"

#include <cmath>
#include <random>
#include <unordered_set>
#include <stdexcept>

namespace blubox::studio {
namespace {

constexpr std::size_t kMaximumEntities = 100000;

std::string NewId() {
    static std::mt19937_64 random(std::random_device{}());
    constexpr char digits[] = "0123456789abcdef";
    std::uniform_int_distribution<unsigned int> nibble(0, 15);
    std::string id(32, '0');
    for (char& value : id) value = digits[nibble(random)];
    id.insert(8, "-");
    id.insert(13, "-");
    id.insert(18, "-");
    id.insert(23, "-");
    return id;
}

std::array<float, 3> ReadVector(const nlohmann::json& json, const char* key,
                                std::array<float, 3> fallback) {
    if (!json.contains(key)) return fallback;
    const auto& values = json.at(key);
    if (!values.is_array() || values.size() != 3) {
        throw std::runtime_error(std::string("Invalid transform vector: ") + key);
    }
    std::array<float, 3> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (!values[i].is_number()) throw std::runtime_error("Transform values must be numeric.");
        result[i] = values[i].get<float>();
        if (!std::isfinite(result[i]) || std::abs(result[i]) > 1000000.0f) {
            throw std::runtime_error("Transform value is outside the supported range.");
        }
    }
    return result;
}

} // namespace

MapDocument MapDocument::Create(std::string mapName) {
    MapDocument document;
    document.worldId = NewId();
    document.name = std::move(mapName);
    Entity floor = document.AddEntity("block");
    floor.name = "Baseplate";
    floor.transform.position = {0.0f, -0.5f, 0.0f};
    floor.transform.scale = {24.0f, 1.0f, 24.0f};
    floor.color = {92, 143, 116, 255};
    floor.components = nlohmann::json::array({{{"type", "Collider"}, {"shape", "box"}, {"static", true}}});
    Entity spawn = document.AddEntity("block");
    spawn.name = "SpawnPoint";
    spawn.transform.position = {0.0f, 0.5f, 0.0f};
    spawn.transform.scale = {1.0f, 1.0f, 1.0f};
    spawn.color = {244, 181, 72, 255};
    spawn.components = nlohmann::json::array({{{"type", "SpawnPoint"}}});
    document.Validate();
    return document;
}

MapDocument MapDocument::FromJson(const nlohmann::json& json) {
    if (!json.is_object() || json.value("format", std::string{}) != "blubox-map" ||
        json.value("formatVersion", 0) != 1) {
        throw std::runtime_error("Unsupported Blubox map format or version.");
    }

    MapDocument document;
    document.worldId = json.at("worldId").get<std::string>();
    document.name = json.at("name").get<std::string>();
    const auto& entries = json.at("entities");
    if (!entries.is_array() || entries.size() > kMaximumEntities) {
        throw std::runtime_error("The map entity count is invalid.");
    }

    document.entities.reserve(entries.size());
    for (const auto& entry : entries) {
        Entity entity;
        entity.id = entry.at("id").get<std::string>();
        entity.name = entry.at("name").get<std::string>();
        entity.kind = entry.at("kind").get<std::string>();
        entity.parent = entry.value("parent", std::string("Workspace"));
        const auto& transform = entry.at("transform");
        entity.transform.position = ReadVector(transform, "position", entity.transform.position);
        entity.transform.rotation = ReadVector(transform, "rotation", entity.transform.rotation);
        entity.transform.scale = ReadVector(transform, "scale", entity.transform.scale);
        if (entry.contains("color")) {
            const auto& color = entry.at("color");
            if (!color.is_array() || color.size() != 4) throw std::runtime_error("Invalid entity color.");
            for (std::size_t i = 0; i < entity.color.size(); ++i) {
                const int channel = color[i].get<int>();
                if (channel < 0 || channel > 255) throw std::runtime_error("Color channel out of range.");
                entity.color[i] = static_cast<unsigned char>(channel);
            }
        }
        if (entry.contains("components")) entity.components = entry.at("components");
        if (entry.contains("properties")) entity.properties = entry.at("properties");
        if (entry.contains("scripts")) {
            const auto& scripts = entry.at("scripts");
            if (!scripts.is_object()) throw std::runtime_error("Entity scripts must be an object.");
            if (scripts.contains("luau")) entity.luauScript = scripts.at("luau").get<std::string>();
        }
        entity.visible = entry.value("visible", true);
        document.entities.push_back(std::move(entity));
    }
    if (json.contains("scriptObjects")) {
        const auto& scripts = json.at("scriptObjects");
        if (!scripts.is_array() || scripts.size() > kMaximumEntities) {
            throw std::runtime_error("The map script count is invalid.");
        }
        if (document.entities.size() + scripts.size() > kMaximumEntities) {
            throw std::runtime_error("The map object and script count exceeds the limit.");
        }
        for (const auto& entry : scripts) {
            if (!entry.is_object()) throw std::runtime_error("Each script object must be an object.");
            Entity script;
            script.id = entry.at("id").get<std::string>();
            script.name = entry.at("name").get<std::string>();
            const std::string className = entry.at("className").get<std::string>();
            if (className != "Script" && className != "LocalScript") {
                throw std::runtime_error("Unsupported script class in map.");
            }
            script.kind = className == "LocalScript" ? "localscript" : "script";
            script.parent = entry.at("parent").get<std::string>();
            script.luauScript = entry.value("source", std::string{});
            document.entities.push_back(std::move(script));
        }
    }
    if (json.contains("serviceProperties")) {
        if (!json.at("serviceProperties").is_object()) throw std::runtime_error("Service properties must be an object.");
        document.serviceProperties = json.at("serviceProperties");
    }
    document.Validate();
    return document;
}

nlohmann::json MapDocument::ToJson() const {
    Validate();
    nlohmann::json json = {
        {"format", "blubox-map"},
        {"formatVersion", 1},
        {"coordinateSystem", {{"handedness", "right"}, {"up", "+Y"}, {"units", "meters"}}},
        {"worldId", worldId},
        {"name", name},
        {"serviceProperties", serviceProperties},
        {"entities", nlohmann::json::array()},
        {"scriptObjects", nlohmann::json::array()}
    };
    for (const Entity& entity : entities) {
        if (entity.kind == "script" || entity.kind == "localscript") {
            json["scriptObjects"].push_back({
                {"id", entity.id}, {"name", entity.name},
                {"className", entity.kind == "localscript" ? "LocalScript" : "Script"},
                {"parent", entity.parent}, {"source", entity.luauScript}
            });
            continue;
        }
        json["entities"].push_back({
            {"id", entity.id}, {"name", entity.name}, {"kind", entity.kind}, {"parent", entity.parent},
            {"transform", {{"position", entity.transform.position},
                           {"rotation", entity.transform.rotation},
                           {"scale", entity.transform.scale}}},
            {"color", entity.color}, {"components", entity.components}, {"properties", entity.properties},
            {"visible", entity.visible}, {"scripts", {{"luau", entity.luauScript}}}
        });
    }
    return json;
}

Entity& MapDocument::AddEntity(std::string kind) {
    if (kind != "block" && kind != "sphere" && kind != "script" && kind != "localscript" &&
        kind != "screengui" && kind != "frame" && kind != "textlabel" && kind != "textbutton") {
        throw std::runtime_error("Unsupported object kind.");
    }
    if (entities.size() >= kMaximumEntities) throw std::runtime_error("Map entity limit reached.");
    Entity entity;
    entity.id = NewId();
    entity.name = kind == "sphere" ? "Sphere" : kind == "script" ? "Script" :
                  kind == "localscript" ? "LocalScript" : kind == "screengui" ? "ScreenGui" :
                  kind == "frame" ? "Frame" : kind == "textlabel" ? "TextLabel" :
                  kind == "textbutton" ? "TextButton" : "Block";
    entity.kind = std::move(kind);
    if (entity.kind == "screengui") {
        entity.properties = {{"enabled", true}, {"ignoreGuiInset", false}, {"resetOnSpawn", true}};
    } else if (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton") {
        entity.properties = {{"position", {40, 40}}, {"size", {220, 64}},
                             {"backgroundColor", {34, 44, 58, 230}}, {"textColor", {242, 246, 252, 255}},
                             {"text", entity.kind == "textbutton" ? "Button" : entity.kind == "textlabel" ? "Label" : ""},
                             {"visible", true}};
    }
    entities.push_back(std::move(entity));
    return entities.back();
}

void MapDocument::Validate() const {
    if (worldId.empty() || worldId.size() > 64) throw std::runtime_error("Invalid world id.");
    if (name.empty() || name.size() > 128) throw std::runtime_error("Map name must be 1–128 characters.");
    if (entities.size() > kMaximumEntities) throw std::runtime_error("Map entity limit reached.");
    if (!serviceProperties.is_object() || serviceProperties.size() > 64) {
        throw std::runtime_error("Service properties must be an object with at most 64 entries.");
    }
    std::unordered_set<std::string> ids;
    ids.reserve(entities.size());
    for (const Entity& entity : entities) {
        if (entity.id.empty() || entity.id.size() > 64 || entity.name.empty() || entity.name.size() > 128) {
            throw std::runtime_error("Invalid entity id or name.");
        }
        if (!ids.insert(entity.id).second) throw std::runtime_error("Entity ids must be unique within a map.");
        if (entity.kind != "block" && entity.kind != "sphere" &&
            entity.kind != "script" && entity.kind != "localscript" && entity.kind != "screengui" &&
            entity.kind != "frame" && entity.kind != "textlabel" && entity.kind != "textbutton") {
            throw std::runtime_error("Unsupported object kind.");
        }
        if (entity.parent.empty() || entity.parent.size() > 128) {
            throw std::runtime_error("Invalid object parent.");
        }
        if (entity.luauScript.size() > 256 * 1024) throw std::runtime_error("Luau script exceeds 256 KiB.");
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(entity.transform.position[axis]) ||
                !std::isfinite(entity.transform.rotation[axis]) ||
                !std::isfinite(entity.transform.scale[axis]) ||
                std::abs(entity.transform.position[axis]) > 1000000.0f ||
                std::abs(entity.transform.rotation[axis]) > 1000000.0f ||
                entity.transform.scale[axis] < 0.001f || entity.transform.scale[axis] > 100000.0f) {
                throw std::runtime_error("Invalid entity transform.");
            }
        }
        if (!entity.components.is_array() || entity.components.size() > 128) {
            throw std::runtime_error("Entity components must be an array of at most 128 items.");
        }
        if (!entity.properties.is_object() || entity.properties.size() > 128) {
            throw std::runtime_error("Entity properties must be an object with at most 128 items.");
        }
        for (const auto& component : entity.components) {
            if (!component.is_object() || !component.contains("type") || !component.at("type").is_string()) {
                throw std::runtime_error("Each entity component requires a string type.");
            }
        }
    }
}

} // namespace blubox::studio
