#pragma once

#include "PropertyDescriptor.hpp"

#include <string_view>
#include <unordered_map>
#include <vector>

namespace blubox::engine::reflection {

class ReflectionRegistry {
public:
    bool registerClass(ClassDescriptor descriptor);
    const ClassDescriptor* findClass(std::string_view className) const noexcept;
    std::vector<const PropertyDescriptor*> properties(std::string_view className) const;
    bool get(const InstanceRegistry& world, InstanceHandle handle, std::string_view name, PropertyValue& output) const;
    bool set(InstanceRegistry& world, InstanceHandle handle, std::string_view name,
             const PropertyValue& value, PropertyChangeSink* sink = nullptr) const;

private:
    const PropertyDescriptor* findProperty(std::string_view className, std::string_view propertyName) const;
    std::unordered_map<std::string, ClassDescriptor> classes_;
};

void RegisterBuiltinReflection(ReflectionRegistry& registry);

} // namespace blubox::engine::reflection
