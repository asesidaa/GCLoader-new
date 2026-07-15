# Registry Configuration Virtualization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the approved game and NESYS service registry values into strict TOML-backed, process-local overrides while preserving the physical Type X registry key and every unowned registry operation.

**Architecture:** Add a shared `RegistryConfig` schema/validation unit and a focused `RegistryConfigOverride` runtime component. The override tracks only successful opens of `HKLM\SOFTWARE\taito\typex`, formats owned `REG_DWORD`/`REG_SZ` responses with native `RegQueryValueExA` semantics, and contributes exactly three Advapi32 hooks to the existing owned MinHook transaction. Extend `NesysFeaturePlan` so network virtualization and registry virtualization compose independently while sharing service-child injection only when either policy requires it.

**Tech Stack:** C++23, Win32 ANSI registry APIs, reflect-cpp 0.19.0 TOML, ImGui/SDL3, MinHook, CMake 3.31+, Ninja, CTest, x86 MSVC (`vcvars32.bat`).

## Global Constraints

- Implement the approved design in `docs/superpowers/specs/2026-07-12-registry-config-virtualization-design.md`; the sample SHA-256 values are provenance only and must never become compatibility gates.
- Keep `[registry]`, `[registry.game]`, and `[registry.nesys]` plus every field required by reflect-cpp even when `registry.enabled = false`.
- Newly constructed ConfigGUI state and the distributed `config.toml` must set `registry.enabled = false`.
- Map `GrooveCoasterJpn`, `Rhythmvaders`, and `GrooveCoasterEng` exactly to registry DWORD values `0`, `1`, and `2`; accept neither numeric TOML country values nor unknown enum names.
- Keep `[nesys].server_ip` independent from registry configuration.
- Keep `TrafficCount` out of TOML and pass it through to the physical registry; the real Type X key remains required.
- Do not hook or synthesize `SystemBiosDate`, NIC `NetworkAddress`, `country.dat`, registry enumeration, registry writes, registry creation, or registry deletion.
- When enabled, request exactly `ADVAPI32!RegOpenKeyExA`, `ADVAPI32!RegQueryValueExA`, and `ADVAPI32!RegCloseKey`; do not add wide-character hooks.
- Always call the original `RegOpenKeyExA` first with the caller's unchanged root, subkey, options, and access mask; never fabricate the Type X key.
- Treat registry names case-insensitively and track only the exact `SOFTWARE\taito\typex` subkey opened directly below `HKEY_LOCAL_MACHINE`.
- Return DWORD values as exactly four bytes. Return string sizes including the terminating NUL, cap configured path bytes at 259 before that NUL, and return `ERROR_MORE_DATA` without overrunning short buffers.
- Preserve legal zero values for `event_next_time` and `condition_time`; the loader must not replace them with the service's 1800-second interpretation.
- `experimental.enable_nesys_service_adapter_patch` and `registry.enabled` are independent. Registry-only mode must not initialize adapter, resolver, mutation-suppression, or fixed-RVA ping state.
- Compose every enabled exported hook in a process into one `OwnedMinHookTransaction`; retain the existing guarded internal ping-hook lifecycle only for network-enabled service mode.
- Initialization failures are fail-closed and transactional. Existing suspended-child termination with `ERROR_DLL_INIT_FAILED` remains the service-injection failure path.
- Bound diagnostics to process/toggle state, component installation, the first tracked Type X open, and the first override per owned value; never log repeated configured path contents or physical registry contents.
- Agent-owned verification ends at build, unit/integration tests, and static inspection. Only the user may declare runtime acceptance after completing the manual checklist.
- Preserve unrelated worktree changes. Every commit below uses an exact pathspec and must not stage files outside that task.

## Native API References

- [`RegOpenKeyExA`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexa): key names are case-insensitive, the caller supplies the access mask, and the API does not create missing keys.
- [`RegQueryValueExA`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexa): `lpReserved` must be null, `lpcbData` may be null only when `lpData` is null, size probes succeed, and short buffers return `ERROR_MORE_DATA` with the required size.
- [`RegCloseKey`](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey): a handle is no longer valid only after a successful close.

## File Structure

### New files

- `RegistryConfig.h` — strict reflect-cpp schema, `GameCountry`, shared numeric/path validation declarations, and validation result contract.
- `RegistryConfig.cpp` — semantic validators and deterministic validation messages used by both runtime and ConfigGUI.
- `RegistryConfigOverride.h` — immutable override values, testable tracked-handle dispatcher, original registry API signatures, and hook-registration interfaces.
- `RegistryConfigOverride.cpp` — owned-value lookup, native query-buffer formatting, tracked-handle lifecycle, bounded diagnostics, detours, and the three Advapi32 hook requests.
- `tests/RegistryConfigOverrideTests.cpp` — focused fake-registry tests for ownership, buffers, handles, pass-through behavior, and forbidden-hook absence.

### Modified files

- `config.h` — embed the required registry tree in `InputConfig` and expose read-only runtime accessors.
- `config.cpp` — reject semantically invalid registry values before any hook installation, including when the registry switch is disabled.
- `config.toml` — add the complete default-off registry tree with the approved defaults.
- `GUI_main.cpp` — add the Registry section, all nine editable fields, country descriptions, inline errors, and save gating.
- `CMakeLists.txt` — compile the new shared/runtime units and register `RegistryConfigOverrideTests`.
- `tests/ConfigFeatureTests.cpp` — cover required tables/fields, defaults, enum wire mapping, invalid values, and round-trip behavior.
- `NesysServiceProcess.h` / `NesysServiceProcess.cpp` — make the feature plan accept independent network and registry toggles.
- `NesysServicePatch.cpp` — conditionally prepare and append each component while retaining one transaction and the existing launcher handshake.
- `tests/NesysServicePatchTests.cpp` — verify both roles across all four toggle combinations.
- `tests/NesysHookTransactionTests.cpp` — prove a combined-component failure removes every owned exported hook.

No `dllmain.cpp` change is required: `NesysServicePatchInit()` already runs immediately after role detection and before game-only initialization, and its `FALSE` result already fails DLL attach.

---

### Task 1: Strict Registry Schema, Shared Validation, Defaults, and ConfigGUI

**Files:**
- Create: `RegistryConfig.h`
- Create: `RegistryConfig.cpp`
- Modify: `config.h:1-13,85-99,175-188`
- Modify: `config.cpp:1-42`
- Modify: `config.toml:42-49`
- Modify: `GUI_main.cpp:1-20,426-460,577-605`
- Modify: `CMakeLists.txt:105-129,173-187`
- Test: `tests/ConfigFeatureTests.cpp:17-204,206-421`

**Interfaces:**
- Consumes: reflect-cpp `rfl::Rename`, `rfl::toml::read<InputConfig>()`, `rfl::toml::write(InputConfig)`, and the current direct-edit ConfigGUI model.
- Produces: `GameCountry`, `RegistryGameConfig`, `RegistryNesysConfig`, `RegistryConfig`, `gc::registry_config::GameCountryRegistryDword(GameCountry) -> std::uint32_t`, `IsRegistryDword(std::int64_t) -> bool`, `IsRegistryLogLevel(std::int64_t) -> bool`, `IsRegistryPath(std::string_view) -> bool`, `ValidateRegistryConfig(const RegistryConfig&) -> RegistryValidationResult`, `FirstRegistryValidationError(const RegistryValidationResult&) -> const char*`, `ConfigManager::GetEnableRegistryConfigOverride() -> bool`, and `ConfigManager::GetRegistryConfig() -> const RegistryConfig&`.

- [ ] **Step 1: Update valid TOML fixtures and add registry assertion helpers**

In `tests/ConfigFeatureTests.cpp`, add this complete registry block to every fixture intended to be valid, between `[nesys]` and `[experimental]`:

```toml
[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'
```

Add these helpers inside the anonymous namespace after `expect_style`:

```cpp
int expect_country(
    GameCountry actual,
    GameCountry expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " country value "
              << static_cast<std::uint32_t>(expected) << ", got "
              << static_cast<std::uint32_t>(actual) << "\n";
    return 1;
}

int expect_u32(
    std::uint32_t actual,
    std::uint32_t expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_i64(
    std::int64_t actual,
    std::int64_t expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_registry_valid(
    const RegistryConfig& config,
    bool expected,
    const char* name) {
    const bool actual =
        gc::registry_config::ValidateRegistryConfig(config).valid();
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " registry validity to be "
              << expected << ", got " << actual << "\n";
    return 1;
}
```

- [ ] **Step 2: Add default-value and generated-TOML assertions**

After the constructed NESYS default checks, add the exact default and serialization assertions:

```cpp
failures += expect_bool(
    upgraded_defaults.registry().enabled(),
    false,
    "default registry enabled");
failures += expect_country(
    upgraded_defaults.registry().game().country(),
    GameCountry::GrooveCoasterJpn,
    "default game country");
failures += expect_i64(
    upgraded_defaults.registry().nesys().game_kind(),
    303801,
    "default registry game_kind");
failures += expect_i64(
    upgraded_defaults.registry().nesys().event_next_time(),
    900,
    "default registry event_next_time");
failures += expect_i64(
    upgraded_defaults.registry().nesys().condition_time(),
    300,
    "default registry condition_time");
failures += expect_i64(
    upgraded_defaults.registry().nesys().log_level(),
    3,
    "default registry log_level");
failures += expect_string(
    upgraded_defaults.registry().nesys().news_path(),
    "D:\\system\\DUA\\news",
    "default registry news_path");
failures += expect_string(
    upgraded_defaults.registry().nesys().event_path(),
    "D:\\system\\DUA\\event",
    "default registry event_path");
failures += expect_string(
    upgraded_defaults.registry().nesys().log_path(),
    "D:\\system\\CmdFile\\log",
    "default registry log_path");
failures += expect_registry_valid(
    upgraded_defaults.registry(),
    true,
    "default registry config");

failures += expect_bool(
    generated_toml.find("[registry]") != std::string::npos,
    true,
    "generated TOML registry table");
failures += expect_bool(
    generated_toml.find("[registry.game]") != std::string::npos,
    true,
    "generated TOML registry.game table");
failures += expect_bool(
    generated_toml.find("[registry.nesys]") != std::string::npos,
    true,
    "generated TOML registry.nesys table");
failures += expect_bool(
    generated_toml.find("enabled = false") != std::string::npos,
    true,
    "generated TOML registry disabled default");
```

Define the exact valid registry text once near the fixture constants so missing-table and missing-field tests do not depend on unrelated sections:

```cpp
constexpr std::string_view kDefaultRegistryConfig = R"toml(
[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'

)toml";
```

- [ ] **Step 3: Add missing-table, missing-field, and invalid-country parsing tests**

Use the same block in the complete default fixture, then add these strict parsing checks after the existing required-NESYS checks:

```cpp
const auto valid_registry_config =
    std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;

failures += expect_parse_failure(
    replace_once(
        valid_registry_config,
        kDefaultRegistryConfig,
        ""),
    "missing registry table tree");
failures += expect_parse_failure(
    replace_once(
        valid_registry_config,
        "[registry.game]\ncountry = 'GrooveCoasterJpn'\n\n",
        ""),
    "missing registry.game table");
failures += expect_parse_failure(
    replace_once(
        valid_registry_config,
        "[registry.nesys]\ngame_kind = 303801\nevent_next_time = 900\n"
        "condition_time = 300\nlog_level = 3\n"
        "news_path = 'D:\\system\\DUA\\news'\n"
        "event_path = 'D:\\system\\DUA\\event'\n"
        "log_path = 'D:\\system\\CmdFile\\log'\n",
        ""),
    "missing registry.nesys table");

constexpr std::array<std::string_view, 9> required_registry_fields{
    "enabled = false\n",
    "country = 'GrooveCoasterJpn'\n",
    "game_kind = 303801\n",
    "event_next_time = 900\n",
    "condition_time = 300\n",
    "log_level = 3\n",
    "news_path = 'D:\\system\\DUA\\news'\n",
    "event_path = 'D:\\system\\DUA\\event'\n",
    "log_path = 'D:\\system\\CmdFile\\log'\n",
};
for (const auto field : required_registry_fields) {
    failures += expect_parse_failure(
        replace_once(valid_registry_config, field, ""),
        std::string("missing registry field ").append(field).c_str());
}

failures += expect_parse_failure(
    replace_once(
        valid_registry_config,
        "country = 'GrooveCoasterJpn'",
        "country = 'UnknownCountry'"),
    "unknown registry country");
failures += expect_parse_failure(
    replace_once(
        valid_registry_config,
        "country = 'GrooveCoasterJpn'",
        "country = 1"),
    "numeric registry country");
```

- [ ] **Step 4: Add table-driven country round-trip and DWORD mapping checks**

Add the following cases:

```cpp
struct CountryCase {
    std::string_view name;
    GameCountry country;
    std::uint32_t dword;
};
constexpr std::array<CountryCase, 3> country_cases{{
    {"GrooveCoasterJpn", GameCountry::GrooveCoasterJpn, 0},
    {"Rhythmvaders", GameCountry::Rhythmvaders, 1},
    {"GrooveCoasterEng", GameCountry::GrooveCoasterEng, 2},
}};
for (const auto& test : country_cases) {
    const auto country_text = replace_once(
        valid_registry_config,
        "country = 'GrooveCoasterJpn'",
        std::string("country = '").append(test.name).append("'"));
    const auto parsed = parse_config(country_text);
    failures += expect_country(
        parsed.registry().game().country(),
        test.country,
        std::string(test.name).append(" parsed country").c_str());
    failures += expect_u32(
        gc::registry_config::GameCountryRegistryDword(test.country),
        test.dword,
        std::string(test.name).append(" DWORD").c_str());
    const auto round_trip = parse_config(rfl::toml::write(parsed));
    failures += expect_country(
        round_trip.registry().game().country(),
        test.country,
        std::string(test.name).append(" round-trip").c_str());
}
```

- [ ] **Step 5: Add DWORD, log-level, zero-timer, and path-limit validation tests**

These deliberately use a signed 64-bit configuration model so ConfigGUI can retain and display negative or too-large input instead of silently wrapping it:

```cpp
auto zero_timers = upgraded_defaults.registry();
zero_timers.nesys().event_next_time = 0;
zero_timers.nesys().condition_time = 0;
failures += expect_registry_valid(zero_timers, true, "zero timing values");

auto negative_game_kind = upgraded_defaults.registry();
negative_game_kind.nesys().game_kind = -1;
failures += expect_registry_valid(
    negative_game_kind,
    false,
    "negative game_kind");

auto oversized_condition = upgraded_defaults.registry();
oversized_condition.nesys().condition_time = 4'294'967'296LL;
failures += expect_registry_valid(
    oversized_condition,
    false,
    "condition_time above DWORD range");

auto invalid_log_level = upgraded_defaults.registry();
invalid_log_level.nesys().log_level = 4;
failures += expect_registry_valid(
    invalid_log_level,
    false,
    "log_level above 3");

auto empty_news_path = upgraded_defaults.registry();
empty_news_path.nesys().news_path = "";
failures += expect_registry_valid(
    empty_news_path,
    false,
    "empty news_path");

auto maximum_log_path = upgraded_defaults.registry();
maximum_log_path.nesys().log_path = std::string(259, 'x');
failures += expect_registry_valid(
    maximum_log_path,
    true,
    "259-byte log_path");

auto oversized_log_path = upgraded_defaults.registry();
oversized_log_path.nesys().log_path = std::string(260, 'x');
failures += expect_registry_valid(
    oversized_log_path,
    false,
    "260-byte log_path");
```

- [ ] **Step 6: Run the configuration test to verify it fails**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests'
```

Expected: compilation fails because `GameCountry`, `RegistryConfig`, `InputConfig::registry`, and the `gc::registry_config` validation functions do not exist yet.

- [ ] **Step 7: Define the strict registry schema and validation contract**

Create `RegistryConfig.h` with this complete content:

```cpp
#pragma once

#include <rfl.hpp>

#include <cstdint>
#include <string>
#include <string_view>

enum class GameCountry : std::uint32_t {
    GrooveCoasterJpn = 0,
    Rhythmvaders = 1,
    GrooveCoasterEng = 2,
};

struct RegistryGameConfig {
    rfl::Rename<"country", GameCountry> country =
        GameCountry::GrooveCoasterJpn;
};

struct RegistryNesysConfig {
    rfl::Rename<"game_kind", std::int64_t> game_kind = 303801;
    rfl::Rename<"event_next_time", std::int64_t> event_next_time = 900;
    rfl::Rename<"condition_time", std::int64_t> condition_time = 300;
    rfl::Rename<"log_level", std::int64_t> log_level = 3;
    rfl::Rename<"news_path", std::string> news_path =
        "D:\\system\\DUA\\news";
    rfl::Rename<"event_path", std::string> event_path =
        "D:\\system\\DUA\\event";
    rfl::Rename<"log_path", std::string> log_path =
        "D:\\system\\CmdFile\\log";
};

struct RegistryConfig {
    rfl::Rename<"enabled", bool> enabled = false;
    rfl::Rename<"game", RegistryGameConfig> game;
    rfl::Rename<"nesys", RegistryNesysConfig> nesys;
};

namespace gc::registry_config {

struct RegistryValidationResult {
    bool game_kind{false};
    bool event_next_time{false};
    bool condition_time{false};
    bool log_level{false};
    bool news_path{false};
    bool event_path{false};
    bool log_path{false};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return game_kind && event_next_time && condition_time &&
            log_level && news_path && event_path && log_path;
    }
};

constexpr std::uint32_t GameCountryRegistryDword(
    GameCountry country) noexcept {
    return static_cast<std::uint32_t>(country);
}

bool IsRegistryDword(std::int64_t value) noexcept;
bool IsRegistryLogLevel(std::int64_t value) noexcept;
bool IsRegistryPath(std::string_view value) noexcept;
RegistryValidationResult ValidateRegistryConfig(
    const RegistryConfig& config) noexcept;
const char* FirstRegistryValidationError(
    const RegistryValidationResult& validation) noexcept;

} // namespace gc::registry_config
```

- [ ] **Step 8: Implement the shared semantic validators and deterministic errors**

Create `RegistryConfig.cpp` with this complete content:

```cpp
#include "RegistryConfig.h"

#include <limits>

namespace gc::registry_config {

bool IsRegistryDword(std::int64_t value) noexcept {
    return value >= 0 &&
        static_cast<std::uint64_t>(value) <=
            std::numeric_limits<std::uint32_t>::max();
}

bool IsRegistryLogLevel(std::int64_t value) noexcept {
    return value >= 0 && value <= 3;
}

bool IsRegistryPath(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 259;
}

RegistryValidationResult ValidateRegistryConfig(
    const RegistryConfig& config) noexcept {
    const auto& nesys = config.nesys();
    return {
        IsRegistryDword(nesys.game_kind()),
        IsRegistryDword(nesys.event_next_time()),
        IsRegistryDword(nesys.condition_time()),
        IsRegistryLogLevel(nesys.log_level()),
        IsRegistryPath(nesys.news_path()),
        IsRegistryPath(nesys.event_path()),
        IsRegistryPath(nesys.log_path()),
    };
}

const char* FirstRegistryValidationError(
    const RegistryValidationResult& validation) noexcept {
    if (!validation.game_kind) {
        return "[registry.nesys].game_kind must be an unsigned 32-bit integer";
    }
    if (!validation.event_next_time) {
        return "[registry.nesys].event_next_time must be an unsigned 32-bit integer";
    }
    if (!validation.condition_time) {
        return "[registry.nesys].condition_time must be an unsigned 32-bit integer";
    }
    if (!validation.log_level) {
        return "[registry.nesys].log_level must be an integer from 0 through 3";
    }
    if (!validation.news_path) {
        return "[registry.nesys].news_path must contain 1 through 259 bytes";
    }
    if (!validation.event_path) {
        return "[registry.nesys].event_path must contain 1 through 259 bytes";
    }
    if (!validation.log_path) {
        return "[registry.nesys].log_path must contain 1 through 259 bytes";
    }
    return nullptr;
}

} // namespace gc::registry_config
```

- [ ] **Step 9: Wire the required registry tree into runtime configuration loading**

In `config.h`, include the new schema after `SdlRflParsers.h`:

```cpp
#include "SdlRflParsers.h"
#include "RegistryConfig.h"
```

Add the registry tree between `nesys` and `experimental` in `InputConfig`:

```cpp
rfl::Rename<"nesys", NesysConfig> nesys;
rfl::Rename<"registry", RegistryConfig> registry;
rfl::Rename<"experimental", ExperimentalConfig> experimental;
```

Add these accessors after `GetNesysServerIp()`:

```cpp
bool GetEnableRegistryConfigOverride() const {
    return config.registry.value().enabled.value();
}

const RegistryConfig& GetRegistryConfig() const {
    return config.registry.value();
}
```

In `config.cpp`, validate the complete required tree immediately after the existing dotted-decimal IPv4 check and before assigning `config`:

```cpp
const auto registry_validation =
    gc::registry_config::ValidateRegistryConfig(result.value().registry());
if (!registry_validation.valid()) {
    throw std::runtime_error(
        gc::registry_config::FirstRegistryValidationError(
            registry_validation));
}

config = result.value();
```

This validation must run regardless of `result.value().registry().enabled()`.

- [ ] **Step 10: Add the complete disabled-by-default distributed TOML tree**

In `config.toml`, insert this exact tree between `[nesys]` and `[experimental]`:

```toml
[registry]
enabled = false

[registry.game]
country = 'GrooveCoasterJpn'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
news_path = 'D:\system\DUA\news'
event_path = 'D:\system\DUA\event'
log_path = 'D:\system\CmdFile\log'
```

- [ ] **Step 11: Add ConfigGUI controls, descriptions, inline errors, and save gating**

In `GUI_main.cpp`, include the shared validation interface:

```cpp
#include "NesysNetworkConfig.h"
#include "RegistryConfig.h"
```

Add this helper after the existing binding-row helpers and before `main()`:

```cpp
void DrawInlineValidationError(bool valid, const char* message) {
    if (!valid) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "%s",
            message);
    }
}
```

Insert this complete section after the NESYS server IPv4 validation and before the Experimental section:

```cpp
ImGui::SeparatorText("Registry");
auto& registry = g_config.registry();

bool registry_enabled = registry.enabled();
if (ImGui::Checkbox(
        "Registry configuration overrides",
        &registry_enabled)) {
    registry.enabled = registry_enabled;
    g_config_dirty = true;
}

constexpr const char* country_items[] = {
    "GrooveCoasterJpn - GROOVE COASTER, Japanese branding",
    "Rhythmvaders - RHYTHMVADERS, English branding",
    "GrooveCoasterEng - GROOVE COASTER, English branding",
};
int country_index = static_cast<int>(registry.game().country());
if (ImGui::Combo(
        "Game country",
        &country_index,
        country_items,
        IM_ARRAYSIZE(country_items))) {
    registry.game().country = static_cast<GameCountry>(country_index);
    g_config_dirty = true;
}

auto& registry_nesys = registry.nesys();
auto& game_kind = registry_nesys.game_kind();
if (ImGui::InputScalar(
        "Registry GameKind",
        ImGuiDataType_S64,
        &game_kind)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryDword(game_kind),
    "Enter an integer from 0 through 4294967295.");

auto& event_next_time = registry_nesys.event_next_time();
if (ImGui::InputScalar(
        "Registry EventNextTime",
        ImGuiDataType_S64,
        &event_next_time)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryDword(event_next_time),
    "Enter an integer from 0 through 4294967295.");

auto& condition_time = registry_nesys.condition_time();
if (ImGui::InputScalar(
        "Registry ConditionTime",
        ImGuiDataType_S64,
        &condition_time)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryDword(condition_time),
    "Enter an integer from 0 through 4294967295.");

auto& log_level = registry_nesys.log_level();
if (ImGui::InputScalar(
        "Registry LogLevel",
        ImGuiDataType_S64,
        &log_level)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryLogLevel(log_level),
    "Enter an integer from 0 through 3.");

auto& news_path = registry_nesys.news_path();
if (ImGui::InputText("Registry NewsPath", &news_path)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryPath(news_path),
    "Path must contain 1-259 encoded bytes before the terminating NUL.");

auto& event_path = registry_nesys.event_path();
if (ImGui::InputText("Registry EventPath", &event_path)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryPath(event_path),
    "Path must contain 1-259 encoded bytes before the terminating NUL.");

auto& log_path = registry_nesys.log_path();
if (ImGui::InputText("Registry LogPath", &log_path)) {
    g_config_dirty = true;
}
DrawInlineValidationError(
    gc::registry_config::IsRegistryPath(log_path),
    "Path must contain 1-259 encoded bytes before the terminating NUL.");

const auto registry_validation =
    gc::registry_config::ValidateRegistryConfig(registry);
```

Replace the Save Configuration gate with the combined shared validation gate:

```cpp
const bool configuration_valid =
    nesys_server_ip_valid && registry_validation.valid();
ImGui::BeginDisabled(!configuration_valid);
if (ImGui::Button("Save Configuration") && g_config_dirty) {
    try {
        std::string toml_output = rfl::toml::write(g_config);
        std::ofstream ofs(g_config_path);
        if (ofs.is_open()) {
            ofs << toml_output;
            ofs.close();
            SDL_Log(
                "Configuration saved successfully to %s",
                g_config_path.c_str());
            g_config_dirty = false;
            g_saved = true;
        } else {
            SDL_Log(
                "Error: Could not open %s for writing.",
                g_config_path.c_str());
        }
    } catch (const std::exception& error) {
        SDL_Log(
            "Error serializing configuration to TOML: %s",
            error.what());
    }
}
ImGui::EndDisabled();
```

- [ ] **Step 12: Compile the shared validation unit into runtime, GUI, and tests**

In `CMakeLists.txt`, add `RegistryConfig.cpp` to `SOURCES`, `GUI_SOURCES`, and `ConfigFeatureTests`:

```cmake
set(SOURCES
        CountdownTimerFreeze.cpp
        config.cpp
        dllmain.cpp
        FrameratePatch.cpp
        iDmacDrv32.cpp
        InputManager.cpp
        NesysHookTransaction.cpp
        NesysNetworkConfig.cpp
        NesysServiceLauncher.cpp
        NesysServicePatch.cpp
        NesysServiceProcess.cpp
        RegistryConfig.cpp
        RfidEmu.cpp
        ServerAddressOverride.cpp
        SwitchInputPolicy.cpp
        SwitchInputPatch.cpp
        SyntheticNetworkAdapter.cpp
        TestModeStorageRedirect.cpp
)

set(GUI_SOURCES
        config.cpp
        GUI_main.cpp
        NesysNetworkConfig.cpp
        RegistryConfig.cpp
)

add_executable(ConfigFeatureTests
        NesysNetworkConfig.cpp
        RegistryConfig.cpp
        tests/ConfigFeatureTests.cpp
)
```

- [ ] **Step 13: Build and run the configuration and GUI checks**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests ConfigGUI && ctest --test-dir build-msvc32-latest --output-on-failure -R ConfigFeatureTests'
```

Expected: both targets build, and CTest reports `ConfigFeatureTests` passed. The test covers all three enum names, DWORD mappings 0/1/2, default-off serialization, strict missing tables/fields, numeric/unknown country rejection, legal zero timers, DWORD range checks, log-level bounds, and path byte limits.

- [ ] **Step 14: Commit the strict configuration slice**

```powershell
git add -- RegistryConfig.h RegistryConfig.cpp config.h config.cpp config.toml GUI_main.cpp CMakeLists.txt tests/ConfigFeatureTests.cpp
git commit -m "feat: add strict registry configuration"
```

Expected: the commit contains only the schema, shared validation, distributed defaults, ConfigGUI, build wiring, and configuration tests.

---

### Task 2: Native Registry Overlay and Tracked-Handle Lifecycle

**Files:**
- Create: `RegistryConfigOverride.h`
- Create: `RegistryConfigOverride.cpp`
- Create: `tests/RegistryConfigOverrideTests.cpp`
- Modify: `CMakeLists.txt:105-123,189-200`

**Interfaces:**
- Consumes: `RegistryConfig`, `gc::registry_config::ValidateRegistryConfig()`, `GameCountryRegistryDword()`, `gc::nesys_service::ProcessRole`, `ApiHookRequest`, and the original Win32 registry trampolines populated by MinHook.
- Produces: `RegistryOverrideValues`, `CreateRegistryOverrideValues(const RegistryConfig&) -> std::optional<RegistryOverrideValues>`, `RegistryConfigOverride::Open/Query/Close`, `InitializeRegistryConfigOverride(ProcessRole, const RegistryConfig&) -> bool`, `RegistryOverrideHookExports() -> std::span<const char* const>`, and `AppendRegistryOverrideHookRequests(std::vector<ApiHookRequest>&)`.

- [ ] **Step 1: Register the focused registry overlay test target**

Add this target immediately after `ConfigFeatureTests` in `CMakeLists.txt`:

```cmake
add_executable(RegistryConfigOverrideTests
        NesysServiceProcess.cpp
        RegistryConfig.cpp
        RegistryConfigOverride.cpp
        tests/RegistryConfigOverrideTests.cpp
)
target_include_directories(RegistryConfigOverrideTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${reflectcpp_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
add_test(
        NAME RegistryConfigOverrideTests
        COMMAND RegistryConfigOverrideTests
)
```

- [ ] **Step 2: Create the fake original registry APIs and reusable buffer assertions**

Create `tests/RegistryConfigOverrideTests.cpp` with fake original APIs and assertions that exercise the public dispatcher rather than the machine registry. Use this complete fixture and helper layer:

```cpp
#include "RegistryConfigOverride.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gc::nesys_service;

struct FakeRegistryState {
    LSTATUS open_status{ERROR_SUCCESS};
    HKEY next_handle{reinterpret_cast<HKEY>(0x1000)};
    int open_calls{0};
    HKEY last_root{nullptr};
    std::string last_subkey;
    DWORD last_options{0};
    REGSAM last_access{0};

    LSTATUS query_status{ERROR_FILE_NOT_FOUND};
    int query_calls{0};
    HKEY last_query_handle{nullptr};
    std::string last_value_name;
    LPDWORD last_reserved{nullptr};
    LPDWORD last_type{nullptr};
    LPBYTE last_data{nullptr};
    LPDWORD last_data_size{nullptr};

    LSTATUS close_status{ERROR_SUCCESS};
    int close_calls{0};
    HKEY last_close_handle{nullptr};
};

FakeRegistryState* g_fake = nullptr;

LSTATUS WINAPI fake_open(
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    ++g_fake->open_calls;
    g_fake->last_root = root;
    g_fake->last_subkey = subkey != nullptr ? subkey : "<null>";
    g_fake->last_options = options;
    g_fake->last_access = access;
    if (g_fake->open_status == ERROR_SUCCESS && result != nullptr) {
        *result = g_fake->next_handle;
    }
    return g_fake->open_status;
}

LSTATUS WINAPI fake_query(
    HKEY key,
    LPCSTR value_name,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) {
    ++g_fake->query_calls;
    g_fake->last_query_handle = key;
    g_fake->last_value_name =
        value_name != nullptr ? value_name : "<null>";
    g_fake->last_reserved = reserved;
    g_fake->last_type = type;
    g_fake->last_data = data;
    g_fake->last_data_size = data_size;
    return g_fake->query_status;
}

LSTATUS WINAPI fake_close(HKEY key) {
    ++g_fake->close_calls;
    g_fake->last_close_handle = key;
    return g_fake->close_status;
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

int expect_status(LSTATUS actual, LSTATUS expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " status " << expected
              << ", got " << actual << "\n";
    return 1;
}

HKEY track(
    RegistryConfigOverride& overlay,
    FakeRegistryState& state,
    HKEY root,
    const char* subkey,
    DWORD options,
    REGSAM access,
    HKEY handle,
    int* failures) {
    state.open_status = ERROR_SUCCESS;
    state.next_handle = handle;
    g_fake = &state;
    HKEY opened = nullptr;
    *failures += expect_status(
        overlay.Open(
            fake_open,
            root,
            subkey,
            options,
            access,
            &opened),
        ERROR_SUCCESS,
        "tracked open");
    *failures += expect(opened == handle, "original open handle returned");
    return opened;
}

int expect_dword_override(
    RegistryConfigOverride& overlay,
    FakeRegistryState& state,
    HKEY handle,
    const char* name,
    DWORD expected) {
    int failures = 0;
    const int original_calls = state.query_calls;

    DWORD type = 0;
    DWORD size = 0;
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            nullptr,
            &size),
        ERROR_SUCCESS,
        "DWORD size probe");
    failures += expect(type == REG_DWORD, "DWORD probe type");
    failures += expect(size == sizeof(DWORD), "DWORD probe size");

    DWORD value = 0xCCCCCCCC;
    size = sizeof(value);
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&value),
            &size),
        ERROR_SUCCESS,
        "DWORD exact buffer");
    failures += expect(type == REG_DWORD, "DWORD exact type");
    failures += expect(size == sizeof(DWORD), "DWORD exact size");
    failures += expect(value == expected, "DWORD exact value");

    std::array<BYTE, 8> oversized{};
    oversized.fill(0xA5);
    size = static_cast<DWORD>(oversized.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            oversized.data(),
            &size),
        ERROR_SUCCESS,
        "DWORD oversized buffer");
    DWORD oversized_value = 0;
    std::memcpy(&oversized_value, oversized.data(), sizeof(oversized_value));
    failures += expect(oversized_value == expected, "DWORD oversized value");
    failures += expect(
        oversized[4] == 0xA5 && oversized[7] == 0xA5,
        "DWORD oversized tail untouched");

    std::array<BYTE, 3> short_buffer{0x5A, 0x5A, 0x5A};
    size = static_cast<DWORD>(short_buffer.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            short_buffer.data(),
            &size),
        ERROR_MORE_DATA,
        "DWORD short buffer");
    failures += expect(size == sizeof(DWORD), "DWORD short required size");
    failures += expect(
        short_buffer == std::array<BYTE, 3>{0x5A, 0x5A, 0x5A},
        "DWORD short buffer not overwritten");
    failures += expect(
        state.query_calls == original_calls,
        "owned DWORD never calls original query");
    return failures;
}

int expect_string_override(
    RegistryConfigOverride& overlay,
    FakeRegistryState& state,
    HKEY handle,
    const char* name,
    std::string_view expected) {
    int failures = 0;
    const int original_calls = state.query_calls;
    const DWORD required = static_cast<DWORD>(expected.size() + 1);

    DWORD type = 0;
    DWORD size = 0;
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            nullptr,
            &size),
        ERROR_SUCCESS,
        "string size probe");
    failures += expect(type == REG_SZ, "string probe type");
    failures += expect(size == required, "string probe includes NUL");

    std::vector<BYTE> exact(required, 0xCC);
    size = required;
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            exact.data(),
            &size),
        ERROR_SUCCESS,
        "string exact buffer");
    failures += expect(type == REG_SZ, "string exact type");
    failures += expect(size == required, "string exact size");
    failures += expect(
        std::string_view{
            reinterpret_cast<const char*>(exact.data()),
            expected.size()} == expected,
        "string exact bytes");
    failures += expect(exact.back() == 0, "string exact terminator");

    std::vector<BYTE> oversized(required + 4, 0xA5);
    size = static_cast<DWORD>(oversized.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            oversized.data(),
            &size),
        ERROR_SUCCESS,
        "string oversized buffer");
    failures += expect(
        oversized[required] == 0xA5 && oversized.back() == 0xA5,
        "string oversized tail untouched");

    std::vector<BYTE> short_buffer(required - 1, 0x5A);
    size = static_cast<DWORD>(short_buffer.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            short_buffer.data(),
            &size),
        ERROR_MORE_DATA,
        "string short buffer");
    failures += expect(size == required, "string short required size");
    failures += expect(
        short_buffer.front() == 0x5A && short_buffer.back() == 0x5A,
        "string short buffer not overwritten");
    failures += expect(
        state.query_calls == original_calls,
        "owned string never calls original query");
    return failures;
}

} // namespace
```

- [ ] **Step 3: Add tracked-open, DWORD, string, and invalid-argument tests**

Start `main()` with these configuration, open, and native-buffer cases:

```cpp
int main() {
    using namespace gc::nesys_service;
    int failures = 0;

    RegistryConfig config{};
    config.game().country = GameCountry::GrooveCoasterEng;
    config.nesys().game_kind = 303802;
    config.nesys().event_next_time = 0;
    config.nesys().condition_time = 1;
    config.nesys().log_level = 2;
    config.nesys().news_path = "N:\\news";
    config.nesys().event_path = "E:\\event";
    config.nesys().log_path = "L:\\log";

    const auto values = CreateRegistryOverrideValues(config);
    failures += expect(values.has_value(), "valid immutable override values");
    if (!values.has_value()) {
        return 1;
    }

    FakeRegistryState state{};
    g_fake = &state;
    RegistryConfigOverride game{ProcessRole::Game, *values};
    RegistryConfigOverride service{ProcessRole::Service, *values};

    const auto game_handle = reinterpret_cast<HKEY>(0x1001);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\taito\\typex",
        17,
        KEY_ALL_ACCESS,
        game_handle,
        &failures);
    failures += expect(state.last_root == HKEY_LOCAL_MACHINE, "game root unchanged");
    failures += expect(
        state.last_subkey == "SOFTWARE\\taito\\typex",
        "game subkey unchanged");
    failures += expect(state.last_options == 17, "game options unchanged");
    failures += expect(state.last_access == KEY_ALL_ACCESS, "game access unchanged");
    failures += expect_dword_override(
        game,
        state,
        game_handle,
        "Country",
        2);
    failures += expect_dword_override(
        game,
        state,
        game_handle,
        "cOuNtRy",
        2);

    DWORD type = 0;
    DWORD size = 0;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            reinterpret_cast<LPDWORD>(1),
            &type,
            nullptr,
            &size),
        ERROR_INVALID_PARAMETER,
        "non-null reserved pointer");
    DWORD country = 0;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&country),
            nullptr),
        ERROR_INVALID_PARAMETER,
        "data without size pointer");
    type = 0;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            nullptr,
            &type,
            nullptr,
            nullptr),
        ERROR_SUCCESS,
        "type-only query with null data and size");
    failures += expect(type == REG_DWORD, "type-only query type");

    const auto service_handle = reinterpret_cast<HKEY>(0x2001);
    track(
        service,
        state,
        HKEY_LOCAL_MACHINE,
        "software\\TAITO\\TYPEX",
        0,
        KEY_READ,
        service_handle,
        &failures);
    failures += expect(state.last_access == KEY_READ, "service access unchanged");

    struct DwordCase {
        const char* name;
        DWORD value;
    };
    constexpr std::array<DwordCase, 4> dword_cases{{
        {"GameKind", 303802},
        {"EventNextTime", 0},
        {"ConditionTime", 1},
        {"LogLevel", 2},
    }};
    for (const auto& test : dword_cases) {
        failures += expect_dword_override(
            service,
            state,
            service_handle,
            test.name,
            test.value);
    }

    struct StringCase {
        const char* name;
        std::string_view value;
    };
    constexpr std::array<StringCase, 3> string_cases{{
        {"NewsPath", "N:\\news"},
        {"EventPath", "E:\\event"},
        {"LogPath", "L:\\log"},
    }};
    for (const auto& test : string_cases) {
        failures += expect_string_override(
            service,
            state,
            service_handle,
            test.name,
            test.value);
    }

```

- [ ] **Step 4: Add pass-through ownership and original-argument preservation tests**

Continue the same `main()` function with:

```cpp
    DWORD pass_reserved = 0;
    DWORD pass_type = 0;
    DWORD pass_size = 1;
    BYTE pass_data = 0;
    state.query_status = ERROR_ACCESS_DENIED;
    failures += expect_status(
        service.Query(
            fake_query,
            service_handle,
            "TrafficCount",
            &pass_reserved,
            &pass_type,
            &pass_data,
            &pass_size),
        ERROR_ACCESS_DENIED,
        "TrafficCount original failure preserved");
    failures += expect(
        state.last_query_handle == service_handle &&
            state.last_value_name == "TrafficCount" &&
            state.last_reserved == &pass_reserved &&
            state.last_type == &pass_type &&
            state.last_data == &pass_data &&
            state.last_data_size == &pass_size,
        "pass-through query arguments unchanged");
    state.query_status = ERROR_FILE_NOT_FOUND;

    const int before_pass_through = state.query_calls;
    constexpr std::array<const char*, 4> service_pass_through{
        "TrafficCount",
        "CoinCredit",
        "NetworkAddress",
        "Country",
    };
    for (const auto* name : service_pass_through) {
        failures += expect_status(
            service.Query(
                fake_query,
                service_handle,
                name,
                nullptr,
                nullptr,
                nullptr,
                nullptr),
            ERROR_FILE_NOT_FOUND,
            name);
    }
    failures += expect(
        state.query_calls ==
            before_pass_through +
                static_cast<int>(service_pass_through.size()),
        "service unowned values call original");
    const int before_null_name = state.query_calls;
    failures += expect_status(
        service.Query(
            fake_query,
            service_handle,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "null value name pass-through");
    failures += expect(
        state.query_calls == before_null_name + 1 &&
            state.last_value_name == "<null>",
        "null value name reaches original query");

    const int before_game_unowned = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "GameKind",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "game does not own GameKind");
    failures += expect(
        state.query_calls == before_game_unowned + 1,
        "game unowned query calls original");

    const auto unrelated_handle = reinterpret_cast<HKEY>(0x3001);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\ControlSet001\\Control\\Biosinfo",
        0,
        KEY_READ,
        unrelated_handle,
        &failures);
    const int before_bios = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            unrelated_handle,
            "SystemBiosDate",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "SystemBiosDate pass-through");
    failures += expect(
        state.query_calls == before_bios + 1,
        "unrelated key calls original query");

    const auto wrong_root_handle = reinterpret_cast<HKEY>(0x3002);
    track(
        game,
        state,
        HKEY_CURRENT_USER,
        "SOFTWARE\\taito\\typex",
        0,
        KEY_READ,
        wrong_root_handle,
        &failures);
    const int before_wrong_root = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            wrong_root_handle,
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "Type X path below wrong root pass-through");
    failures += expect(
        state.query_calls == before_wrong_root + 1,
        "wrong-root handle calls original query");

    state.open_status = ERROR_ACCESS_DENIED;
    state.next_handle = reinterpret_cast<HKEY>(0x4001);
    HKEY failed_open = nullptr;
    failures += expect_status(
        game.Open(
            fake_open,
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\taito\\typex",
            0,
            KEY_ALL_ACCESS,
            &failed_open),
        ERROR_ACCESS_DENIED,
        "physical Type X open failure preserved");
    const int before_failed_handle = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            reinterpret_cast<HKEY>(0x4001),
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "failed open never tracked");
    failures += expect(
        state.query_calls == before_failed_handle + 1,
        "failed-open handle passes through");

```

- [ ] **Step 5: Add simultaneous-handle, failed-close, and handle-reuse tests**

Continue the same `main()` function with:

```cpp
    const auto second_handle = reinterpret_cast<HKEY>(0x1002);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\TAITO\\TYPEX",
        0,
        KEY_READ,
        second_handle,
        &failures);
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    state.close_status = ERROR_SUCCESS;
    failures += expect_status(
        game.Close(fake_close, game_handle),
        ERROR_SUCCESS,
        "first handle close");
    const int before_closed_query = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "closed handle no stale ownership");
    failures += expect(
        state.query_calls == before_closed_query + 1,
        "closed handle calls original query");
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    state.close_status = ERROR_BUSY;
    failures += expect_status(
        game.Close(fake_close, second_handle),
        ERROR_BUSY,
        "failed close status");
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    state.close_status = ERROR_SUCCESS;
    failures += expect_status(
        game.Close(fake_close, second_handle),
        ERROR_SUCCESS,
        "successful close after failure");
    const int before_reuse = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            second_handle,
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "reused numeric handle not implicitly tracked");
    failures += expect(
        state.query_calls == before_reuse + 1,
        "stale reused handle calls original");
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\taito\\typex",
        0,
        KEY_READ,
        second_handle,
        &failures);
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

```

- [ ] **Step 6: Add exact hook-inventory and invalid-snapshot tests**

Finish the same `main()` function with:

```cpp
    const auto exports = RegistryOverrideHookExports();
    failures += expect(exports.size() == 3, "exact registry hook count");
    constexpr std::array<std::string_view, 3> expected_exports{
        "RegOpenKeyExA",
        "RegQueryValueExA",
        "RegCloseKey",
    };
    for (std::size_t index = 0; index < expected_exports.size(); ++index) {
        failures += expect(
            exports[index] == expected_exports[index],
            "registry hook export order");
    }
    constexpr std::array<std::string_view, 8> forbidden_exports{
        "RegOpenKeyExW",
        "RegQueryValueExW",
        "RegEnumKeyExA",
        "RegEnumValueA",
        "RegCreateKeyExA",
        "RegSetValueExA",
        "RegDeleteKeyA",
        "RegDeleteValueA",
    };
    for (const auto forbidden : forbidden_exports) {
        for (const auto* exported : exports) {
            failures += expect(
                forbidden != exported,
                "forbidden registry export absent");
        }
    }

    std::vector<ApiHookRequest> requests;
    AppendRegistryOverrideHookRequests(requests);
    failures += expect(requests.size() == 3, "three registry hook requests");
    for (std::size_t index = 0; index < requests.size(); ++index) {
        failures += expect(
            std::wstring_view{requests[index].module_name} == L"Advapi32.dll",
            "registry hook module");
        failures += expect(
            std::string_view{requests[index].export_name} ==
                expected_exports[index],
            "registry request export order");
        failures += expect(
            requests[index].detour != nullptr &&
                requests[index].original != nullptr,
            "registry request has detour and trampoline slot");
    }

    RegistryConfig invalid = config;
    invalid.nesys().log_path = std::string(260, 'x');
    failures += expect(
        !CreateRegistryOverrideValues(invalid).has_value(),
        "invalid config cannot become immutable override state");

    return failures == 0 ? 0 : 1;
}
```

- [ ] **Step 7: Run the overlay test to verify it fails**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target RegistryConfigOverrideTests'
```

Expected: CMake/build fails because `RegistryConfigOverride.h`, `RegistryConfigOverride.cpp`, and their public interfaces do not exist.

- [ ] **Step 8: Define the immutable overlay and hook-registration interface**

Create `RegistryConfigOverride.h` with this complete content:

```cpp
#pragma once

#include <Windows.h>

#include "NesysHookTransaction.h"
#include "NesysServiceProcess.h"
#include "RegistryConfig.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace gc::nesys_service {

using RegOpenKeyExAFn = LSTATUS(WINAPI*)(
    HKEY,
    LPCSTR,
    DWORD,
    REGSAM,
    PHKEY);
using RegQueryValueExAFn = LSTATUS(WINAPI*)(
    HKEY,
    LPCSTR,
    LPDWORD,
    LPDWORD,
    LPBYTE,
    LPDWORD);
using RegCloseKeyFn = LSTATUS(WINAPI*)(HKEY);

struct RegistryOverrideValues {
    DWORD country{0};
    DWORD game_kind{0};
    DWORD event_next_time{0};
    DWORD condition_time{0};
    DWORD log_level{0};
    std::string news_path;
    std::string event_path;
    std::string log_path;
};

std::optional<RegistryOverrideValues> CreateRegistryOverrideValues(
    const RegistryConfig& config);

class RegistryConfigOverride {
public:
    RegistryConfigOverride(
        ProcessRole role,
        RegistryOverrideValues values);

    RegistryConfigOverride(const RegistryConfigOverride&) = delete;
    RegistryConfigOverride& operator=(const RegistryConfigOverride&) = delete;

    LSTATUS Open(
        RegOpenKeyExAFn original,
        HKEY root,
        LPCSTR subkey,
        DWORD options,
        REGSAM access,
        PHKEY result);
    LSTATUS Query(
        RegQueryValueExAFn original,
        HKEY key,
        LPCSTR value_name,
        LPDWORD reserved,
        LPDWORD type,
        LPBYTE data,
        LPDWORD data_size) noexcept;
    LSTATUS Close(
        RegCloseKeyFn original,
        HKEY key) noexcept;

private:
    bool IsTracked(HKEY key) const noexcept;
    void LogFirstTrackedOpen() noexcept;
    void LogFirstOverride(
        std::size_t index,
        const char* value_name,
        DWORD type) noexcept;

    const ProcessRole role_;
    const RegistryOverrideValues values_;
    mutable std::mutex tracked_mutex_;
    std::unordered_set<HKEY> tracked_handles_;
    std::atomic_bool tracked_open_logged_{false};
    std::array<std::atomic_bool, 8> override_logged_{};
};

bool InitializeRegistryConfigOverride(
    ProcessRole role,
    const RegistryConfig& config) noexcept;
std::span<const char* const> RegistryOverrideHookExports() noexcept;
void AppendRegistryOverrideHookRequests(
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service
```

- [ ] **Step 9: Implement owned-value lookup and native query-buffer formatting**

Create `RegistryConfigOverride.cpp` with these exact includes, globals, and value lookup definitions:

```cpp
#include "RegistryConfigOverride.h"

#include <array>
#include <cstring>
#include <memory>
#include <utility>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

RegOpenKeyExAFn g_original_reg_open_key_ex_a = nullptr;
RegQueryValueExAFn g_original_reg_query_value_ex_a = nullptr;
RegCloseKeyFn g_original_reg_close_key = nullptr;
std::unique_ptr<RegistryConfigOverride> g_registry_override;

constexpr std::array<const char*, 3> kRegistryExports{
    "RegOpenKeyExA",
    "RegQueryValueExA",
    "RegCloseKey",
};

enum class OwnedValueIndex : std::size_t {
    Country = 0,
    GameKind = 1,
    EventNextTime = 2,
    ConditionTime = 3,
    LogLevel = 4,
    NewsPath = 5,
    EventPath = 6,
    LogPath = 7,
};

struct RegistryValueView {
    const char* name;
    DWORD type;
    const void* bytes;
    DWORD size;
    OwnedValueIndex index;
};

bool is_type_x_open(HKEY root, LPCSTR subkey) {
    return root == HKEY_LOCAL_MACHINE &&
        subkey != nullptr &&
        EqualsIgnoreCaseAscii(subkey, "SOFTWARE\\taito\\typex");
}

RegistryValueView dword_view(
    const char* name,
    const DWORD& value,
    OwnedValueIndex index) noexcept {
    return {name, REG_DWORD, &value, sizeof(value), index};
}

RegistryValueView string_view(
    const char* name,
    const std::string& value,
    OwnedValueIndex index) noexcept {
    return {
        name,
        REG_SZ,
        value.c_str(),
        static_cast<DWORD>(value.size() + 1),
        index,
    };
}

std::optional<RegistryValueView> find_owned_value(
    ProcessRole role,
    const RegistryOverrideValues& values,
    LPCSTR value_name) noexcept {
    if (value_name == nullptr) {
        return std::nullopt;
    }
    if (role == ProcessRole::Game) {
        if (EqualsIgnoreCaseAscii(value_name, "Country")) {
            return dword_view(
                "Country",
                values.country,
                OwnedValueIndex::Country);
        }
        return std::nullopt;
    }
    if (EqualsIgnoreCaseAscii(value_name, "GameKind")) {
        return dword_view(
            "GameKind",
            values.game_kind,
            OwnedValueIndex::GameKind);
    }
    if (EqualsIgnoreCaseAscii(value_name, "EventNextTime")) {
        return dword_view(
            "EventNextTime",
            values.event_next_time,
            OwnedValueIndex::EventNextTime);
    }
    if (EqualsIgnoreCaseAscii(value_name, "ConditionTime")) {
        return dword_view(
            "ConditionTime",
            values.condition_time,
            OwnedValueIndex::ConditionTime);
    }
    if (EqualsIgnoreCaseAscii(value_name, "LogLevel")) {
        return dword_view(
            "LogLevel",
            values.log_level,
            OwnedValueIndex::LogLevel);
    }
    if (EqualsIgnoreCaseAscii(value_name, "NewsPath")) {
        return string_view(
            "NewsPath",
            values.news_path,
            OwnedValueIndex::NewsPath);
    }
    if (EqualsIgnoreCaseAscii(value_name, "EventPath")) {
        return string_view(
            "EventPath",
            values.event_path,
            OwnedValueIndex::EventPath);
    }
    if (EqualsIgnoreCaseAscii(value_name, "LogPath")) {
        return string_view(
            "LogPath",
            values.log_path,
            OwnedValueIndex::LogPath);
    }
    return std::nullopt;
}

const char* registry_type_name(DWORD type) noexcept {
    return type == REG_DWORD ? "REG_DWORD" : "REG_SZ";
}

LSTATUS copy_registry_value(
    const RegistryValueView& value,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) noexcept {
    if (reserved != nullptr || (data != nullptr && data_size == nullptr)) {
        return ERROR_INVALID_PARAMETER;
    }
    if (type != nullptr) {
        *type = value.type;
    }
    if (data_size == nullptr) {
        return ERROR_SUCCESS;
    }

    const DWORD capacity = *data_size;
    *data_size = value.size;
    if (data == nullptr) {
        return ERROR_SUCCESS;
    }
    if (capacity < value.size) {
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, value.bytes, value.size);
    return ERROR_SUCCESS;
}
```

- [ ] **Step 10: Add the three thin Advapi32 detours**

Continue the same file with the detours. They delegate every decision to the initialized component; MinHook cannot enable them before it has populated all three original trampolines:

```cpp
LSTATUS WINAPI reg_open_key_ex_a_detour(
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    if (g_registry_override == nullptr ||
        g_original_reg_open_key_ex_a == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    try {
        return g_registry_override->Open(
            g_original_reg_open_key_ex_a,
            root,
            subkey,
            options,
            access,
            result);
    } catch (...) {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LSTATUS WINAPI reg_query_value_ex_a_detour(
    HKEY key,
    LPCSTR value_name,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) {
    if (g_registry_override == nullptr ||
        g_original_reg_query_value_ex_a == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    return g_registry_override->Query(
        g_original_reg_query_value_ex_a,
        key,
        value_name,
        reserved,
        type,
        data,
        data_size);
}

LSTATUS WINAPI reg_close_key_detour(HKEY key) {
    if (g_registry_override == nullptr ||
        g_original_reg_close_key == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    return g_registry_override->Close(g_original_reg_close_key, key);
}

} // namespace
```

- [ ] **Step 11: Implement immutable snapshots, tracked handles, pass-through dispatch, and diagnostics**

Continue with immutable-value creation and the testable class methods:

```cpp
std::optional<RegistryOverrideValues> CreateRegistryOverrideValues(
    const RegistryConfig& config) {
    if (!gc::registry_config::ValidateRegistryConfig(config).valid()) {
        return std::nullopt;
    }
    const auto& nesys = config.nesys();
    return RegistryOverrideValues{
        static_cast<DWORD>(
            gc::registry_config::GameCountryRegistryDword(
                config.game().country())),
        static_cast<DWORD>(nesys.game_kind()),
        static_cast<DWORD>(nesys.event_next_time()),
        static_cast<DWORD>(nesys.condition_time()),
        static_cast<DWORD>(nesys.log_level()),
        nesys.news_path(),
        nesys.event_path(),
        nesys.log_path(),
    };
}

RegistryConfigOverride::RegistryConfigOverride(
    ProcessRole role,
    RegistryOverrideValues values)
    : role_(role),
      values_(std::move(values)) {
}

void RegistryConfigOverride::LogFirstTrackedOpen() noexcept {
    if (tracked_open_logged_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO
            << "RegistryConfigOverride: first tracked Type X open"
            << " role=" << ProcessRoleName(role_);
    } catch (...) {
    }
}

void RegistryConfigOverride::LogFirstOverride(
    std::size_t index,
    const char* value_name,
    DWORD type) noexcept {
    if (override_logged_[index].exchange(true, std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO
            << "RegistryConfigOverride: first value override"
            << " role=" << ProcessRoleName(role_)
            << " value=" << value_name
            << " type=" << registry_type_name(type);
    } catch (...) {
    }
}

LSTATUS RegistryConfigOverride::Open(
    RegOpenKeyExAFn original,
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    if (original == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    const LSTATUS status =
        original(root, subkey, options, access, result);
    if (status != ERROR_SUCCESS ||
        result == nullptr ||
        *result == nullptr ||
        !is_type_x_open(root, subkey)) {
        return status;
    }
    {
        std::scoped_lock lock(tracked_mutex_);
        tracked_handles_.insert(*result);
    }
    LogFirstTrackedOpen();
    return status;
}

bool RegistryConfigOverride::IsTracked(HKEY key) const noexcept {
    std::scoped_lock lock(tracked_mutex_);
    return tracked_handles_.contains(key);
}

LSTATUS RegistryConfigOverride::Query(
    RegQueryValueExAFn original,
    HKEY key,
    LPCSTR value_name,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) noexcept {
    if (original == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    if (!IsTracked(key)) {
        return original(
            key,
            value_name,
            reserved,
            type,
            data,
            data_size);
    }
    const auto owned = find_owned_value(role_, values_, value_name);
    if (!owned.has_value()) {
        return original(
            key,
            value_name,
            reserved,
            type,
            data,
            data_size);
    }
    const LSTATUS status = copy_registry_value(
        *owned,
        reserved,
        type,
        data,
        data_size);
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA) {
        LogFirstOverride(
            static_cast<std::size_t>(owned->index),
            owned->name,
            owned->type);
    }
    return status;
}

LSTATUS RegistryConfigOverride::Close(
    RegCloseKeyFn original,
    HKEY key) noexcept {
    if (original == nullptr) {
        return ERROR_INVALID_FUNCTION;
    }
    const LSTATUS status = original(key);
    if (status == ERROR_SUCCESS) {
        std::scoped_lock lock(tracked_mutex_);
        tracked_handles_.erase(key);
    }
    return status;
}
```

- [ ] **Step 12: Add process-lifetime initialization and exact hook requests**

Finish the file with process-lifetime initialization and exact hook requests:

```cpp
bool InitializeRegistryConfigOverride(
    ProcessRole role,
    const RegistryConfig& config) noexcept {
    try {
        auto values = CreateRegistryOverrideValues(config);
        if (!values.has_value()) {
            return false;
        }
        g_registry_override = std::make_unique<RegistryConfigOverride>(
            role,
            std::move(*values));
        return true;
    } catch (...) {
        return false;
    }
}

std::span<const char* const> RegistryOverrideHookExports() noexcept {
    return kRegistryExports;
}

void AppendRegistryOverrideHookRequests(
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"Advapi32.dll",
        "RegOpenKeyExA",
        reinterpret_cast<LPVOID>(&reg_open_key_ex_a_detour),
        reinterpret_cast<LPVOID*>(&g_original_reg_open_key_ex_a),
    });
    requests.push_back({
        L"Advapi32.dll",
        "RegQueryValueExA",
        reinterpret_cast<LPVOID>(&reg_query_value_ex_a_detour),
        reinterpret_cast<LPVOID*>(&g_original_reg_query_value_ex_a),
    });
    requests.push_back({
        L"Advapi32.dll",
        "RegCloseKey",
        reinterpret_cast<LPVOID>(&reg_close_key_detour),
        reinterpret_cast<LPVOID*>(&g_original_reg_close_key),
    });
}

} // namespace gc::nesys_service
```

- [ ] **Step 13: Compile the runtime overlay into `iDmacDrv32`**

Add `RegistryConfigOverride.cpp` to `SOURCES` immediately after `RegistryConfig.cpp`:

```cmake
        RegistryConfig.cpp
        RegistryConfigOverride.cpp
```

- [ ] **Step 14: Build and run the focused registry overlay test**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target RegistryConfigOverrideTests && ctest --test-dir build-msvc32-latest --output-on-failure -R RegistryConfigOverrideTests'
```

Expected: `RegistryConfigOverrideTests` passes. It proves exact/case-insensitive Type X recognition, unchanged `KEY_ALL_ACCESS`/`KEY_READ` opens, no fabricated key, role-scoped ownership, DWORD/string native buffer contracts, invalid argument handling, simultaneous handles, close failure preservation, stale-handle removal, physical pass-through values, and the exact three-hook inventory.

- [ ] **Step 15: Commit the registry overlay slice**

```powershell
git add -- RegistryConfigOverride.h RegistryConfigOverride.cpp CMakeLists.txt tests/RegistryConfigOverrideTests.cpp
git commit -m "feat: virtualize owned registry values"
```

Expected: the commit contains the focused component, its build wiring, and fake-registry tests; it does not yet activate the component from the process feature plan.

---

### Task 3: Independent Feature Composition and Transactional Process Lifecycle

**Files:**
- Modify: `NesysServiceProcess.h:16-27`
- Modify: `NesysServiceProcess.cpp:181-205`
- Modify: `NesysServicePatch.cpp:1-238`
- Test: `tests/NesysServicePatchTests.cpp:255-294`
- Test: `tests/NesysHookTransactionTests.cpp:93-159`

**Interfaces:**
- Consumes: `ConfigManager::GetEnableNesysServiceAdapterPatch()`, `GetEnableRegistryConfigOverride()`, `GetRegistryConfig()`, `InitializeRegistryConfigOverride()`, all component `Append*HookRequests()` functions, `OwnedMinHookTransaction`, the service ping preflight/prepare/activate/rollback lifecycle, and the existing suspended-child launcher.
- Produces: `ResolveNesysFeaturePlan(ProcessRole, bool network_enabled, bool registry_enabled) -> NesysFeaturePlan`, with explicit `network_virtualization`, `registry_virtualization`, `registry_config_override`, `service_launcher`, and exact exported-hook counts for all eight role/toggle combinations.

- [ ] **Step 1: Replace the Boolean-only feature-plan tests with the full role/toggle matrix**

In `tests/NesysServicePatchTests.cpp`, add this helper inside the anonymous namespace:

```cpp
int expect_plan(
    const gc::nesys_service::NesysFeaturePlan& actual,
    const gc::nesys_service::NesysFeaturePlan& expected,
    const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr
        << "Feature plan mismatch for " << name
        << ": enabled=" << actual.enabled
        << " network=" << actual.network_virtualization
        << " registry=" << actual.registry_virtualization
        << " synthetic_adapter=" << actual.synthetic_adapter
        << " server_address_override=" << actual.server_address_override
        << " registry_override=" << actual.registry_config_override
        << " launcher=" << actual.service_launcher
        << " ping=" << actual.service_ping_redirect
        << " hooks=" << actual.api_hook_count
        << "\n";
    return 1;
}
```

Replace the three old `ResolveNesysFeaturePlan(role, enabled)` assertions at the end of `main()` with this exact matrix:

```cpp
using gc::nesys_service::NesysFeaturePlan;
using gc::nesys_service::ProcessRole;
using gc::nesys_service::ResolveNesysFeaturePlan;

failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Game, false, false),
    NesysFeaturePlan{
        false, false, false, false, false, false, false, false, 0},
    "game network-off registry-off");
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Game, false, true),
    NesysFeaturePlan{
        true, false, true, false, false, true, true, false, 4},
    "game registry-only");
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Game, true, false),
    NesysFeaturePlan{
        true, true, false, true, true, false, true, false, 6},
    "game network-only");
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Game, true, true),
    NesysFeaturePlan{
        true, true, true, true, true, true, true, false, 9},
    "game combined");

failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Service, false, false),
    NesysFeaturePlan{
        false, false, false, false, false, false, false, false, 0},
    "service network-off registry-off");
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Service, false, true),
    NesysFeaturePlan{
        true, false, true, false, false, true, false, false, 3},
    "service registry-only");
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Service, true, false),
    NesysFeaturePlan{
        true, true, false, true, true, false, false, true, 10},
    "service network-only");
failures += expect_plan(
    ResolveNesysFeaturePlan(ProcessRole::Service, true, true),
    NesysFeaturePlan{
        true, true, true, true, true, true, false, true, 13},
    "service combined");
```

These counts are the exact exported-hook inventories:

| Role/mode | Network hooks | Registry hooks | Launcher hook | Total |
|---|---:|---:|---:|---:|
| Game, both off | 0 | 0 | 0 | 0 |
| Game, registry only | 0 | 3 | 1 | 4 |
| Game, network only | 5 | 0 | 1 | 6 |
| Game, combined | 5 | 3 | 1 | 9 |
| Service, both off | 0 | 0 | 0 | 0 |
| Service, registry only | 0 | 3 | 0 | 3 |
| Service, network only | 10 | 0 | 0 | 10 |
| Service, combined | 10 | 3 | 0 | 13 |

- [ ] **Step 2: Add a combined-component rollback regression**

In `tests/NesysHookTransactionTests.cpp`, add this case before the unrelated-target assertion:

```cpp
const auto registry_open = reinterpret_cast<LPVOID>(0x3000);
const auto registry_query = reinterpret_cast<LPVOID>(0x3100);
const auto registry_close = reinterpret_cast<LPVOID>(0x3200);
const auto network_hook = reinterpret_cast<LPVOID>(0x3300);
const std::vector<ResolvedApiHook> combined_hooks{
    hook(registry_open),
    hook(network_hook),
    hook(registry_query),
    hook(registry_close),
};

FakeState combined_failure{};
combined_failure.fail_queue_call = 2;
g_fake = &combined_failure;
OwnedMinHookTransaction combined_rollback(fake_api());
failures += expect(
    combined_rollback.Initialize(),
    "combined failure init");
failures += expect(
    combined_rollback.CreateAll(combined_hooks),
    "combined failure creates every owned hook");
failures += expect(
    !combined_rollback.Commit(),
    "combined queue failure");
failures += expect(
    contains(combined_failure.removed, registry_open) &&
        contains(combined_failure.removed, network_hook) &&
        contains(combined_failure.removed, registry_query) &&
        contains(combined_failure.removed, registry_close),
    "combined failure removes every network and registry target");
```

- [ ] **Step 3: Run the feature-plan tests to verify they fail**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests NesysHookTransactionTests'
```

Expected: `NesysServicePatchTests` fails to compile because the plan still accepts one Boolean, lacks registry fields, and lacks value equality. `NesysHookTransactionTests` compiles independently and its new rollback case passes.

- [ ] **Step 4: Extend `NesysFeaturePlan` to represent independent policies**

Replace `NesysFeaturePlan` and its resolver declaration in `NesysServiceProcess.h` with:

```cpp
struct NesysFeaturePlan {
    bool enabled{false};
    bool network_virtualization{false};
    bool registry_virtualization{false};
    bool synthetic_adapter{false};
    bool server_address_override{false};
    bool registry_config_override{false};
    bool service_launcher{false};
    bool service_ping_redirect{false};
    std::size_t api_hook_count{0};

    bool operator==(const NesysFeaturePlan&) const = default;
};

NesysFeaturePlan ResolveNesysFeaturePlan(
    ProcessRole role,
    bool network_enabled,
    bool registry_enabled) noexcept;
```

Replace the resolver implementation in `NesysServiceProcess.cpp` with:

```cpp
NesysFeaturePlan ResolveNesysFeaturePlan(
    ProcessRole role,
    bool network_enabled,
    bool registry_enabled) noexcept {
    NesysFeaturePlan plan{};
    plan.enabled = network_enabled || registry_enabled;
    plan.network_virtualization = network_enabled;
    plan.registry_virtualization = registry_enabled;

    if (network_enabled) {
        plan.synthetic_adapter = true;
        plan.server_address_override = true;
        if (role == ProcessRole::Game) {
            plan.api_hook_count += 5;
        } else {
            plan.api_hook_count += 10;
            plan.service_ping_redirect = true;
        }
    }

    if (registry_enabled) {
        plan.registry_config_override = true;
        plan.api_hook_count += 3;
    }

    if (role == ProcessRole::Game && plan.enabled) {
        plan.service_launcher = true;
        ++plan.api_hook_count;
    }

    return plan;
}
```

This preserves the existing network-only inventories while adding only the approved registry hooks and registry-only launcher transport.

- [ ] **Step 5: Prepare only enabled component state and resolve the exact exported inventory**

Add the registry component include to `NesysServicePatch.cpp`:

```cpp
#include "RegistryConfigOverride.h"
```

Replace `initialize_enabled_feature()` with this complete component-composition implementation:

```cpp
bool initialize_feature_plan(
    HMODULE loader_module,
    ProcessRole role,
    const NesysFeaturePlan& plan,
    const ConfigManager& config) {
    if (plan.server_address_override &&
        !InitializeServerAddressOverride(config.GetNesysServerIp())) {
        PLOG_ERROR
            << "NesysServicePatch: invalid NESYS server IPv4";
        return false;
    }

    if (plan.registry_config_override &&
        !InitializeRegistryConfigOverride(
            role,
            config.GetRegistryConfig())) {
        PLOG_ERROR
            << "NesysServicePatch: registry override state initialization failed";
        return false;
    }

    std::uintptr_t executable_base = 0;
    if (plan.service_ping_redirect) {
        executable_base = reinterpret_cast<std::uintptr_t>(
            GetModuleHandleW(nullptr));
        if (executable_base == 0) {
            PLOG_ERROR
                << "NesysServicePatch: main executable module unavailable";
            return false;
        }
    }

    std::vector<ApiHookRequest> requests;
    requests.reserve(plan.api_hook_count);
    if (plan.synthetic_adapter) {
        AppendSyntheticAdapterHookRequests(role, requests);
    }
    if (plan.server_address_override) {
        AppendServerAddressHookRequests(role, requests);
    }
    if (plan.registry_config_override) {
        AppendRegistryOverrideHookRequests(requests);
    }
    if (plan.service_launcher) {
        AppendNesysServiceLauncherHookRequest(requests);
    }
    if (requests.size() != plan.api_hook_count) {
        PLOG_ERROR
            << "NesysServicePatch: role hook count mismatch"
            << " expected=" << plan.api_hook_count
            << " actual=" << requests.size();
        return false;
    }

    std::vector<ResolvedApiHook> resolved;
    HookInstallError resolve_error{};
    if (!ResolveApiHooks(requests, &resolved, &resolve_error)) {
        log_hook_error(resolve_error);
        return false;
    }
    if (plan.service_ping_redirect &&
        !PreflightServicePingRedirect(executable_base)) {
        return false;
    }
    if (plan.service_launcher &&
        !InitializeNesysServiceLauncher(loader_module)) {
        PLOG_ERROR
            << "NesysServicePatch: launcher state initialization failed";
        return false;
    }

```

- [ ] **Step 6: Create, commit, and diagnose every enabled exported hook through one transaction**

Continue the same `initialize_feature_plan()` function with:

```cpp
    auto transaction = std::make_unique<OwnedMinHookTransaction>(
        ProductionMinHookApi());
    if (!transaction->Initialize()) {
        log_hook_error(transaction->error());
        return false;
    }
    if (!transaction->CreateAll(resolved)) {
        log_hook_error(transaction->error());
        return false;
    }
    if (plan.service_ping_redirect &&
        !PrepareServicePingRedirect(executable_base)) {
        transaction->Rollback();
        RollbackServicePingRedirect();
        return false;
    }
    if (!transaction->Commit()) {
        log_hook_error(transaction->error());
        RollbackServicePingRedirect();
        return false;
    }
    if (plan.service_ping_redirect &&
        !ActivateServicePingRedirect()) {
        transaction->Rollback();
        RollbackServicePingRedirect();
        return false;
    }

    g_owned_hooks = std::move(transaction);
    try {
        if (plan.synthetic_adapter) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=synthetic_network_adapter";
        }
        if (plan.server_address_override) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=server_address_override";
        }
        if (plan.registry_config_override) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=registry_config_override"
                << " owned_api_hooks=3";
        }
        if (plan.service_launcher) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=nesys_service_launcher";
        }
        if (plan.service_ping_redirect) {
            PLOG_INFO
                << "NesysServicePatch: component active"
                << " name=service_ping_redirect";
        }
        PLOG_INFO
            << "NesysServicePatch: all role hooks active"
            << " role=" << ProcessRoleName(role)
            << " network=" << plan.network_virtualization
            << " registry=" << plan.registry_virtualization
            << " api_hooks=" << plan.api_hook_count;
        if (plan.synthetic_adapter) {
            PLOG_INFO
                << "NesysServicePatch: synthetic adapter"
                << " name=" << kSyntheticAdapterName
                << " mac=DE-AD-BE-EF-00-01"
                << " index=0x" << std::hex
                << kSyntheticInterfaceIndex
                << " ipv4=" << kSyntheticIpv4
                << " link_state=up"
                << std::dec;
        }
    } catch (...) {
    }
    return true;
}
```

This function must not call `InitializeServerAddressOverride()`, `GetModuleHandleW(nullptr)`, `PreflightServicePingRedirect()`, `PrepareServicePingRedirect()`, or `ActivateServicePingRedirect()` for a registry-only plan.

- [ ] **Step 7: Feed both strict toggles into the process plan**

Replace the configuration/plan block inside `NesysServicePatchInit()` with:

```cpp
const auto& config = ConfigManager::instance();
const bool network_enabled =
    config.GetEnableNesysServiceAdapterPatch();
const bool registry_enabled =
    config.GetEnableRegistryConfigOverride();
const auto plan = ResolveNesysFeaturePlan(
    role,
    network_enabled,
    registry_enabled);
PLOG_INFO
    << "NesysServicePatch: init"
    << " role=" << ProcessRoleName(role)
    << " network=" << network_enabled
    << " registry=" << registry_enabled;

success = !plan.enabled ||
    initialize_feature_plan(
        loader_module,
        role,
        plan,
        config);
if (!plan.enabled) {
    PLOG_INFO
        << "NesysServicePatch: all policies disabled; installed no hooks";
}
```

Keep the existing initialization-state CAS, exception handling, final state store, and Boolean return unchanged around this replacement. Do not add a second MinHook transaction or a second service-launcher detour.

- [ ] **Step 8: Build and run the lifecycle, rollback, overlay, and configuration tests**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests RegistryConfigOverrideTests NesysHookTransactionTests NesysServicePatchTests && ctest --test-dir build-msvc32-latest --output-on-failure -R "ConfigFeatureTests|RegistryConfigOverrideTests|NesysHookTransactionTests|NesysServicePatchTests"'
```

Expected: all six targets build. The four selected tests pass, including all eight feature plans, network-only count preservation, registry-only launcher composition, registry-only service isolation, combined hook counts, combined rollback, and the existing terminate-without-resume child-initialization failure behavior.

- [ ] **Step 9: Commit the composed lifecycle slice**

```powershell
git add -- NesysServiceProcess.h NesysServiceProcess.cpp NesysServicePatch.cpp tests/NesysServicePatchTests.cpp tests/NesysHookTransactionTests.cpp
git commit -m "feat: compose registry and NESYS hook policies"
```

Expected: the commit activates registry virtualization through the existing fail-closed process boundary without modifying `dllmain.cpp`, launcher injection mechanics, or network-only behavior.

---

### Task 4: Complete Automated Verification and Runtime-Acceptance Handoff

**Files:**
- Verify: `CMakeLists.txt`
- Verify: `RegistryConfig.h`
- Verify: `RegistryConfig.cpp`
- Verify: `RegistryConfigOverride.h`
- Verify: `RegistryConfigOverride.cpp`
- Verify: `config.h`
- Verify: `config.cpp`
- Verify: `config.toml`
- Verify: `GUI_main.cpp`
- Verify: `NesysServiceProcess.h`
- Verify: `NesysServiceProcess.cpp`
- Verify: `NesysServicePatch.cpp`
- Verify: `tests/ConfigFeatureTests.cpp`
- Verify: `tests/RegistryConfigOverrideTests.cpp`
- Verify: `tests/NesysHookTransactionTests.cpp`
- Verify: `tests/NesysServicePatchTests.cpp`

**Interfaces:**
- Consumes: every deliverable from Tasks 1-3 and all pre-existing CTest targets.
- Produces: current x86 build/test/static evidence and a clearly separated user-owned runtime checklist; it does not produce an agent claim of runtime acceptance.

- [ ] **Step 1: Reconfigure the existing x86 Ninja build tree**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo'
```

Expected: CMake generation succeeds and registers `RegistryConfigOverrideTests` alongside the existing nine tests.

- [ ] **Step 2: Build the loader, ConfigGUI, registry tests, and existing NESYS tests**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests RegistryConfigOverrideTests NesysHookTransactionTests SyntheticNetworkAdapterTests ServerAddressOverrideTests NesysServicePatchTests'
```

Expected: all named targets build successfully under the x86 MSVC environment. `iDmacDrv32.dll` and `ConfigGUI.exe` are produced in `build-msvc32-latest`.

- [ ] **Step 3: Run the focused configuration, registry, transaction, and NESYS suite**

Run:

```powershell
ctest --test-dir build-msvc32-latest --output-on-failure -R "ConfigFeatureTests|RegistryConfigOverrideTests|NesysHookTransactionTests|SyntheticNetworkAdapterTests|ServerAddressOverrideTests|NesysServicePatchTests"
```

Expected: all six selected tests pass. This is automated implementation evidence only.

- [ ] **Step 4: Build every configured target and run the complete CTest suite**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest'
ctest --test-dir build-msvc32-latest --output-on-failure
```

Expected: the full build succeeds and CTest reports all ten tests passed, including all pre-existing non-NESYS tests.

- [ ] **Step 5: Prove the static registry boundary and absence of hash gates**

Run the positive inventory check:

```powershell
rg -n '"Reg(OpenKeyExA|QueryValueExA|CloseKey)"' RegistryConfigOverride.cpp
```

Expected: the production request table contains exactly `RegOpenKeyExA`, `RegQueryValueExA`, and `RegCloseKey`, all in `RegistryConfigOverride.cpp`.

Run the forbidden registry/country hooks check:

```powershell
rg -n --glob 'RegistryConfigOverride.*' --glob 'NesysServicePatch.*' 'RegOpenKeyExW|RegQueryValueExW|RegEnumKeyExA|RegEnumValueA|RegCreateKey|RegSetValue|RegDeleteKey|RegDeleteValue|country\.dat' .
```

Expected: no output and ripgrep exit code 1.

Run the production-wide mutation check:

```powershell
rg -n --glob '*.cpp' --glob '*.h' --glob '!tests/**' 'RegCreateKey|RegSetValue|RegDeleteKey|RegDeleteValue' .
```

Expected: no output and ripgrep exit code 1; GCLoader contains no registry creation, write, or deletion call.

Run the executable-hash gate check:

```powershell
rg -n --glob '*.cpp' --glob '*.h' 'FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522|487402D4ABDEF6A857A397CF25C9D681CB6F6052965C500361B0FD14D00913F2' .
```

Expected: no output and ripgrep exit code 1; neither binary hash is a runtime gate.

- [ ] **Step 6: Inspect the implementation diff and repository state without absorbing unrelated work**

Run:

```powershell
git diff --check
git status --short
git log -3 --oneline
```

Expected: `git diff --check` reports no whitespace errors. The three task commits are present with the planned messages. Any pre-existing unrelated user changes remain unstaged/uncommitted by these tasks and are reported separately rather than altered.

If any automated check fails, return to the task that owns the failing behavior, add a focused failing regression there, fix it, rerun that task's focused command, and then repeat Steps 1-6. Do not hide a verification repair in a catch-all commit.

- [ ] **Step 7: Hand the runtime checklist to the user and stop at the automated boundary**

Report the exact build and test commands plus their observed results. Then ask the user to perform and accept this checklist:

- With registry virtualization disabled, confirm original registry behavior is unchanged.
- With registry virtualization enabled and network virtualization disabled, confirm the service is still injected and only registry hooks activate.
- Confirm `GrooveCoasterJpn`, `Rhythmvaders`, and `GrooveCoasterEng` produce the expected branding/language and effective DWORD values 0, 1, and 2.
- Confirm the expected environment does not successfully replace the configured country through `country.dat`.
- Change the physical owned NESYS values to distinguishable values and confirm the service observes the TOML values instead.
- Confirm `TrafficCount` still comes from the physical registry and that a missing value retains the original service failure.
- Confirm `SystemBiosDate` remains physical.
- Confirm NIC registry enumeration is untouched and no synthetic `NetworkAddress` is created.
- Confirm the adapter APIs still report the existing synthetic MAC independently of registry state.
- Confirm the configured timers, log level, and news/event/log directories have the expected runtime effects.
- Confirm no registry value is written, created, or deleted by GCLoader.

Expected handoff state: automated implementation verification is complete, but the feature remains explicitly **runtime acceptance pending** until the user reports the checklist result.

## Self-Review

- **Spec coverage:** Task 1 covers the complete strict TOML tree, disabled default, country enum names and DWORD mapping, seven NESYS defaults, semantic limits, ConfigGUI controls, shared serialization, and invalid-save gating. Task 2 covers exact Type X recognition, original-first opens, immutable role-owned values, native DWORD/string query behavior, multiple handles, close semantics, bounded diagnostics, pass-through values, and the exact three hooks. Task 3 covers all four policy combinations in both roles, registry-only injection, network-only preservation, one owned transaction, service-ping isolation, rollback, and the existing fail-closed child handshake. Task 4 covers focused/full build and tests, static no-write/no-extra-hook/no-hash-gate proof, and the user-owned runtime boundary.
- **Placeholder scan:** Clean. Every code-edit step names exact files and contains concrete declarations, implementations, fixtures, commands, and expected results.
- **Type consistency:** `GameCountry` is the only country model and maps through `GameCountryRegistryDword()`. User-entered registry integers remain `std::int64_t` until shared validation proves the DWORD range, then `RegistryOverrideValues` snapshots them as `DWORD`. `ProcessRole`, `NesysFeaturePlan`, `RegistryOverrideValues`, all three registry function-pointer types, and every append/init signature are defined before their lifecycle consumers. Hook totals are consistent at game `0/4/6/9` and service `0/3/10/13` for off-off/registry-only/network-only/combined.
