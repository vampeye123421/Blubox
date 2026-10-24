#pragma once

#include <array>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace blubox::studio {

struct Transform {
    std::array<float, 3> position{0.0f, 0.0f, 0.0f};
    std::array<float, 3> rotation{0.0f, 0.0f, 0.0f};
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

struct Entity {
    std::string id;
    std::string name;
    std::string kind = "block";
    // Workspace is the default parent for world geometry. Script instances may
    // use a service name or a parent entity id.
    std::string parent = "Workspace";
    Transform transform;
    std::array<unsigned char, 4> color{75, 163, 226, 255};
    nlohmann::json components = nlohmann::json::array();
    nlohmann::json properties = nlohmann::json::object();
    std::string luauScript;
    bool visible = true;
};

class MapDocument {
public:
    std::string worldId;
    std::string name = "Untitled World";
    std::vector<Entity> entities;
    nlohmann::json serviceProperties = nlohmann::json::object();

    static MapDocument Create(std::string name);
    static MapDocument FromJson(const nlohmann::json& json);
    nlohmann::json ToJson() const;
    Entity& AddEntity(std::string kind);
    void Validate() const;
};

} // namespace blubox::studio
