#include "Input/Win32/XInputApi.h"

#include <array>
#include <vector>

namespace gc::input {
namespace {

std::string Win32Failure(const char* operation, DWORD error)
{
    return std::string(operation) + " failed with Win32 error " +
        std::to_string(error);
}

std::expected<std::wstring, std::string> SystemDirectory()
{
    std::array<wchar_t, MAX_PATH> fixed{};
    UINT length = GetSystemDirectoryW(
        fixed.data(), static_cast<UINT>(fixed.size()));
    if (length == 0)
    {
        return std::unexpected(
            Win32Failure("GetSystemDirectoryW", GetLastError()));
    }
    if (length < fixed.size())
    {
        return std::wstring(fixed.data(), length);
    }

    std::vector<wchar_t> dynamic(static_cast<std::size_t>(length) + 1);
    length = GetSystemDirectoryW(
        dynamic.data(), static_cast<UINT>(dynamic.size()));
    if (length == 0 || length >= dynamic.size())
    {
        return std::unexpected(
            Win32Failure("GetSystemDirectoryW", GetLastError()));
    }
    return std::wstring(dynamic.data(), length);
}

} // namespace

std::expected<XInputApi, std::string> LoadSystemXInput()
{
    const auto directory = SystemDirectory();
    if (!directory)
    {
        return std::unexpected(directory.error());
    }

    constexpr std::array candidates{
        L"xinput1_4.dll",
        L"xinput9_1_0.dll",
    };
    DWORD last_error = ERROR_MOD_NOT_FOUND;
    for (const wchar_t* candidate : candidates)
    {
        const std::wstring path = *directory + L"\\" + candidate;
        HMODULE module = LoadLibraryW(path.c_str());
        if (module == nullptr)
        {
            last_error = GetLastError();
            continue;
        }

        auto get_state = reinterpret_cast<decltype(&XInputGetState)>(
            GetProcAddress(module, "XInputGetState"));
        if (get_state != nullptr)
        {
            return XInputApi{
                .module = module,
                .get_state = get_state,
                .loaded_name = path,
            };
        }

        last_error = GetLastError();
        FreeLibrary(module);
    }
    return std::unexpected(Win32Failure("Load system XInput", last_error));
}

void UnloadXInput(XInputApi& api) noexcept
{
    if (api.module != nullptr)
    {
        FreeLibrary(api.module);
    }
    api.module = nullptr;
    api.get_state = nullptr;
    api.loaded_name.clear();
}

} // namespace gc::input
