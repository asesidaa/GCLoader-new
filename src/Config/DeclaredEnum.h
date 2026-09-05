#pragma once

#include <rfl/enums.hpp>

namespace gc::config {

template <class Enum>
[[nodiscard]] constexpr auto DeclaredEnumValues() noexcept
{
    return rfl::get_enumerator_array<Enum>();
}

template <class Enum>
[[nodiscard]] constexpr bool IsDeclaredEnumValue(Enum value) noexcept
{
    for (const auto& [name, candidate] : DeclaredEnumValues<Enum>())
    {
        static_cast<void>(name);
        if (candidate == value)
        {
            return true;
        }
    }
    return false;
}

} // namespace gc::config
