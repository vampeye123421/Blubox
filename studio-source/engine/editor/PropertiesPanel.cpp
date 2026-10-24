#include "PropertiesPanel.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace blubox::engine::editor {
namespace {
using namespace reflection;

std::string ClassFor(const studio::Entity& entity) {
    if (entity.kind == "block") return "Part";
    if (entity.kind == "spawnlocation") return "SpawnLocation";
    if (entity.kind == "sphere") return "Sphere";
    if (entity.kind == "wedgepart") return "WedgePart";
    if (entity.kind == "cornerwedgepart") return "CornerWedgePart";
    if (entity.kind == "meshpart") return "MeshPart";
    if (entity.kind == "decal") return "Decal";
    if (entity.kind == "localscript") return "LocalScript";
    if (entity.kind == "script") return "Script";
    if (entity.kind == "screengui") return "ScreenGui";
    if (entity.kind == "frame") return "Frame";
    if (entity.kind == "textlabel") return "TextLabel";
    if (entity.kind == "textbutton") return "TextButton";
    return entity.kind == "model" ? "Model" : "Folder";
}

Color ReadColor(const nlohmann::json& value, Color fallback = {}) {
    if (!value.is_array() || value.size() < 3) return fallback;
    auto channel = [&](std::size_t i, std::uint8_t defaultValue) {
        if (i >= value.size() || !value[i].is_number_integer()) return defaultValue;
        return static_cast<std::uint8_t>(std::clamp(value[i].get<int>(), 0, 255));
    };
    return {channel(0, fallback.r), channel(1, fallback.g), channel(2, fallback.b), channel(3, fallback.a)};
}

double ReadTransparency(const studio::Entity& entity, const char* property,
                        const char* colorProperty, Color fallback) {
    if (entity.properties.contains(property) && entity.properties[property].is_number())
        return std::clamp(entity.properties[property].get<double>(), 0.0, 1.0);
    const Color color = ReadColor(entity.properties.value(colorProperty, nlohmann::json::array()), fallback);
    return 1.0 - static_cast<double>(color.a) / 255.0;
}

Vector2 ReadVector2(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_number() || !value[1].is_number()) return {};
    return {value[0].get<float>(), value[1].get<float>()};
}

bool Read(const studio::Entity& entity, std::string_view name, PropertyValue& value) {
    if (name == "Name") value = entity.name;
    else if (name == "Position" && (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton")) {
        const auto p = entity.properties.value("position", nlohmann::json::array({40, 40}));
        value = Vector2{p.is_array() && p.size() == 2 ? static_cast<float>(p[0].get<int>()) : 40.0f,
                        p.is_array() && p.size() == 2 ? static_cast<float>(p[1].get<int>()) : 40.0f};
    } else if (name == "PositionScale") value = ReadVector2(entity.properties.value("positionScale", nlohmann::json::array({0.0, 0.0})));
    else if (name == "Position") value = Vec3{entity.transform.position[0], entity.transform.position[1], entity.transform.position[2]};
    else if (name == "Rotation" && entity.kind == "decal") value = entity.properties.value("rotationDegrees", 0.0);
    else if (name == "Rotation") value = Vec3{entity.transform.rotation[0], entity.transform.rotation[1], entity.transform.rotation[2]};
    else if (name == "Size" && (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton" || entity.kind == "decal")) {
        const auto s = entity.properties.value("size", nlohmann::json::array({220, 64}));
        value = Vector2{s.is_array() && s.size() == 2 ? static_cast<float>(s[0].get<int>()) : 220.0f,
                        s.is_array() && s.size() == 2 ? static_cast<float>(s[1].get<int>()) : 64.0f};
    } else if (name == "SizeScale") value = ReadVector2(entity.properties.value("sizeScale", nlohmann::json::array({0.0, 0.0})));
    else if (name == "Size") value = Vec3{entity.transform.scale[0], entity.transform.scale[1], entity.transform.scale[2]};
    else if (name == "Color") value = entity.kind == "decal" ? ReadColor(entity.properties.value("tint", nlohmann::json::array({255,255,255,255}))) : ReadColor(nlohmann::json(entity.color));
    else if (name == "Anchored") value = entity.properties.value("Anchored", true);
    else if (name == "Visible") value = entity.kind.starts_with("text") || entity.kind == "frame" ? entity.properties.value("visible", true) : entity.visible;
    else if (name == "Material") value = AssetReference{entity.properties.value("materialId", std::uint64_t{0})};
    else if (name == "MeshAsset") value = AssetReference{entity.properties.value("meshAssetId", std::uint64_t{0})};
    else if (name == "AlbedoMap") value = AssetReference{entity.properties.value("albedoMap", std::uint64_t{0})};
    else if (name == "NormalMap") value = AssetReference{entity.properties.value("normalMap", std::uint64_t{0})};
    else if (name == "RoughnessMap") value = AssetReference{entity.properties.value("roughnessMap", std::uint64_t{0})};
    else if (name == "MetalnessMap") value = AssetReference{entity.properties.value("metalnessMap", std::uint64_t{0})};
    else if (name == "EmissiveMap") value = AssetReference{entity.properties.value("emissiveMap", std::uint64_t{0})};
    else if (name == "Texture") value = AssetReference{entity.properties.value("textureId", std::uint64_t{0})};
    else if (name == "Roughness") value = entity.properties.value("roughness", 0.8);
    else if (name == "Metalness") value = entity.properties.value("metalness", 0.0);
    else if (name == "EmissiveIntensity") value = entity.properties.value("emissiveIntensity", 0.0);
    else if (name == "UvScale") { const auto v = entity.properties.value("uvScale", nlohmann::json::array({1.0,1.0})); value = Vector2{v.size() == 2 ? v[0].get<float>() : 1.0f, v.size() == 2 ? v[1].get<float>() : 1.0f}; }
    else if (name == "UvOffset") { const auto v = entity.properties.value("uvOffset", nlohmann::json::array({0.0,0.0})); value = Vector2{v.size() == 2 ? v[0].get<float>() : 0.0f, v.size() == 2 ? v[1].get<float>() : 0.0f}; }
    else if (name == "UvRotation") value = entity.properties.value("uvRotationDegrees", 0.0);
    else if (name == "Transparency") {
        if (entity.kind == "decal") value = entity.properties.value("transparency", 0.0);
        else if (entity.properties.contains("transparency") && entity.properties["transparency"].is_number())
            value = std::clamp(entity.properties["transparency"].get<double>(), 0.0, 1.0);
        else value = 1.0 - static_cast<double>(entity.color[3]) / 255.0;
    }
    else if (name == "NormalBlend") value = entity.properties.value("normalBlend", 0.0);
    else if (name == "DepthTest") value = entity.properties.value("depthTest", true);
    else if (name == "Source") value = entity.luauScript;
    else if (name == "Enabled") value = entity.kind == "screengui" ? entity.properties.value("enabled", true) : entity.properties.value("Enabled", true);
    else if (name == "Duration") value = entity.properties.value("Duration", 0.0);
    else if (name == "Neutral") value = entity.properties.value("Neutral", true);
    else if (name == "IgnoreGuiInset") value = entity.properties.value("ignoreGuiInset", false);
    else if (name == "ResetOnSpawn") value = entity.properties.value("resetOnSpawn", true);
    else if (name == "TeamColor") value = ReadColor(entity.properties.value("TeamColor", nlohmann::json::array({194, 194, 194, 255})));
    else if (name == "AllowTeamChangeOnTouch") value = entity.properties.value("AllowTeamChangeOnTouch", false);
    else if (name == "BackgroundColor") value = ReadColor(entity.properties.value("backgroundColor", nlohmann::json::array({34, 44, 58, 230})));
    else if (name == "TextColor") value = ReadColor(entity.properties.value("textColor", nlohmann::json::array({242, 246, 252, 255})));
    else if (name == "Text") value = entity.properties.value("text", std::string{});
    else if (name == "BackgroundTransparency") value = ReadTransparency(entity, "backgroundTransparency", "backgroundColor", Color{34, 44, 58, 230});
    else if (name == "TextTransparency") value = ReadTransparency(entity, "textTransparency", "textColor", Color{242, 246, 252, 255});
    else if (name == "ScaleWithViewport") value = entity.properties.value("scaleWithViewport", true);
    else if (name == "TextScaled") value = entity.properties.value("textScaled", false);
    else if (name == "TextWrapped") value = entity.properties.value("textWrapped", false);
    else if (name == "TextTruncate") value = entity.properties.value("textTruncate", false);
    else if (name == "FontSize") value = entity.properties.value("fontSize", 20.0);
    else if (name == "HorizontalAlignment") value = entity.properties.value("horizontalAlignment", std::string("Left"));
    else if (name == "VerticalAlignment") value = entity.properties.value("verticalAlignment", std::string("Top"));
    else if (name == "TextXAlignment") value = entity.properties.value("textXAlignment", std::string("Left"));
    else if (name == "TextYAlignment") value = entity.properties.value("textYAlignment", std::string("Center"));
    else if (name == "Brightness") value = entity.properties.value("Brightness", 1.0);
    else if (name == "FieldOfView") value = entity.properties.value("FieldOfView", 60.0);
    else return false;
    return true;
}

bool Write(studio::Entity& entity, std::string_view name, const PropertyValue& value) {
    if (name == "Name") entity.name = std::get<std::string>(value);
    else if (name == "Position" && (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton")) { auto v = std::get<Vector2>(value); entity.properties["position"] = {static_cast<int>(v.x), static_cast<int>(v.y)}; }
    else if (name == "PositionScale") { auto v = std::get<Vector2>(value); entity.properties["positionScale"] = {v.x, v.y}; }
    else if (name == "Position") { auto v = std::get<Vec3>(value); entity.transform.position = {v.x, v.y, v.z}; }
    else if (name == "Rotation" && entity.kind == "decal") entity.properties["rotationDegrees"] = std::get<double>(value);
    else if (name == "Rotation") { auto v = std::get<Vec3>(value); entity.transform.rotation = {v.x, v.y, v.z}; }
    else if (name == "Size" && (entity.kind == "frame" || entity.kind == "textlabel" || entity.kind == "textbutton")) { auto v = std::get<Vector2>(value); entity.properties["size"] = {static_cast<int>(v.x), static_cast<int>(v.y)}; }
    else if (name == "SizeScale") { auto v = std::get<Vector2>(value); entity.properties["sizeScale"] = {v.x, v.y}; }
    else if (name == "Size" && entity.kind == "decal") { auto v = std::get<Vector2>(value); entity.properties["size"] = {v.x, v.y}; }
    else if (name == "Size") { auto v = std::get<Vec3>(value); entity.transform.scale = {v.x, v.y, v.z}; }
    else if (name == "Color" && entity.kind == "decal") { auto c = std::get<Color>(value); entity.properties["tint"] = {c.r,c.g,c.b,255}; }
    else if (name == "Color") {
        auto c = std::get<Color>(value);
        if (!entity.properties.contains("transparency"))
            entity.properties["transparency"] = 1.0 - static_cast<double>(entity.color[3]) / 255.0;
        entity.color = {c.r, c.g, c.b, 255};
    }
    else if (name == "Anchored") entity.properties["Anchored"] = std::get<bool>(value);
    else if (name == "Visible") {
        if (entity.kind.starts_with("text") || entity.kind == "frame") entity.properties["visible"] = std::get<bool>(value);
        else entity.visible = std::get<bool>(value);
    } else if (name == "Material") entity.properties["materialId"] = std::get<AssetReference>(value).id;
    else if (name == "MeshAsset") entity.properties["meshAssetId"] = std::get<AssetReference>(value).id;
    else if (name == "AlbedoMap") entity.properties["albedoMap"] = std::get<AssetReference>(value).id;
    else if (name == "NormalMap") entity.properties["normalMap"] = std::get<AssetReference>(value).id;
    else if (name == "RoughnessMap") entity.properties["roughnessMap"] = std::get<AssetReference>(value).id;
    else if (name == "MetalnessMap") entity.properties["metalnessMap"] = std::get<AssetReference>(value).id;
    else if (name == "EmissiveMap") entity.properties["emissiveMap"] = std::get<AssetReference>(value).id;
    else if (name == "Texture") entity.properties["textureId"] = std::get<AssetReference>(value).id;
    else if (name == "Roughness") entity.properties["roughness"] = std::get<double>(value);
    else if (name == "Metalness") entity.properties["metalness"] = std::get<double>(value);
    else if (name == "EmissiveIntensity") entity.properties["emissiveIntensity"] = std::get<double>(value);
    else if (name == "UvScale") { auto v = std::get<Vector2>(value); entity.properties["uvScale"] = {v.x,v.y}; }
    else if (name == "UvOffset") { auto v = std::get<Vector2>(value); entity.properties["uvOffset"] = {v.x,v.y}; }
    else if (name == "UvRotation") entity.properties["uvRotationDegrees"] = std::get<double>(value);
    else if (name == "Transparency") entity.properties["transparency"] = std::get<double>(value);
    else if (name == "NormalBlend") entity.properties["normalBlend"] = std::get<double>(value);
    else if (name == "DepthTest") entity.properties["depthTest"] = std::get<bool>(value);
    else if (name == "Source") entity.luauScript = std::get<std::string>(value);
    else if (name == "Enabled") entity.properties[entity.kind == "screengui" ? "enabled" : "Enabled"] = std::get<bool>(value);
    else if (name == "Duration") entity.properties["Duration"] = std::get<double>(value);
    else if (name == "Neutral") entity.properties["Neutral"] = std::get<bool>(value);
    else if (name == "IgnoreGuiInset") entity.properties["ignoreGuiInset"] = std::get<bool>(value);
    else if (name == "ResetOnSpawn") entity.properties["resetOnSpawn"] = std::get<bool>(value);
    else if (name == "TeamColor") { auto c = std::get<Color>(value); entity.properties["TeamColor"] = {c.r, c.g, c.b, c.a}; }
    else if (name == "AllowTeamChangeOnTouch") entity.properties["AllowTeamChangeOnTouch"] = std::get<bool>(value);
    else if (name == "BackgroundColor") {
        auto c = std::get<Color>(value);
        if (!entity.properties.contains("backgroundTransparency")) {
            const auto old = ReadColor(entity.properties.value("backgroundColor", nlohmann::json::array()), Color{34, 44, 58, 230});
            entity.properties["backgroundTransparency"] = 1.0 - static_cast<double>(old.a) / 255.0;
        }
        entity.properties["backgroundColor"] = {c.r, c.g, c.b, 255};
    }
    else if (name == "TextColor") {
        auto c = std::get<Color>(value);
        if (!entity.properties.contains("textTransparency")) {
            const auto old = ReadColor(entity.properties.value("textColor", nlohmann::json::array()), Color{242, 246, 252, 255});
            entity.properties["textTransparency"] = 1.0 - static_cast<double>(old.a) / 255.0;
        }
        entity.properties["textColor"] = {c.r, c.g, c.b, 255};
    }
    else if (name == "Text") entity.properties["text"] = std::get<std::string>(value);
    else if (name == "BackgroundTransparency") entity.properties["backgroundTransparency"] = std::get<double>(value);
    else if (name == "TextTransparency") entity.properties["textTransparency"] = std::get<double>(value);
    else if (name == "ScaleWithViewport") entity.properties["scaleWithViewport"] = std::get<bool>(value);
    else if (name == "TextScaled") entity.properties["textScaled"] = std::get<bool>(value);
    else if (name == "TextWrapped") entity.properties["textWrapped"] = std::get<bool>(value);
    else if (name == "TextTruncate") entity.properties["textTruncate"] = std::get<bool>(value);
    else if (name == "FontSize") entity.properties["fontSize"] = std::get<double>(value);
    else if (name == "HorizontalAlignment") entity.properties["horizontalAlignment"] = std::get<std::string>(value);
    else if (name == "VerticalAlignment") entity.properties["verticalAlignment"] = std::get<std::string>(value);
    else if (name == "TextXAlignment") entity.properties["textXAlignment"] = std::get<std::string>(value);
    else if (name == "TextYAlignment") entity.properties["textYAlignment"] = std::get<std::string>(value);
    else if (name == "Brightness" || name == "FieldOfView") entity.properties[std::string(name)] = std::get<double>(value);
    else return false;
    return true;
}

bool DrawEditor(const PropertyDescriptor& descriptor, PropertyValue& value) {
    ImGui::PushID(descriptor.name.c_str());
    bool changed = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(descriptor.displayName.c_str());
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::BeginDisabled(descriptor.readOnly);
    switch (descriptor.type) {
    case PropertyType::Boolean: { bool v = std::get<bool>(value); changed = ImGui::Checkbox("##value", &v); if (changed) value = v; break; }
    case PropertyType::Float: {
        double v = std::get<double>(value);
        if (descriptor.name == "Transparency" || descriptor.name == "BackgroundTransparency" || descriptor.name == "TextTransparency") {
            float opacity = static_cast<float>(std::clamp(v, 0.0, 1.0));
            changed = ImGui::SliderFloat("##value", &opacity, 0.0f, 1.0f, "%.0f%%");
            if (changed) v = opacity;
        } else {
            changed = ImGui::InputDouble("##value", &v, 0.0, 0.0, "%.3f");
        }
        if (changed) {
            if (!std::isfinite(v)) v = 0.0;
            value = std::clamp(v, descriptor.minimum, descriptor.maximum);
        }
        break;
    }
    case PropertyType::Integer: { auto v = std::get<std::int64_t>(value); long long n = v; changed = ImGui::InputScalar("##value", ImGuiDataType_S64, &n); if (changed) value = static_cast<std::int64_t>(n); break; }
    case PropertyType::String: { auto v = std::get<std::string>(value); changed = ImGui::InputText("##value", &v); if (changed) value = std::move(v); break; }
    case PropertyType::Vector3: {
        const auto v = std::get<Vec3>(value);
        float values[]{v.x, v.y, v.z};
        changed = ImGui::InputFloat3("##value", values, "%.3f");
        if (changed) {
            for (float& component : values) {
                if (!std::isfinite(component)) component = 0.0f;
                component = std::clamp(component, static_cast<float>(descriptor.minimum),
                                       static_cast<float>(descriptor.maximum));
            }
            value = Vec3{values[0], values[1], values[2]};
        }
        break;
    }
    case PropertyType::Vector2: {
        const auto v = std::get<Vector2>(value);
        float values[]{v.x, v.y};
        changed = ImGui::InputFloat2("##value", values, "%.2f");
        if (changed) {
            for (float& component : values) {
                if (!std::isfinite(component)) component = 0.0f;
                component = std::clamp(component, static_cast<float>(descriptor.minimum),
                                       static_cast<float>(descriptor.maximum));
            }
            value = Vector2{values[0], values[1]};
        }
        break;
    }
    case PropertyType::Color: { auto c = std::get<Color>(value); float values[]{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; changed = ImGui::ColorEdit3("##value", values); if (changed) value = Color{static_cast<std::uint8_t>(std::clamp(values[0], 0.0f, 1.0f) * 255.0f + 0.5f), static_cast<std::uint8_t>(std::clamp(values[1], 0.0f, 1.0f) * 255.0f + 0.5f), static_cast<std::uint8_t>(std::clamp(values[2], 0.0f, 1.0f) * 255.0f + 0.5f), 255}; break; }
    case PropertyType::Enum: {
        const auto selected = std::get<std::string>(value);
        int current = 0;
        for (std::size_t i = 0; i < descriptor.enumValues.size(); ++i)
            if (descriptor.enumValues[i] == selected) { current = static_cast<int>(i); break; }
        std::vector<const char*> labels;
        labels.reserve(descriptor.enumValues.size());
        for (const auto& option : descriptor.enumValues) labels.push_back(option.c_str());
        if (!labels.empty() && ImGui::Combo("##value", &current, labels.data(), static_cast<int>(labels.size()))) {
            value = descriptor.enumValues[static_cast<std::size_t>(current)];
            changed = true;
        }
        break;
    }
    case PropertyType::AssetReference: { auto asset = std::get<AssetReference>(value).id; unsigned long long id = asset; changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &id); if (changed) value = AssetReference{static_cast<assets::AssetId>(id)}; break; }
    default: ImGui::TextDisabled("Not editable"); break;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}
}

std::uint32_t PropertiesPanel::draw(studio::Entity& entity) const {
    const auto className = ClassFor(entity);
    const auto descriptors = reflection_.properties(className);
    std::uint32_t changedFlags = reflection::ChangeNone;
    std::string activeCategory;
    for (const auto* descriptor : descriptors) {
        if (!descriptor || descriptor->hidden) continue;
        if (descriptor->category != activeCategory) {
            activeCategory = descriptor->category;
            ImGui::Spacing();
            ImGui::TextDisabled("%s", activeCategory.c_str());
            ImGui::Separator();
        }
        reflection::PropertyValue value;
        if (!Read(entity, descriptor->name, value)) continue;
        if (DrawEditor(*descriptor, value) && Write(entity, descriptor->name, value)) changedFlags |= descriptor->changeFlags;
    }
    return changedFlags;
}

} // namespace blubox::engine::editor
