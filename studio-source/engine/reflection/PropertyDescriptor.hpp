#pragma once

#include "PropertyValue.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace blubox::engine::reflection {

enum PropertyChangeFlag : std::uint32_t {
    ChangeNone = 0, ChangeTransform = 1u << 0, ChangePhysics = 1u << 1,
    ChangeRender = 1u << 2, ChangeMaterial = 1u << 3, ChangeSpawn = 1u << 4,
    ChangeScript = 1u << 5, ChangeNetwork = 1u << 6
};

struct PropertyDescriptor {
    std::string name;
    std::string displayName;
    PropertyType type = PropertyType::None;
    std::string category = "Data";
    bool readOnly = false;
    bool hidden = false;
    bool serialized = true;
    bool replicated = false;
    double minimum = 0.0;
    double maximum = 1.0;
    std::vector<std::string> enumValues;
    std::uint32_t changeFlags = ChangeNone;
};

struct ClassDescriptor {
    std::string name;
    std::string baseClass;
    std::vector<PropertyDescriptor> properties;
};

class PropertyChangeSink {
public:
    virtual ~PropertyChangeSink() = default;
    virtual void onPropertyChanged(InstanceHandle handle, const PropertyDescriptor& property) = 0;
};

} // namespace blubox::engine::reflection
