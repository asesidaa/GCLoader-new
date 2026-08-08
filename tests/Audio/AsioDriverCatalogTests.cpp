// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriverCatalog.h"

#include <Windows.h>

#include <algorithm>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

CLSID ParseClsid(const wchar_t* text) {
    CLSID result{};
    if (FAILED(CLSIDFromString(text, &result))) {
        std::cerr << "Invalid test CLSID\n";
        std::exit(2);
    }
    return result;
}

class FakeRegistrySource final : public gc::audio::IAsioRegistrySource {
public:
    std::vector<gc::audio::AsioRegistryValue> values;
    bool fail{};

    std::expected<
        std::vector<gc::audio::AsioRegistryValue>,
        gc::audio::AsioFailure>
    Read32BitRegistrations() noexcept override {
        if (fail) {
            return std::unexpected(gc::audio::AsioFailure{
                .stage = gc::audio::AsioFailureStage::registry,
                .detail = "injected registry failure",
            });
        }
        return values;
    }
};

struct RegistryActionFake {
    int calls{};
    HKEY root{};
    std::wstring path;
    REGSAM access{};
};

std::expected<
    std::vector<gc::audio::AsioRegistryValue>,
    gc::audio::AsioFailure>
FakeRegistryRead(
    void* context,
    HKEY root,
    std::wstring_view path,
    REGSAM access) noexcept {
    auto& fake = *static_cast<RegistryActionFake*>(context);
    ++fake.calls;
    fake.root = root;
    fake.path.assign(path);
    fake.access = access;
    if ((access & KEY_WOW64_64KEY) != 0) {
        return std::vector<gc::audio::AsioRegistryValue>{
            {
                .subkey_name = L"64-bit-only driver",
                .clsid_text =
                    L"{99999999-9999-9999-9999-999999999999}",
            },
        };
    }
    return std::vector<gc::audio::AsioRegistryValue>{};
}

} // namespace

int main() {
    using gc::audio::AsioFailureStage;
    using gc::audio::AsioRegistryValue;

    int failures = 0;

    FakeRegistrySource source;
    source.values = {
        {
            .subkey_name = L"Zulu ASIO",
            .clsid_text = L"{44444444-4444-4444-4444-444444444444}",
        },
        {
            .subkey_name = L"驱动 ASIO",
            .clsid_text = L"{33333333-3333-3333-3333-333333333333}",
        },
        {
            .subkey_name = L"alpha ASIO",
            .clsid_text = L"{11111111-1111-1111-1111-111111111111}",
        },
        {
            .subkey_name = L"Beta ASIO",
            .clsid_text = L"{22222222-2222-2222-2222-222222222222}",
        },
    };
    const auto enumerated = gc::audio::EnumerateAsioDrivers(source);
    failures += Expect(
        enumerated && enumerated->size() == 4 &&
            (*enumerated)[0].registry_name == "alpha ASIO" &&
            (*enumerated)[1].registry_name == "Beta ASIO" &&
            (*enumerated)[2].registry_name == "Zulu ASIO" &&
            (*enumerated)[3].registry_name == "驱动 ASIO" &&
            IsEqualCLSID(
                (*enumerated)[3].clsid,
                ParseClsid(
                    L"{33333333-3333-3333-3333-333333333333}")),
        "enumeration preserves Unicode identity in stable ordinal order");

    FakeRegistrySource exact_source;
    exact_source.values = {
        {
            .subkey_name = L"FlexASIO",
            .clsid_text = L"{55555555-5555-5555-5555-555555555555}",
        },
    };
    const auto exact = gc::audio::ResolveAsioDriver(
        exact_source,
        "fLeXaSiO");
    failures += Expect(
        exact && exact->registry_name == "FlexASIO" &&
            IsEqualCLSID(
                exact->clsid,
                ParseClsid(
                    L"{55555555-5555-5555-5555-555555555555}")),
        "lookup uses Windows case-insensitive ordinal semantics");

    const std::string invalid_utf8{
        static_cast<char>(0xC3),
        static_cast<char>(0x28)};
    const auto malformed_lookup = gc::audio::ResolveAsioDriver(
        exact_source,
        invalid_utf8);
    failures += Expect(
        !malformed_lookup &&
            malformed_lookup.error().stage == AsioFailureStage::registry,
        "malformed UTF-8 lookup is rejected");

    FakeRegistrySource malformed_name;
    malformed_name.values = {
        {
            .subkey_name = std::wstring(
                1,
                static_cast<wchar_t>(0xD800)),
            .clsid_text = L"{66666666-6666-6666-6666-666666666666}",
        },
    };
    const auto malformed_name_result =
        gc::audio::EnumerateAsioDrivers(malformed_name);
    failures += Expect(
        !malformed_name_result &&
            malformed_name_result.error().stage ==
                AsioFailureStage::registry,
        "malformed UTF-16 registry identity is rejected");

    FakeRegistrySource malformed_clsid;
    malformed_clsid.values = {
        {
            .subkey_name = L"Broken CLSID",
            .clsid_text = L"not-a-clsid",
        },
    };
    const auto malformed_clsid_result =
        gc::audio::EnumerateAsioDrivers(malformed_clsid);
    failures += Expect(
        !malformed_clsid_result &&
            malformed_clsid_result.error().stage ==
                AsioFailureStage::clsid,
        "malformed registration CLSID is rejected");

    FakeRegistrySource duplicate;
    duplicate.values = {
        {
            .subkey_name = L"ASIO4ALL v2",
            .clsid_text = L"{77777777-7777-7777-7777-777777777777}",
        },
        {
            .subkey_name = L"asio4all V2",
            .clsid_text = L"{88888888-8888-8888-8888-888888888888}",
        },
    };
    const auto duplicate_result =
        gc::audio::EnumerateAsioDrivers(duplicate);
    failures += Expect(
        !duplicate_result &&
            duplicate_result.error().stage == AsioFailureStage::registry,
        "duplicate case-folded registry names are rejected");

    const auto missing = gc::audio::ResolveAsioDriver(
        exact_source,
        "Arbitrary Missing Driver");
    failures += Expect(
        !missing && missing.error().stage == AsioFailureStage::registry,
        "arbitrary missing registry name is rejected");

    FakeRegistrySource failed_source;
    failed_source.fail = true;
    const auto source_failure =
        gc::audio::EnumerateAsioDrivers(failed_source);
    failures += Expect(
        !source_failure &&
            source_failure.error().detail == "injected registry failure",
        "source errors remain typed and intact");

    RegistryActionFake action_fake;
    gc::audio::ProductionAsioRegistrySource production_source{
        {
            .context = &action_fake,
            .read = &FakeRegistryRead,
        }};
    const auto production_values =
        production_source.Read32BitRegistrations();
    failures += Expect(
        production_values && production_values->empty() &&
            action_fake.calls == 1 &&
            action_fake.root == HKEY_LOCAL_MACHINE &&
            action_fake.path == L"SOFTWARE\\ASIO" &&
            (action_fake.access & KEY_READ) == KEY_READ &&
            (action_fake.access & KEY_WOW64_32KEY) != 0 &&
            (action_fake.access & KEY_WOW64_64KEY) == 0,
        "production source opens only HKLM ASIO in the 32-bit view");

    return failures == 0 ? 0 : 1;
}
