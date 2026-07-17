#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gc::testmode_storage {

std::optional<std::string> RedirectPathA(
    std::string_view path,
    std::string_view current_directory);

std::optional<std::wstring> RedirectPathW(
    std::wstring_view path,
    std::wstring_view current_directory);

} // namespace gc::testmode_storage
