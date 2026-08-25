#pragma once

// ReSharper disable once CppUnusedIncludeDirective
#include <Windows.h>
#include <Xinput.h>

#include <expected>
#include <string>

namespace gc::input {

struct XInputApi {
    HMODULE module{};
    decltype(&XInputGetState) get_state{};
    std::wstring loaded_name;
};

[[nodiscard]] std::expected<XInputApi, std::string> LoadSystemXInput();
void UnloadXInput(XInputApi& api) noexcept;

} // namespace gc::input
