#pragma once

#include "Input/Types/PhysicalKey.h"

#include <rfl.hpp>

#include <string>

// Keep the specialization grouped in the namespace of its primary template.
// ReSharper disable once CppRedundantNamespaceDefinition
namespace rfl {

template <>
struct Reflector<gc::input::PhysicalKey> {
    using ReflType = std::string;

    static gc::input::PhysicalKey to(const ReflType& token) noexcept
    {
        const auto parsed = gc::input::ParsePhysicalKey(token);
        return parsed ? parsed.value() : gc::input::PhysicalKey{};
    }

    static ReflType from(const gc::input::PhysicalKey& key)
    {
        return gc::input::FormatPhysicalKey(key);
    }
};

} // namespace rfl
