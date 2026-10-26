#include "ReflectionRegistry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace blubox::engine::reflection {
namespace {
PropertyDescriptor Property(std::string name, PropertyType type, std::string category,
                            double min = 0.0, double max = 1.0, std::uint32_t flags = ChangeNone) {
    return {name, name, type, std::move(category), false, false, true, false, min, max, {}, flags};
}

bool ValidValue(const PropertyDescriptor& property, const PropertyValue& value) {
    switch (property.type) {
    case PropertyType::Boolean: return std::holds_alternative<bool>(value);
    case PropertyType::Integer: return std::holds_alternative<std::int64_t>(value);
    case PropertyType::Float: return std::holds_alternative<double>(value) && std::isfinite(std::get<double>(value)) &&
        std::get<double>(value) >= property.minimum && std::get<double>(value) <= property.maximum;
    case PropertyType::String: return std::holds_alternative<std::string>(value) && std::get<std::string>(value).size() <= 65536;
    case PropertyType::Enum:
        return std::holds_alternative<std::string>(value) &&
            std::find(property.enumValues.begin(), property.enumValues.end(), std::get<std::string>(value)) != property.enumValues.end();
    case PropertyType::Vector2: return std::holds_alternative<Vector2>(value) && std::isfinite(std::get<Vector2>(value).x) && std::isfinite(std::get<Vector2>(value).y);
    case PropertyType::Vector3: return std::holds_alternative<Vec3>(value) && std::isfinite(std::get<Vec3>(value).x) && std::isfinite(std::get<Vec3>(value).y) && std::isfinite(std::get<Vec3>(value).z);
    case PropertyType::Quaternion: return std::holds_alternative<Quaternion>(value);
    case PropertyType::Color: return std::holds_alternative<Color>(value);
    case PropertyType::AssetReference: return std::holds_alternative<AssetReference>(value);
    case PropertyType::InstanceReference: return std::holds_alternative<InstanceReference>(value);
    default: return false;
    }
}

std::string ClassName(ClassId id) {
    switch (id) {
    case ClassId::Part: return "Part"; case ClassId::SpawnLocation: return "SpawnLocation";
    case ClassId::Sphere: return "Sphere"; case ClassId::WedgePart: return "WedgePart";
    case ClassId::CornerWedgePart: return "CornerWedgePart"; case ClassId::MeshPart: return "MeshPart";
    case ClassId::Shirt: return "Shirt"; case ClassId::Pants: return "Pants";
    case ClassId::Accessory: return "Accessory";
    case ClassId::Model: return "Model"; case ClassId::Folder: return "Folder";
    case ClassId::Script: return "Script"; case ClassId::LocalScript: return "LocalScript";
    case ClassId::Light: return "Light"; case ClassId::Camera: return "Camera";
    case ClassId::ScreenGui: return "ScreenGui"; case ClassId::Frame: return "Frame";
    case ClassId::TextLabel: return "TextLabel"; case ClassId::TextButton: return "TextButton";
    default: return "Instance";
    }
}

PropertyDescriptor PartProperty(std::string name, PropertyType type, std::string category,
                                std::uint32_t flags = ChangeNone, double min = 0.0, double max = 1.0) {
    return Property(std::move(name), type, std::move(category), min, max, flags);
}

PropertyDescriptor EnumProperty(std::string name, std::string category,
                                std::initializer_list<const char*> values,
                                std::uint32_t flags = ChangeRender) {
    auto property = Property(std::move(name), PropertyType::Enum, std::move(category), 0.0, 0.0, flags);
    for (const char* value : values) property.enumValues.emplace_back(value);
    return property;
}
}

bool ReflectionRegistry::registerClass(ClassDescriptor descriptor) {
    if (descriptor.name.empty() || descriptor.name.size() > 128) return false;
    if (!descriptor.baseClass.empty() && !classes_.contains(descriptor.baseClass)) return false;
    std::sort(descriptor.properties.begin(), descriptor.properties.end(), [](const auto& a, const auto& b) {
        if (a.category != b.category) return a.category < b.category;
        return a.displayName < b.displayName;
    });
    return classes_.emplace(descriptor.name, std::move(descriptor)).second;
}

const ClassDescriptor* ReflectionRegistry::findClass(std::string_view name) const noexcept {
    auto found = classes_.find(std::string(name));
    return found == classes_.end() ? nullptr : &found->second;
}

const PropertyDescriptor* ReflectionRegistry::findProperty(std::string_view className, std::string_view propertyName) const {
    const ClassDescriptor* descriptor = findClass(className);
    while (descriptor) {
        auto found = std::find_if(descriptor->properties.begin(), descriptor->properties.end(), [&](const auto& p) { return p.name == propertyName; });
        if (found != descriptor->properties.end()) return &*found;
        descriptor = descriptor->baseClass.empty() ? nullptr : findClass(descriptor->baseClass);
    }
    return nullptr;
}

std::vector<const PropertyDescriptor*> ReflectionRegistry::properties(std::string_view className) const {
    std::vector<const ClassDescriptor*> chain;
    for (auto* current = findClass(className); current; current = current->baseClass.empty() ? nullptr : findClass(current->baseClass)) chain.push_back(current);
    std::vector<const PropertyDescriptor*> result;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (const auto& property : (*it)->properties) {
            auto existing = std::find_if(result.begin(), result.end(), [&](const auto* item) { return item->name == property.name; });
            if (existing == result.end()) result.push_back(&property); else *existing = &property;
        }
    }
    return result;
}

bool ReflectionRegistry::get(const InstanceRegistry& world, InstanceHandle handle, std::string_view name, PropertyValue& output) const {
    const auto* instance = world.get(handle);
    if (!instance || !findProperty(ClassName(instance->classId), name)) return false;
    if (name == "Name") output = instance->name;
    else if (name == "Position") output = instance->local.position;
    else if (name == "Rotation") output = instance->local.rotation;
    else if (name == "Size") output = instance->size;
    else if (name == "Color" || name == "TeamColor") output = Color{instance->color[0], instance->color[1], instance->color[2], instance->color[3]};
    else if (name == "Anchored") output = instance->anchored;
    else if (name == "Visible") output = instance->visible;
    else if (name == "Transparency") output = static_cast<double>(instance->transparency);
    else if (name == "Material") output = AssetReference{instance->materialId};
    else if (name == "MeshAsset") output = AssetReference{instance->meshAssetId};
    else if (name == "Source") output = instance->source;
    else if (name == "Enabled" || name == "AllowTeamChangeOnTouch") output = instance->enabled;
    else if (name == "Duration") output = static_cast<double>(instance->duration);
    else if (name == "Brightness") output = static_cast<double>(instance->brightness);
    else return false;
    return true;
}

bool ReflectionRegistry::set(InstanceRegistry& world, InstanceHandle handle, std::string_view name,
                             const PropertyValue& value, PropertyChangeSink* sink) const {
    auto* instance = world.get(handle);
    if (!instance) return false;
    const auto* property = findProperty(ClassName(instance->classId), name);
    if (!property || property->readOnly || !ValidValue(*property, value)) return false;
    if (name == "Name") instance->name = std::get<std::string>(value);
    else if (name == "Position") instance->local.position = std::get<Vec3>(value);
    else if (name == "Rotation") instance->local.rotation = std::get<Vec3>(value);
    else if (name == "Size") instance->size = std::get<Vec3>(value);
    else if (name == "Color" || name == "TeamColor") {
        const auto c = std::get<Color>(value); instance->color = {c.r, c.g, c.b, c.a};
    } else if (name == "Anchored") instance->anchored = std::get<bool>(value);
    else if (name == "Visible") instance->visible = std::get<bool>(value);
    else if (name == "Transparency") instance->transparency = static_cast<float>(std::get<double>(value));
    else if (name == "Material") instance->materialId = std::get<AssetReference>(value).id;
    else if (name == "MeshAsset") instance->meshAssetId = std::get<AssetReference>(value).id;
    else if (name == "Source") instance->source = std::get<std::string>(value);
    else if (name == "Enabled" || name == "AllowTeamChangeOnTouch") instance->enabled = std::get<bool>(value);
    else if (name == "Duration") instance->duration = static_cast<float>(std::get<double>(value));
    else if (name == "Brightness") instance->brightness = static_cast<float>(std::get<double>(value));
    else return false;
    if ((property->changeFlags & ChangeTransform) != 0) world.markTransformDirty(handle);
    if (sink) sink->onPropertyChanged(handle, *property);
    return true;
}

void RegisterBuiltinReflection(ReflectionRegistry& registry) {
    ClassDescriptor part{"Part", ""};
    part.properties = {
        PartProperty("Name", PropertyType::String, "Data", ChangeNone, 0, 128),
        PartProperty("Position", PropertyType::Vector3, "Transform", ChangeTransform | ChangePhysics | ChangeRender | ChangeNetwork, -1000000, 1000000),
        PartProperty("Rotation", PropertyType::Vector3, "Transform", ChangeTransform | ChangePhysics | ChangeRender | ChangeNetwork, -1000000, 1000000),
        PartProperty("Size", PropertyType::Vector3, "Transform", ChangeTransform | ChangePhysics | ChangeRender | ChangeNetwork, 0.001, 100000),
        PartProperty("Color", PropertyType::Color, "Appearance", ChangeRender | ChangeNetwork, 0, 255),
        PartProperty("Transparency", PropertyType::Float, "Appearance", ChangeRender, 0, 1),
        PartProperty("Anchored", PropertyType::Boolean, "Physics", ChangePhysics),
        PartProperty("Visible", PropertyType::Boolean, "Appearance", ChangeRender),
        PartProperty("Material", PropertyType::AssetReference, "Appearance", ChangeMaterial | ChangeRender, 0, std::numeric_limits<double>::max()),
        PartProperty("AlbedoMap", PropertyType::AssetReference, "Surface Appearance", ChangeMaterial | ChangeRender, 0, std::numeric_limits<double>::max()),
        PartProperty("NormalMap", PropertyType::AssetReference, "Surface Appearance", ChangeMaterial | ChangeRender, 0, std::numeric_limits<double>::max()),
        PartProperty("RoughnessMap", PropertyType::AssetReference, "Surface Appearance", ChangeMaterial | ChangeRender, 0, std::numeric_limits<double>::max()),
        PartProperty("MetalnessMap", PropertyType::AssetReference, "Surface Appearance", ChangeMaterial | ChangeRender, 0, std::numeric_limits<double>::max()),
        PartProperty("EmissiveMap", PropertyType::AssetReference, "Surface Appearance", ChangeMaterial | ChangeRender, 0, std::numeric_limits<double>::max()),
        PartProperty("Roughness", PropertyType::Float, "Surface Appearance", ChangeMaterial | ChangeRender, 0, 1),
        PartProperty("Metalness", PropertyType::Float, "Surface Appearance", ChangeMaterial | ChangeRender, 0, 1),
        PartProperty("EmissiveIntensity", PropertyType::Float, "Surface Appearance", ChangeMaterial | ChangeRender, 0, 1000),
        PartProperty("UvScale", PropertyType::Vector2, "Surface Appearance", ChangeMaterial | ChangeRender, -1000, 1000),
        PartProperty("UvOffset", PropertyType::Vector2, "Surface Appearance", ChangeMaterial | ChangeRender, -100000, 100000),
        PartProperty("UvRotation", PropertyType::Float, "Surface Appearance", ChangeMaterial | ChangeRender, -360000, 360000)};
    registry.registerClass(std::move(part));
    ClassDescriptor spawn{"SpawnLocation", "Part"};
    spawn.properties = {PartProperty("Enabled", PropertyType::Boolean, "Behavior", ChangeSpawn),
                        PartProperty("Duration", PropertyType::Float, "Behavior", ChangeSpawn, 0, 3600),
                        PartProperty("TeamColor", PropertyType::Color, "Behavior", ChangeSpawn, 0, 255),
                        PartProperty("Neutral", PropertyType::Boolean, "Behavior", ChangeSpawn),
                        PartProperty("AllowTeamChangeOnTouch", PropertyType::Boolean, "Behavior", ChangeSpawn)};
    registry.registerClass(std::move(spawn));
    ClassDescriptor mesh{"MeshPart", "Part"};
    mesh.properties.push_back(PartProperty("MeshAsset", PropertyType::AssetReference, "Geometry", ChangeRender, 0, std::numeric_limits<double>::max()));
    registry.registerClass(std::move(mesh));
    for (const char* name : {"Sphere", "WedgePart", "CornerWedgePart"}) registry.registerClass({name, "Part", {}});
    registry.registerClass({"Model", "", {PartProperty("Name", PropertyType::String, "Data", ChangeNone, 0, 128)}});
    registry.registerClass({"Folder", "", {PartProperty("Name", PropertyType::String, "Data", ChangeNone, 0, 128)}});
    registry.registerClass({"Script", "", {PartProperty("Name", PropertyType::String, "Data", ChangeNone, 0, 128), PartProperty("Source", PropertyType::String, "Script", ChangeScript, 0, 262144)}});
    registry.registerClass({"LocalScript", "Script", {}});
    registry.registerClass({"ScreenGui", "", {PartProperty("Enabled", PropertyType::Boolean, "Behavior"), PartProperty("IgnoreGuiInset", PropertyType::Boolean, "Behavior"), PartProperty("ResetOnSpawn", PropertyType::Boolean, "Behavior")}});
    ClassDescriptor frame{"Frame", ""};
    frame.properties = {
        PartProperty("Position", PropertyType::Vector2, "Layout", ChangeRender, -10000, 10000),
        PartProperty("PositionScale", PropertyType::Vector2, "Layout", ChangeRender, -10, 10),
        PartProperty("Size", PropertyType::Vector2, "Layout", ChangeRender, 1, 10000),
        PartProperty("SizeScale", PropertyType::Vector2, "Layout", ChangeRender, 0, 10),
        EnumProperty("HorizontalAlignment", "Layout", {"Left", "Center", "Right"}),
        EnumProperty("VerticalAlignment", "Layout", {"Top", "Center", "Bottom"}),
        PartProperty("ScaleWithViewport", PropertyType::Boolean, "Layout", ChangeRender),
        PartProperty("Visible", PropertyType::Boolean, "Appearance", ChangeRender),
        PartProperty("BackgroundColor", PropertyType::Color, "Appearance", ChangeRender, 0, 255),
        PartProperty("BackgroundTransparency", PropertyType::Float, "Appearance", ChangeRender, 0, 1)};
    registry.registerClass(std::move(frame));
    ClassDescriptor textLabel{"TextLabel", "Frame"};
    textLabel.properties = {
        PartProperty("Text", PropertyType::String, "Text", ChangeRender, 0, 4096),
        PartProperty("TextColor", PropertyType::Color, "Text", ChangeRender, 0, 255),
        PartProperty("TextTransparency", PropertyType::Float, "Text", ChangeRender, 0, 1),
        PartProperty("FontSize", PropertyType::Float, "Text", ChangeRender, 6, 144),
        PartProperty("TextScaled", PropertyType::Boolean, "Text", ChangeRender),
        PartProperty("TextWrapped", PropertyType::Boolean, "Text", ChangeRender),
        PartProperty("TextTruncate", PropertyType::Boolean, "Text", ChangeRender),
        EnumProperty("TextXAlignment", "Text", {"Left", "Center", "Right"}),
        EnumProperty("TextYAlignment", "Text", {"Top", "Center", "Bottom"})};
    registry.registerClass(std::move(textLabel));
    registry.registerClass({"TextButton", "TextLabel", {}});
    registry.registerClass({"Decal", "", {PartProperty("Texture", PropertyType::AssetReference, "Appearance", ChangeRender, 0, std::numeric_limits<double>::max()), PartProperty("Size", PropertyType::Vector2, "Appearance", ChangeRender, 0.001, 100000), PartProperty("UvOffset", PropertyType::Vector2, "Appearance", ChangeRender, -100000, 100000), PartProperty("UvScale", PropertyType::Vector2, "Appearance", ChangeRender, -1000, 1000), PartProperty("Rotation", PropertyType::Float, "Appearance", ChangeRender, -360000, 360000), PartProperty("Transparency", PropertyType::Float, "Appearance", ChangeRender, 0, 1), PartProperty("NormalBlend", PropertyType::Float, "Appearance", ChangeRender, 0, 1), PartProperty("Color", PropertyType::Color, "Appearance", ChangeRender, 0, 255), PartProperty("DepthTest", PropertyType::Boolean, "Appearance", ChangeRender)}});
    registry.registerClass({"Light", "", {PartProperty("Enabled", PropertyType::Boolean, "Lighting"), PartProperty("Color", PropertyType::Color, "Lighting", ChangeRender, 0, 255), PartProperty("Brightness", PropertyType::Float, "Lighting", ChangeRender, 0, 100000)}});
    registry.registerClass({"Camera", "", {PartProperty("Position", PropertyType::Vector3, "Transform", ChangeTransform, -1000000, 1000000), PartProperty("FieldOfView", PropertyType::Float, "Camera", ChangeRender, 1, 120)}});
}

} // namespace blubox::engine::reflection
