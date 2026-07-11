# NESYS Network Virtualization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `game471.exe` and `NesysService.exe` see one immutable IPv4-only NESYS adapter and redirect every resolver-based server lookup to required `[nesys].server_ip` without changing Windows networking globally.

**Architecture:** Keep the existing one-DLL, process-role split. `SyntheticNetworkAdapter` owns native IP Helper buffer contracts, stable notification behavior, mutation suppression, and the service ping redirect; `ServerAddressOverride` owns validated process-lifetime address state plus modern and legacy resolver detours; `NesysServiceLauncher` owns suspended child injection and the readiness handshake. `NesysServicePatch` resolves every role-required export, creates only feature-owned hooks in a queued MinHook transaction, activates the guarded service mid-hook, and returns a Boolean gate to `DllMain` before any game-only initialization.

**Tech Stack:** C++23, Win32 x86 DLL, Windows IP Helper/Winsock APIs, MinHook commit `05c06c5bbca226b72ffb40fc0caaef33bcaf6f74`, SafetyHook v0.6.9, reflect-cpp v0.19.0 TOML, ImGui v1.91.9b, plog 1.1.10, CMake 3.31/Ninja, CTest, existing `build-msvc32-latest` MSVC x86 build.

## Global Constraints

- Source, tests, plans, and commits belong in `H:\gc\artifacts\GCLoader`. `H:\gc` is runtime/deploy state; do not commit `H:\gc\config.toml`, `H:\gc\loader-log.txt`, deployed executables, or deployed DLLs.
- Preserve the pre-existing untracked `.superpowers/` directory and never stage it.
- Keep `iDmacDrv32.dll` as the only injected DLL and retain the existing game/service process-role split.
- Add the required top-level schema exactly as `[nesys]` with `server_ip = '127.0.0.1'`.
- `server_ip` is not wrapped in `rfl::DefaultIfMissing`. A missing table or field must fail reflect-cpp parsing even when `experimental.enable_nesys_service_adapter_patch = false`.
- A default-constructed ConfigGUI model uses `127.0.0.1` and serializes the `[nesys]` table.
- Accept only four decimal IPv4 octets in the range `0` through `255` separated by three dots. Reject hostnames, IPv6, schemes, paths, ports, `host:port`, empty octets, extra octets, and out-of-range octets.
- The existing `experimental.enable_nesys_service_adapter_patch` Boolean gates adapter virtualization, resolver overrides, service injection, mutation suppression, and ping redirection as one feature.
- Disabled mode installs no NESYS hooks and does not intercept or inject `NesysService.exe`.
- The synthetic profile is immutable: name `GCLoaderNesys0`, description `GCLoader NESYS IPv4 Adapter`, MAC `DE-AD-BE-EF-00-01`, index `0x0BADC0DE`, Ethernet, IPv4 `192.0.2.2`, mask `255.255.255.0`, gateway/DHCP/DNS `192.0.2.1`, DHCP enabled, admin/oper state up, MTU `1500`, speed `1,000,000,000 bit/s`, and no IPv6.
- Every IP Helper detour must honor the native caller-buffer size-probe, undersized-buffer, successful-buffer, pointer, list-termination, and no-overrun contracts.
- Leave `GetIpNetTable` unhooked. Suppress only `IpReleaseAddress`, `IpRenewAddress`, and `FlushIpNetTable`.
- Hook `WS2_32!GetAddrInfoW` and `WS2_32!GetAddrInfoExW` in both processes, plus `WS2_32!gethostbyname` only in the service.
- For non-null modern resolver nodes, pass only the configured numeric IPv4 to the original trampoline, force `AF_INET` and `AI_NUMERICHOST`, clear `AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL`, and preserve every other argument.
- Null resolver nodes pass through unchanged. Resolver failure never retries the original hostname.
- Asynchronous `GetAddrInfoExW` normalized hints live for the process lifetime and are deduplicated by normalized flags, socket type, and protocol.
- Preserve Winsock allocation/free ownership; do not hook `FreeAddrInfoW` or `FreeAddrInfoExW`.
- Legacy non-null `gethostbyname` returns thread-local `hostent` storage with the requested name, no aliases, `AF_INET`, and the configured address bytes. A null name passes through.
- Do not hook `connect`, `WSAConnect`, `WinHttpConnect`, `WinHttpSendRequest`, direct numeric raw-TCP destinations, URL construction, request paths, ports, headers, SNI, or certificate handling.
- The service ping target is `NesysService.exe module base + RVA 0x8E40` and is guarded by the exact approved 32-byte function-entry signature. Do not hash the executable and do not pattern-scan.
- The x86 ping callback replaces only saved `EAX` with process-lifetime `"127.0.0.1"` and resumes original execution.
- Resolve all exported API targets and validate the service fixed-RVA signature before creating hooks.
- Never call `MH_EnableHook(MH_ALL_HOOKS)`, `MH_DisableHook(MH_ALL_HOOKS)`, or `MH_QueueEnableHook(MH_ALL_HOOKS)` from the NESYS feature. Queue and roll back only the exact targets owned by this feature.
- When enabled, a local initialization failure returns `FALSE` from `DllMain(DLL_PROCESS_ATTACH)`.
- A failed injected-service `LoadLibraryW` readiness result terminates the still-suspended child, waits, closes owned handles, zeros the caller's `PROCESS_INFORMATION`, sets `ERROR_DLL_INIT_FAILED`, and returns `FALSE`. It never resumes an unpatched child.
- On successful injection, resume the service primary thread only when the original caller did not request `CREATE_SUSPENDED`.
- Bound direct logging to startup/component outcomes and the first invocation of each adapter-query, adapter-notification, modern-resolver, legacy-resolver, mutation-suppression, and ping-redirection family.
- Unit tests use synthetic buffers and fake API backends; they must not patch a live game or service process.
- Build `iDmacDrv32`, `ConfigGUI`, every focused NESYS test, and the existing tests with the x86 toolchain, then run the complete CTest suite.

---

## Scope Check

This is one coherent fail-closed feature. Configuration alone is not useful; adapter virtualization without resolver control still leaves server routing dependent on the host; resolver control without adapter virtualization still permits the verified zero-MAC startup stall; and service injection without a readiness gate can resume a partially patched process. Keep one plan, with reviewer-sized tasks that each establish a testable interface consumed by the final transaction.

The approved design already contains daemon-backed binary proof. Implementation must not reopen adapter-selection heuristics or substitute a different destination mechanism. Re-run IDA only if the deployed executable, `RVA 0x8E40` bytes, or imported API set differs during execution.

## File Structure

- Create `NesysNetworkConfig.h` / `NesysNetworkConfig.cpp`: allocation-free dotted-decimal IPv4 parser shared by runtime, tests, and ConfigGUI.
- Modify `config.h` / `config.cpp`: required `NesysConfig` model, getter, and runtime syntax rejection.
- Modify `GUI_main.cpp`: dedicated NESYS section, editable server IPv4, inline invalid state, and disabled save while invalid.
- Modify `tests/ConfigFeatureTests.cpp`: schema strictness, defaults, serialization, valid classes, and invalid syntax matrix.
- Create `NesysHookTransaction.h` / `NesysHookTransaction.cpp`: exported-target resolution plus exact-target MinHook create/queue/apply/rollback.
- Create `tests/NesysHookTransactionTests.cpp`: fake-backend proof that partial creation and activation roll back only owned targets.
- Create `SyntheticNetworkAdapter.h` / `SyntheticNetworkAdapter.cpp`: immutable profile, all IP Helper detours, role-specific hook requests, notification behavior, mutation suppression, fixed-RVA signature guard, and SafetyHook ping redirect.
- Create `tests/SyntheticNetworkAdapterTests.cpp`: native buffer contracts, exact structures, cross-API identity, notification behavior, hook inventory, and ping guard/callback.
- Create `ServerAddressOverride.h` / `ServerAddressOverride.cpp`: immutable canonical ANSI/Wide address, hint normalization/cache, modern and legacy resolver dispatch, and role-specific hook requests.
- Create `tests/ServerAddressOverrideTests.cpp`: argument preservation, null pass-through, async lifetime/concurrency, no fallback, result ownership, and thread-local `hostent`.
- Create `NesysServiceLauncher.h` / `NesysServiceLauncher.cpp`: current-DLL injection, child finalization policy, `CreateProcessA` detour, and game-only hook request.
- Modify `NesysServiceProcess.h` / `NesysServiceProcess.cpp`: keep launch matching/role helpers and add the pure role/feature component plan used by lifecycle tests.
- Modify `tests/NesysServicePatchTests.cpp`: launcher success/failure cleanup, caller suspension preservation, and disabled/game/service component plans.
- Replace `NesysServicePatch.h` / `NesysServicePatch.cpp`: Boolean orchestration gate and process-lifetime owned hook state.
- Modify `dllmain.cpp`: run the NESYS gate immediately after logging/role detection and return `FALSE` before game-only patches on failure.
- Modify `CMakeLists.txt`: compile the new sources and register four focused test executables.

### Task 1: Required NESYS IPv4 Configuration and ConfigGUI Validation

**Files:**
- Create: `NesysNetworkConfig.h`
- Create: `NesysNetworkConfig.cpp`
- Modify: `config.h:49-94,168-179`
- Modify: `config.cpp:1-35`
- Modify: `GUI_main.cpp:1-18,318-445,562-590`
- Modify: `tests/ConfigFeatureTests.cpp:15-89,151-317`
- Modify: `CMakeLists.txt:102-122,167-180`

**Interfaces:**
- Consumes: reflect-cpp's existing strict required-field behavior and the current ConfigGUI dirty/save flow.
- Produces:
  - `std::optional<std::array<std::uint8_t, 4>> ParseDottedDecimalIpv4(std::string_view) noexcept`
  - `bool IsDottedDecimalIpv4(std::string_view) noexcept`
  - `struct NesysConfig { rfl::Rename<"server_ip", std::string> server_ip = "127.0.0.1"; }`
  - `const std::string& ConfigManager::GetNesysServerIp() const`
  - ConfigGUI label `NESYS Server IPv4` and a save gate driven by the same parser

- [ ] **Step 1: Add failing schema, parser, and default-serialization tests**

In `tests/ConfigFeatureTests.cpp`, add:

~~~cpp
#include "NesysNetworkConfig.h"

#include <array>
~~~

Update the fixtures exactly as follows:

- Insert the table below between `card_read` and `[experimental]` in `kDefaultExperimentalConfig` and `kEnabledExperimentalConfig`.
- Append the table below to `kDefaultCardReadConfig` so the existing "missing experimental table" case remains isolated to that table.
- Prepend the table below to `kDefaultExperimentalTable` so the existing "missing card_read" case still contains a valid NESYS table.
- Insert the table below between `card_read = ';'` and `[experimental]` in the punctuation fixture.
- Leave bare `kRequiredConfigPrefix` without the table so its missing-schema test continues to fail.

~~~toml
[nesys]
server_ip = '127.0.0.1'

~~~

After `expect_string`, add:

~~~cpp
int expect_ipv4_valid(std::string_view value, bool expected, const char* name) {
    const bool actual = gc::nesys_service::IsDottedDecimalIpv4(value);
    if (actual == expected) {
        return 0;
    }

    std::cerr << "Expected " << name << " validity to be " << expected
              << ", got " << actual << "\n";
    return 1;
}
~~~

Add these assertions in `main()` after parsing `upgraded_defaults`:

~~~cpp
failures += expect_string(
    upgraded_defaults.nesys().server_ip(),
    "127.0.0.1",
    "default NESYS server IPv4");

InputConfig generated_defaults{};
failures += expect_string(
    generated_defaults.nesys().server_ip(),
    "127.0.0.1",
    "constructed ConfigGUI NESYS server IPv4");
const auto generated_toml = rfl::toml::write(generated_defaults);
failures += expect_bool(
    generated_toml.find("[nesys]") != std::string::npos,
    true,
    "generated TOML NESYS table");
failures += expect_bool(
    generated_toml.find("server_ip") != std::string::npos,
    true,
    "generated TOML NESYS server field");

const auto valid_nesys_config =
    std::string(kRequiredConfigPrefix) + kDefaultExperimentalConfig;
failures += expect_parse_failure(
    replace_once(
        valid_nesys_config,
        "[nesys]\nserver_ip = '127.0.0.1'\n\n",
        ""),
    "missing NESYS table");
failures += expect_parse_failure(
    replace_once(
        valid_nesys_config,
        "server_ip = '127.0.0.1'\n",
        ""),
    "missing NESYS server_ip");

constexpr std::array<std::string_view, 5> valid_ipv4{
    "127.0.0.1",
    "10.23.45.67",
    "192.168.100.200",
    "203.0.113.9",
    "255.255.255.255",
};
for (const auto value : valid_ipv4) {
    failures += expect_ipv4_valid(value, true, std::string(value).c_str());
}

constexpr std::array<std::string_view, 13> invalid_ipv4{
    "",
    "localhost",
    "::1",
    "http://127.0.0.1",
    "127.0.0.1/path",
    "127.0.0.1:80",
    "1.2.3",
    "1.2.3.4.5",
    ".1.2.3",
    "1..2.3",
    "1.2.3.",
    "256.1.2.3",
    "1.2.-3.4",
};
for (const auto value : invalid_ipv4) {
    failures += expect_ipv4_valid(value, false, std::string(value).c_str());
}

auto custom_server_text = replace_once(
    valid_nesys_config,
    "server_ip = '127.0.0.1'",
    "server_ip = '10.23.45.67'");
const auto custom_server = parse_config(custom_server_text);
const auto custom_server_round_trip =
    parse_config(rfl::toml::write(custom_server));
failures += expect_string(
    custom_server_round_trip.nesys().server_ip(),
    "10.23.45.67",
    "custom NESYS server round-trip");
~~~

- [ ] **Step 2: Build the focused test and verify the red state**

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests'
~~~

Expected: compilation fails because `NesysNetworkConfig.h`, `InputConfig::nesys`, and `NesysConfig` do not exist.

- [ ] **Step 3: Implement the allocation-free IPv4 syntax parser**

Create `NesysNetworkConfig.h`:

~~~cpp
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gc::nesys_service {

using Ipv4Octets = std::array<std::uint8_t, 4>;

std::optional<Ipv4Octets> ParseDottedDecimalIpv4(
    std::string_view text) noexcept;

bool IsDottedDecimalIpv4(std::string_view text) noexcept;

} // namespace gc::nesys_service
~~~



Create `NesysNetworkConfig.cpp`:

~~~cpp
#include "NesysNetworkConfig.h"

#include <charconv>
#include <system_error>

namespace gc::nesys_service {

std::optional<Ipv4Octets> ParseDottedDecimalIpv4(
    std::string_view text) noexcept {
    Ipv4Octets octets{};
    std::size_t begin = 0;

    for (std::size_t index = 0; index < octets.size(); ++index) {
        std::size_t end = text.find('.', begin);
        if (index + 1 < octets.size()) {
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
        } else {
            if (end != std::string_view::npos) {
                return std::nullopt;
            }
            end = text.size();
        }

        const auto token = text.substr(begin, end - begin);
        if (token.empty() || token.size() > 3) {
            return std::nullopt;
        }

        unsigned value = 0;
        const auto [parsed_end, error] = std::from_chars(
            token.data(),
            token.data() + token.size(),
            value,
            10);
        if (error != std::errc{} ||
            parsed_end != token.data() + token.size() ||
            value > 255) {
            return std::nullopt;
        }

        octets[index] = static_cast<std::uint8_t>(value);
        begin = end + 1;
    }

    return octets;
}

bool IsDottedDecimalIpv4(std::string_view text) noexcept {
    return ParseDottedDecimalIpv4(text).has_value();
}

} // namespace gc::nesys_service
~~~

- [ ] **Step 4: Add the required reflect-cpp model and runtime getter**

In `config.h`, add before `ExperimentalConfig`:

~~~cpp
struct NesysConfig
{
    rfl::Rename<"server_ip", std::string> server_ip = "127.0.0.1";
};
~~~

Add the required field to `InputConfig` before `experimental`:

~~~cpp
rfl::Rename<"nesys", NesysConfig> nesys;
~~~

Add this public getter beside the experimental getters:

~~~cpp
const std::string& GetNesysServerIp() const {
    return config.nesys.value().server_ip.value();
}
~~~

In `config.cpp`, include the parser and validate the parsed value before assigning `config`:

~~~cpp
#include "NesysNetworkConfig.h"
~~~

~~~cpp
if (result)
{
    const auto& server_ip = result.value().nesys().server_ip();
    if (!gc::nesys_service::IsDottedDecimalIpv4(server_ip))
    {
        throw std::runtime_error(
            "Invalid [nesys].server_ip; expected dotted-decimal IPv4");
    }

    config = result.value();
    PLOG_DEBUG << "Config file parsed successfully" << std::endl;
    PLOG_DEBUG << "Loaded: " << rfl::json::write(config) << std::endl;
    return;
}
~~~

- [ ] **Step 5: Add the dedicated GUI field and invalid-save gate**

In `GUI_main.cpp`, add:

~~~cpp
#include "misc/cpp/imgui_stdlib.h"
#include "NesysNetworkConfig.h"
~~~

Immediately before `ImGui::SeparatorText("Experimental")`, add:

~~~cpp
ImGui::SeparatorText("NESYS");
auto& nesys_server_ip = g_config.nesys().server_ip();
if (ImGui::InputText("NESYS Server IPv4", &nesys_server_ip)) {
    g_config_dirty = true;
}
const bool nesys_server_ip_valid =
    gc::nesys_service::IsDottedDecimalIpv4(nesys_server_ip);
if (!nesys_server_ip_valid) {
    ImGui::TextColored(
        ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
        "Enter a dotted-decimal IPv4 address without a port.");
}
~~~

Wrap the existing save button block with:

~~~cpp
ImGui::BeginDisabled(!nesys_server_ip_valid);
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
    } catch (const std::exception& e) {
        SDL_Log(
            "Error serializing configuration to TOML: %s",
            e.what());
    }
}
ImGui::EndDisabled();
~~~

- [ ] **Step 6: Wire the parser into DLL, GUI, and tests**

In `CMakeLists.txt`, add `NesysNetworkConfig.cpp` to `SOURCES` and `GUI_SOURCES`, and make the config test target:

~~~cmake
add_executable(ConfigFeatureTests
        NesysNetworkConfig.cpp
        tests/ConfigFeatureTests.cpp
)
~~~

- [ ] **Step 7: Build and run the focused config test**

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests ConfigGUI iDmacDrv32 && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R ConfigFeatureTests'
~~~

Expected: all three targets build and `ConfigFeatureTests` passes. Existing configurations without `[nesys].server_ip` now fail exactly as required.

- [ ] **Step 8: Commit**

~~~powershell
git add -- CMakeLists.txt NesysNetworkConfig.h NesysNetworkConfig.cpp config.h config.cpp GUI_main.cpp tests/ConfigFeatureTests.cpp
git commit -m "Add required NESYS server IPv4 config"
~~~

### Task 2: Feature-Owned MinHook Transaction

**Files:**
- Create: `NesysHookTransaction.h`
- Create: `NesysHookTransaction.cpp`
- Create: `tests/NesysHookTransactionTests.cpp`
- Modify: `CMakeLists.txt:102-122,200-208`

**Interfaces:**
- Consumes: MinHook's disabled `MH_CreateHook`, per-target `MH_QueueEnableHook`, `MH_ApplyQueued`, `MH_DisableHook`, and `MH_RemoveHook` APIs.
- Produces:
  - `ApiHookRequest` and `ResolvedApiHook`
  - `bool ResolveApiHooks(std::span<const ApiHookRequest>, std::vector<ResolvedApiHook>*, HookInstallError*) noexcept`
  - `OwnedMinHookTransaction::Initialize()`, `CreateAll(...)`, `Commit()`, and `Rollback()`
  - exact failure stage, MinHook status, Win32 status, export name, and target address

- [ ] **Step 1: Write fake-backend transaction tests**

Create `tests/NesysHookTransactionTests.cpp`:

~~~cpp
#include "NesysHookTransaction.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

struct FakeState {
    int fail_create_call{-1};
    int fail_queue_call{-1};
    bool fail_apply{false};
    int create_calls{0};
    int queue_calls{0};
    int apply_calls{0};
    std::vector<LPVOID> created;
    std::vector<LPVOID> queued;
    std::vector<LPVOID> disabled;
    std::vector<LPVOID> removed;
};

FakeState* g_fake = nullptr;

MH_STATUS WINAPI fake_initialize() {
    return MH_OK;
}

MH_STATUS WINAPI fake_create(LPVOID target, LPVOID, LPVOID*) {
    const int call = g_fake->create_calls++;
    if (call == g_fake->fail_create_call) {
        return MH_ERROR_MEMORY_ALLOC;
    }
    g_fake->created.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_queue(LPVOID target) {
    const int call = g_fake->queue_calls++;
    if (call == g_fake->fail_queue_call) {
        return MH_ERROR_MEMORY_PROTECT;
    }
    g_fake->queued.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_apply() {
    ++g_fake->apply_calls;
    return g_fake->fail_apply ? MH_ERROR_MEMORY_PROTECT : MH_OK;
}

MH_STATUS WINAPI fake_disable(LPVOID target) {
    g_fake->disabled.push_back(target);
    return MH_OK;
}

MH_STATUS WINAPI fake_remove(LPVOID target) {
    g_fake->removed.push_back(target);
    return MH_OK;
}

gc::nesys_service::MinHookApi fake_api() {
    return {
        fake_initialize,
        fake_create,
        fake_queue,
        fake_apply,
        fake_disable,
        fake_remove,
    };
}

gc::nesys_service::ResolvedApiHook hook(LPVOID target) {
    return {
        {L"fake.dll", "Fake", reinterpret_cast<LPVOID>(0x2000), nullptr},
        target,
    };
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool contains(const std::vector<LPVOID>& values, LPVOID target) {
    return std::find(values.begin(), values.end(), target) != values.end();
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;
    const auto first = reinterpret_cast<LPVOID>(0x1000);
    const auto second = reinterpret_cast<LPVOID>(0x1100);
    const auto unrelated = reinterpret_cast<LPVOID>(0x9999);
    const std::vector<ResolvedApiHook> hooks{hook(first), hook(second)};

    FakeState success{};
    g_fake = &success;
    OwnedMinHookTransaction committed(fake_api());
    failures += expect(committed.Initialize(), "initialize success");
    failures += expect(committed.CreateAll(hooks), "create all");
    failures += expect(committed.Commit(), "commit all");
    failures += expect(
        success.created == std::vector<LPVOID>{first, second},
        "create exact targets");
    failures += expect(
        success.queued == std::vector<LPVOID>{first, second},
        "queue exact targets");
    failures += expect(success.apply_calls == 1, "single queued apply");

    FakeState create_failure{};
    create_failure.fail_create_call = 1;
    g_fake = &create_failure;
    OwnedMinHookTransaction create_rollback(fake_api());
    failures += expect(create_rollback.Initialize(), "create failure init");
    failures += expect(!create_rollback.CreateAll(hooks), "create failure");
    failures += expect(
        contains(create_failure.removed, first),
        "created target removed");
    failures += expect(
        !contains(create_failure.removed, second),
        "failed target not removed");

    FakeState queue_failure{};
    queue_failure.fail_queue_call = 1;
    g_fake = &queue_failure;
    OwnedMinHookTransaction queue_rollback(fake_api());
    failures += expect(queue_rollback.Initialize(), "queue failure init");
    failures += expect(queue_rollback.CreateAll(hooks), "queue failure create");
    failures += expect(!queue_rollback.Commit(), "queue failure commit");
    failures += expect(
        contains(queue_failure.removed, first) &&
            contains(queue_failure.removed, second),
        "queue failure removes every owned target");

    FakeState apply_failure{};
    apply_failure.fail_apply = true;
    g_fake = &apply_failure;
    OwnedMinHookTransaction apply_rollback(fake_api());
    failures += expect(apply_rollback.Initialize(), "apply failure init");
    failures += expect(apply_rollback.CreateAll(hooks), "apply failure create");
    failures += expect(!apply_rollback.Commit(), "apply failure");
    failures += expect(
        contains(apply_failure.disabled, first) &&
            contains(apply_failure.disabled, second),
        "apply failure disables every owned target");

    failures += expect(
        !contains(create_failure.removed, unrelated) &&
            !contains(queue_failure.removed, unrelated) &&
            !contains(apply_failure.disabled, unrelated),
        "rollback never touches unrelated hook");

    return failures == 0 ? 0 : 1;
}
~~~

- [ ] **Step 2: Register the test and verify the red state**

Add to `CMakeLists.txt`:

~~~cmake
add_executable(NesysHookTransactionTests
        NesysHookTransaction.cpp
        tests/NesysHookTransactionTests.cpp
)
target_include_directories(NesysHookTransactionTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
)
target_link_libraries(NesysHookTransactionTests PRIVATE minhook)
add_test(NAME NesysHookTransactionTests COMMAND NesysHookTransactionTests)
~~~

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysHookTransactionTests'
~~~

Expected: compilation fails because `NesysHookTransaction.h` and its interfaces do not exist.

- [ ] **Step 3: Define exported hook requests and failure data**

Create `NesysHookTransaction.h`:

~~~cpp
#pragma once

#include <Windows.h>
#include <MinHook.h>

#include <span>
#include <vector>

namespace gc::nesys_service {

struct ApiHookRequest {
    LPCWSTR module_name;
    LPCSTR export_name;
    LPVOID detour;
    LPVOID* original;
};

struct ResolvedApiHook {
    ApiHookRequest request;
    LPVOID target;
};

enum class HookInstallStage {
    None,
    ResolveModule,
    ResolveExport,
    Initialize,
    Create,
    QueueEnable,
    ApplyQueued,
};

struct HookInstallError {
    HookInstallStage stage{HookInstallStage::None};
    MH_STATUS minhook_status{MH_OK};
    DWORD win32_error{ERROR_SUCCESS};
    LPCSTR export_name{nullptr};
    LPVOID target{nullptr};
};

bool ResolveApiHooks(
    std::span<const ApiHookRequest> requests,
    std::vector<ResolvedApiHook>* resolved,
    HookInstallError* error) noexcept;

struct MinHookApi {
    decltype(&MH_Initialize) initialize;
    decltype(&MH_CreateHook) create_hook;
    decltype(&MH_QueueEnableHook) queue_enable_hook;
    decltype(&MH_ApplyQueued) apply_queued;
    decltype(&MH_DisableHook) disable_hook;
    decltype(&MH_RemoveHook) remove_hook;
};

MinHookApi ProductionMinHookApi() noexcept;

class OwnedMinHookTransaction {
public:
    explicit OwnedMinHookTransaction(MinHookApi api) noexcept;
    ~OwnedMinHookTransaction();

    OwnedMinHookTransaction(const OwnedMinHookTransaction&) = delete;
    OwnedMinHookTransaction& operator=(const OwnedMinHookTransaction&) = delete;

    bool Initialize() noexcept;
    bool CreateAll(std::span<const ResolvedApiHook> hooks) noexcept;
    bool Commit() noexcept;
    void Rollback() noexcept;

    const HookInstallError& error() const noexcept { return error_; }
    bool committed() const noexcept { return committed_; }

private:
    MinHookApi api_;
    std::vector<LPVOID> owned_targets_;
    HookInstallError error_{};
    bool committed_{false};
};

} // namespace gc::nesys_service
~~~

- [ ] **Step 4: Implement resolve-first and exact-target rollback**

Create `NesysHookTransaction.cpp`:

~~~cpp
#include "NesysHookTransaction.h"

#include <new>

namespace gc::nesys_service {
namespace {

void set_error(
    HookInstallError* error,
    HookInstallStage stage,
    MH_STATUS status,
    DWORD win32_error,
    LPCSTR export_name,
    LPVOID target) noexcept {
    if (error != nullptr) {
        *error = {stage, status, win32_error, export_name, target};
    }
}

} // namespace

bool ResolveApiHooks(
    std::span<const ApiHookRequest> requests,
    std::vector<ResolvedApiHook>* resolved,
    HookInstallError* error) noexcept {
    if (resolved == nullptr) {
        set_error(
            error,
            HookInstallStage::ResolveExport,
            MH_UNKNOWN,
            ERROR_INVALID_PARAMETER,
            nullptr,
            nullptr);
        return false;
    }

    try {
        resolved->clear();
        resolved->reserve(requests.size());
        for (const auto& request : requests) {
            HMODULE module = GetModuleHandleW(request.module_name);
            if (module == nullptr) {
                set_error(
                    error,
                    HookInstallStage::ResolveModule,
                    MH_ERROR_MODULE_NOT_FOUND,
                    ERROR_MOD_NOT_FOUND,
                    request.export_name,
                    nullptr);
                resolved->clear();
                return false;
            }

            const auto target = reinterpret_cast<LPVOID>(
                GetProcAddress(module, request.export_name));
            if (target == nullptr) {
                set_error(
                    error,
                    HookInstallStage::ResolveExport,
                    MH_ERROR_FUNCTION_NOT_FOUND,
                    ERROR_PROC_NOT_FOUND,
                    request.export_name,
                    nullptr);
                resolved->clear();
                return false;
            }
            resolved->push_back({request, target});
        }
    } catch (const std::bad_alloc&) {
        set_error(
            error,
            HookInstallStage::ResolveExport,
            MH_ERROR_MEMORY_ALLOC,
            ERROR_NOT_ENOUGH_MEMORY,
            nullptr,
            nullptr);
        resolved->clear();
        return false;
    }

    if (error != nullptr) {
        *error = {};
    }
    return true;
}

MinHookApi ProductionMinHookApi() noexcept {
    return {
        MH_Initialize,
        MH_CreateHook,
        MH_QueueEnableHook,
        MH_ApplyQueued,
        MH_DisableHook,
        MH_RemoveHook,
    };
}

OwnedMinHookTransaction::OwnedMinHookTransaction(
    MinHookApi api) noexcept
    : api_(api) {
}

OwnedMinHookTransaction::~OwnedMinHookTransaction() {
    if (!committed_) {
        Rollback();
    }
}

bool OwnedMinHookTransaction::Initialize() noexcept {
    const auto status = api_.initialize();
    if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
        return true;
    }
    error_ = {
        HookInstallStage::Initialize,
        status,
        ERROR_SUCCESS,
        nullptr,
        nullptr,
    };
    return false;
}

bool OwnedMinHookTransaction::CreateAll(
    std::span<const ResolvedApiHook> hooks) noexcept {
    try {
        owned_targets_.reserve(hooks.size());
    } catch (const std::bad_alloc&) {
        error_ = {
            HookInstallStage::Create,
            MH_ERROR_MEMORY_ALLOC,
            ERROR_NOT_ENOUGH_MEMORY,
            nullptr,
            nullptr,
        };
        return false;
    }

    for (const auto& hook : hooks) {
        const auto status = api_.create_hook(
            hook.target,
            hook.request.detour,
            hook.request.original);
        if (status != MH_OK) {
            error_ = {
                HookInstallStage::Create,
                status,
                ERROR_SUCCESS,
                hook.request.export_name,
                hook.target,
            };
            Rollback();
            return false;
        }
        owned_targets_.push_back(hook.target);
    }
    return true;
}

bool OwnedMinHookTransaction::Commit() noexcept {
    for (const auto target : owned_targets_) {
        const auto status = api_.queue_enable_hook(target);
        if (status != MH_OK) {
            error_ = {
                HookInstallStage::QueueEnable,
                status,
                ERROR_SUCCESS,
                nullptr,
                target,
            };
            Rollback();
            return false;
        }
    }

    const auto status = api_.apply_queued();
    if (status != MH_OK) {
        error_ = {
            HookInstallStage::ApplyQueued,
            status,
            ERROR_SUCCESS,
            nullptr,
            nullptr,
        };
        Rollback();
        return false;
    }

    committed_ = true;
    return true;
}

void OwnedMinHookTransaction::Rollback() noexcept {
    for (auto iterator = owned_targets_.rbegin();
         iterator != owned_targets_.rend();
         ++iterator) {
        api_.disable_hook(*iterator);
        api_.remove_hook(*iterator);
    }
    owned_targets_.clear();
    committed_ = false;
}

} // namespace gc::nesys_service
~~~

- [ ] **Step 5: Build and run the transaction test**

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysHookTransactionTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R NesysHookTransactionTests'
~~~

Expected: `NesysHookTransactionTests` passes. The fake unrelated target never appears in disable/remove records.

- [ ] **Step 6: Commit**

~~~powershell
git add -- CMakeLists.txt NesysHookTransaction.h NesysHookTransaction.cpp tests/NesysHookTransactionTests.cpp
git commit -m "Add feature-owned NESYS hook transactions"
~~~

### Task 3: Native Synthetic Adapter API Contracts

**Files:**
- Create: `SyntheticNetworkAdapter.h`
- Create: `SyntheticNetworkAdapter.cpp`
- Create: `tests/SyntheticNetworkAdapterTests.cpp`
- Modify: `CMakeLists.txt:102-122,200-208`

**Interfaces:**
- Consumes: `ProcessRole` and `ApiHookRequest` from Tasks 1-2.
- Produces:
  - exact synthetic adapter constants in `SyntheticNetworkAdapter.h`
  - detours `SyntheticGetAdaptersInfo`, `SyntheticGetIfTable`, `SyntheticGetInterfaceInfo`, `SyntheticGetNetworkParams`, `SyntheticNotifyAddrChange`, `SyntheticCancelIPChangeNotify`, `SuppressIpReleaseAddress`, `SuppressIpRenewAddress`, and `SuppressFlushIpNetTable`
  - `std::span<const char* const> SyntheticAdapterHookExports(ProcessRole) noexcept`
  - `void AppendSyntheticAdapterHookRequests(ProcessRole, std::vector<ApiHookRequest>&)`

- [ ] **Step 1: Write failing native-buffer and inventory tests**

Create `tests/SyntheticNetworkAdapterTests.cpp`. Use byte vectors initialized to `0xCD` and verify each API in three calls: null buffer/size probe, one-byte-short buffer, and exact-size success. The core assertions are:

~~~cpp
#include "SyntheticNetworkAdapter.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool tail_is_unchanged(
    const std::vector<std::uint8_t>& buffer,
    std::size_t used) {
    return std::all_of(
        buffer.begin() + static_cast<std::ptrdiff_t>(used),
        buffer.end(),
        [](std::uint8_t value) { return value == 0xCD; });
}

bool has_export(
    std::span<const char* const> exports,
    const char* name) {
    return std::any_of(
        exports.begin(),
        exports.end(),
        [name](const char* value) {
            return std::strcmp(value, name) == 0;
        });
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;

    ULONG adapter_size = 0;
    failures += expect(
        SyntheticGetAdaptersInfo(nullptr, &adapter_size) ==
            ERROR_BUFFER_OVERFLOW,
        "GetAdaptersInfo size probe");
    failures += expect(
        adapter_size == sizeof(IP_ADAPTER_INFO),
        "GetAdaptersInfo required size");

    std::vector<std::uint8_t> adapter_bytes(adapter_size + 16, 0xCD);
    ULONG short_adapter_size = adapter_size - 1;
    failures += expect(
        SyntheticGetAdaptersInfo(
            reinterpret_cast<PIP_ADAPTER_INFO>(adapter_bytes.data()),
            &short_adapter_size) == ERROR_BUFFER_OVERFLOW,
        "GetAdaptersInfo short buffer");
    failures += expect(
        std::all_of(
            adapter_bytes.begin(),
            adapter_bytes.end(),
            [](std::uint8_t value) { return value == 0xCD; }),
        "GetAdaptersInfo short buffer untouched");

    ULONG exact_adapter_size = adapter_size;
    failures += expect(
        SyntheticGetAdaptersInfo(
            reinterpret_cast<PIP_ADAPTER_INFO>(adapter_bytes.data()),
            &exact_adapter_size) == NO_ERROR,
        "GetAdaptersInfo success");
    const auto* adapter =
        reinterpret_cast<const IP_ADAPTER_INFO*>(adapter_bytes.data());
    failures += expect(adapter->Next == nullptr, "single adapter");
    failures += expect(
        std::strcmp(adapter->AdapterName, kSyntheticAdapterName) == 0,
        "adapter name");
    failures += expect(
        std::strcmp(adapter->Description, kSyntheticAdapterDescription) == 0,
        "adapter description");
    failures += expect(
        adapter->AddressLength == kSyntheticMac.size() &&
            std::memcmp(
                adapter->Address,
                kSyntheticMac.data(),
                kSyntheticMac.size()) == 0,
        "adapter MAC");
    failures += expect(
        adapter->Index == kSyntheticInterfaceIndex,
        "adapter index");
    failures += expect(
        adapter->Type == MIB_IF_TYPE_ETHERNET,
        "adapter Ethernet type");
    failures += expect(adapter->DhcpEnabled == TRUE, "adapter DHCP enabled");
    failures += expect(
        adapter->CurrentIpAddress == &adapter->IpAddressList,
        "current IPv4 pointer");
    failures += expect(
        std::strcmp(
            adapter->IpAddressList.IpAddress.String,
            kSyntheticIpv4) == 0 &&
            std::strcmp(
                adapter->IpAddressList.IpMask.String,
                kSyntheticMask) == 0,
        "adapter IPv4 and mask");
    failures += expect(
        std::strcmp(
            adapter->GatewayList.IpAddress.String,
            kSyntheticGateway) == 0,
        "adapter gateway");
    failures += expect(
        std::strcmp(
            adapter->DhcpServer.IpAddress.String,
            kSyntheticDhcpServer) == 0,
        "adapter DHCP server");
    failures += expect(
        adapter->IpAddressList.Next == nullptr &&
            adapter->GatewayList.Next == nullptr &&
            adapter->DhcpServer.Next == nullptr,
        "adapter lists terminate");
    failures += expect(
        adapter->HaveWins == FALSE &&
            adapter->PrimaryWinsServer.Next == nullptr &&
            adapter->SecondaryWinsServer.Next == nullptr,
        "WINS disabled");
    failures += expect(
        adapter->LeaseObtained == 0 &&
            adapter->LeaseExpires == static_cast<time_t>(0x7FFFFFFF),
        "lease range");
    failures += expect(
        tail_is_unchanged(adapter_bytes, adapter_size),
        "GetAdaptersInfo no overrun");

    ULONG if_table_size = 0;
    failures += expect(
        SyntheticGetIfTable(nullptr, &if_table_size, TRUE) ==
            ERROR_INSUFFICIENT_BUFFER,
        "GetIfTable size probe");
    failures += expect(
        if_table_size == SIZEOF_IFTABLE(1),
        "GetIfTable required size");
    std::vector<std::uint8_t> if_table_bytes(if_table_size + 16, 0xCD);
    ULONG exact_if_table_size = if_table_size;
    failures += expect(
        SyntheticGetIfTable(
            reinterpret_cast<PMIB_IFTABLE>(if_table_bytes.data()),
            &exact_if_table_size,
            TRUE) == NO_ERROR,
        "GetIfTable success");
    const auto* if_table =
        reinterpret_cast<const MIB_IFTABLE*>(if_table_bytes.data());
    const auto& row = if_table->table[0];
    failures += expect(if_table->dwNumEntries == 1, "one interface row");
    failures += expect(
        row.dwIndex == kSyntheticInterfaceIndex,
        "row index");
    failures += expect(
        row.dwType == MIB_IF_TYPE_ETHERNET &&
            row.dwAdminStatus == MIB_IF_ADMIN_STATUS_UP &&
            row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL,
        "row Ethernet and up");
    failures += expect(
        row.dwMtu == kSyntheticMtu &&
            row.dwSpeed == kSyntheticLinkSpeed,
        "row MTU and speed");
    failures += expect(
        row.dwPhysAddrLen == kSyntheticMac.size() &&
            std::memcmp(
                row.bPhysAddr,
                kSyntheticMac.data(),
                kSyntheticMac.size()) == 0,
        "row MAC");
    failures += expect(
        tail_is_unchanged(if_table_bytes, if_table_size),
        "GetIfTable no overrun");

    ULONG interface_size = 0;
    failures += expect(
        SyntheticGetInterfaceInfo(nullptr, &interface_size) ==
            ERROR_INSUFFICIENT_BUFFER,
        "GetInterfaceInfo size probe");
    std::vector<std::uint8_t> interface_bytes(interface_size + 16, 0xCD);
    ULONG exact_interface_size = interface_size;
    failures += expect(
        SyntheticGetInterfaceInfo(
            reinterpret_cast<PIP_INTERFACE_INFO>(interface_bytes.data()),
            &exact_interface_size) == NO_ERROR,
        "GetInterfaceInfo success");
    const auto* interface_info =
        reinterpret_cast<const IP_INTERFACE_INFO*>(interface_bytes.data());
    failures += expect(interface_info->NumAdapters == 1, "one interface map");
    failures += expect(
        interface_info->Adapter[0].Index == kSyntheticInterfaceIndex &&
            std::wcscmp(
                interface_info->Adapter[0].Name,
                kSyntheticAdapterNameWide) == 0,
        "interface map identity");

    ULONG network_size = 0;
    failures += expect(
        SyntheticGetNetworkParams(nullptr, &network_size) ==
            ERROR_BUFFER_OVERFLOW,
        "GetNetworkParams size probe");
    std::vector<std::uint8_t> network_bytes(network_size + 16, 0xCD);
    ULONG exact_network_size = network_size;
    failures += expect(
        SyntheticGetNetworkParams(
            reinterpret_cast<PFIXED_INFO>(network_bytes.data()),
            &exact_network_size) == NO_ERROR,
        "GetNetworkParams success");
    const auto* network =
        reinterpret_cast<const FIXED_INFO*>(network_bytes.data());
    failures += expect(
        std::strcmp(network->HostName, "GCLoader") == 0 &&
            network->DomainName[0] == '\0' &&
            network->ScopeId[0] == '\0',
        "network names");
    failures += expect(
        network->NodeType == BROADCAST_NODETYPE &&
            network->EnableRouting == 0 &&
            network->EnableProxy == 0 &&
            network->EnableDns == 1,
        "network flags");
    failures += expect(
        network->CurrentDnsServer == &network->DnsServerList &&
            network->DnsServerList.Next == nullptr &&
            std::strcmp(
                network->DnsServerList.IpAddress.String,
                kSyntheticDnsServer) == 0,
        "network DNS");

    HANDLE notification_handle = reinterpret_cast<HANDLE>(0x1234);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        SyntheticNotifyAddrChange(
            &notification_handle,
            &overlapped) == ERROR_IO_PENDING,
        "NotifyAddrChange pending");
    failures += expect(
        notification_handle == nullptr &&
            GetLastError() == ERROR_IO_PENDING &&
            WaitForSingleObject(overlapped.hEvent, 0) == WAIT_TIMEOUT,
        "NotifyAddrChange null handle and unsignaled event");
    failures += expect(
        SyntheticCancelIPChangeNotify(&overlapped) == TRUE,
        "CancelIPChangeNotify success");
    CloseHandle(overlapped.hEvent);

    IP_ADAPTER_INDEX_MAP map{};
    map.Index = kSyntheticInterfaceIndex;
    failures += expect(
        SuppressIpReleaseAddress(&map) == NO_ERROR &&
            SuppressIpRenewAddress(&map) == NO_ERROR &&
            SuppressFlushIpNetTable(map.Index) == NO_ERROR,
        "mutation suppression success");

    const auto game_exports =
        SyntheticAdapterHookExports(ProcessRole::Game);
    const auto service_exports =
        SyntheticAdapterHookExports(ProcessRole::Service);
    failures += expect(
        game_exports.size() == 3 &&
            has_export(game_exports, "GetAdaptersInfo") &&
            has_export(game_exports, "NotifyAddrChange") &&
            has_export(game_exports, "CancelIPChangeNotify"),
        "game adapter hook inventory");
    failures += expect(
        service_exports.size() == 7 &&
            has_export(service_exports, "GetAdaptersInfo") &&
            has_export(service_exports, "GetIfTable") &&
            has_export(service_exports, "GetInterfaceInfo") &&
            has_export(service_exports, "GetNetworkParams") &&
            has_export(service_exports, "IpReleaseAddress") &&
            has_export(service_exports, "IpRenewAddress") &&
            has_export(service_exports, "FlushIpNetTable"),
        "service adapter hook inventory");
    failures += expect(
        !has_export(service_exports, "GetIpNetTable"),
        "GetIpNetTable remains real");

    return failures == 0 ? 0 : 1;
}
~~~

Insert these one-byte-short assertions before the corresponding successful calls:

~~~cpp
ULONG short_if_table_size = if_table_size - 1;
failures += expect(
    SyntheticGetIfTable(
        reinterpret_cast<PMIB_IFTABLE>(if_table_bytes.data()),
        &short_if_table_size,
        TRUE) == ERROR_INSUFFICIENT_BUFFER &&
        short_if_table_size == if_table_size,
    "GetIfTable short buffer");
failures += expect(
    std::all_of(
        if_table_bytes.begin(),
        if_table_bytes.end(),
        [](std::uint8_t value) { return value == 0xCD; }),
    "GetIfTable short buffer untouched");

ULONG short_interface_size = interface_size - 1;
failures += expect(
    SyntheticGetInterfaceInfo(
        reinterpret_cast<PIP_INTERFACE_INFO>(interface_bytes.data()),
        &short_interface_size) == ERROR_INSUFFICIENT_BUFFER &&
        short_interface_size == interface_size,
    "GetInterfaceInfo short buffer");
failures += expect(
    std::all_of(
        interface_bytes.begin(),
        interface_bytes.end(),
        [](std::uint8_t value) { return value == 0xCD; }),
    "GetInterfaceInfo short buffer untouched");

ULONG short_network_size = network_size - 1;
failures += expect(
    SyntheticGetNetworkParams(
        reinterpret_cast<PFIXED_INFO>(network_bytes.data()),
        &short_network_size) == ERROR_BUFFER_OVERFLOW &&
        short_network_size == network_size,
    "GetNetworkParams short buffer");
failures += expect(
    std::all_of(
        network_bytes.begin(),
        network_bytes.end(),
        [](std::uint8_t value) { return value == 0xCD; }),
    "GetNetworkParams short buffer untouched");
~~~

Add `#include <cwchar>` with the test includes. Add these assertions after the corresponding successful calls:

~~~cpp
failures += expect(
    std::wcscmp(row.wszName, kSyntheticAdapterNameWide) == 0,
    "row adapter name");
failures += expect(
    tail_is_unchanged(interface_bytes, interface_size),
    "GetInterfaceInfo no overrun");
failures += expect(
    tail_is_unchanged(network_bytes, network_size),
    "GetNetworkParams no overrun");
~~~

Together with the existing name/index assertions, these make cross-API identity and all four no-overrun contracts explicit.

- [ ] **Step 2: Register the focused adapter test and verify the red state**

Add:

~~~cmake
add_executable(SyntheticNetworkAdapterTests
        SyntheticNetworkAdapter.cpp
        tests/SyntheticNetworkAdapterTests.cpp
)
target_include_directories(SyntheticNetworkAdapterTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
add_test(NAME SyntheticNetworkAdapterTests COMMAND SyntheticNetworkAdapterTests)
~~~

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SyntheticNetworkAdapterTests'
~~~

Expected: compilation fails because `SyntheticNetworkAdapter.h` does not exist.

- [ ] **Step 3: Define the immutable profile and detour interface**

Create `SyntheticNetworkAdapter.h`:

~~~cpp
#pragma once

#include <WinSock2.h>
#include <Iphlpapi.h>

#include "NesysHookTransaction.h"
#include "NesysServiceProcess.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gc::nesys_service {

inline constexpr char kSyntheticAdapterName[] = "GCLoaderNesys0";
inline constexpr wchar_t kSyntheticAdapterNameWide[] = L"GCLoaderNesys0";
inline constexpr char kSyntheticAdapterDescription[] =
    "GCLoader NESYS IPv4 Adapter";
inline constexpr std::array<std::uint8_t, 6> kSyntheticMac{
    0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01,
};
inline constexpr DWORD kSyntheticInterfaceIndex = 0x0BADC0DE;
inline constexpr char kSyntheticIpv4[] = "192.0.2.2";
inline constexpr char kSyntheticMask[] = "255.255.255.0";
inline constexpr char kSyntheticGateway[] = "192.0.2.1";
inline constexpr char kSyntheticDhcpServer[] = "192.0.2.1";
inline constexpr char kSyntheticDnsServer[] = "192.0.2.1";
inline constexpr DWORD kSyntheticMtu = 1500;
inline constexpr DWORD kSyntheticLinkSpeed = 1'000'000'000;

ULONG WINAPI SyntheticGetAdaptersInfo(
    PIP_ADAPTER_INFO adapter_info,
    PULONG size_pointer) noexcept;
DWORD WINAPI SyntheticGetIfTable(
    PMIB_IFTABLE table,
    PULONG size_pointer,
    BOOL order) noexcept;
DWORD WINAPI SyntheticGetInterfaceInfo(
    PIP_INTERFACE_INFO interface_info,
    PULONG size_pointer) noexcept;
DWORD WINAPI SyntheticGetNetworkParams(
    PFIXED_INFO fixed_info,
    PULONG size_pointer) noexcept;
DWORD WINAPI SyntheticNotifyAddrChange(
    PHANDLE handle,
    LPOVERLAPPED overlapped) noexcept;
BOOL WINAPI SyntheticCancelIPChangeNotify(
    LPOVERLAPPED overlapped) noexcept;
DWORD WINAPI SuppressIpReleaseAddress(
    PIP_ADAPTER_INDEX_MAP adapter_info) noexcept;
DWORD WINAPI SuppressIpRenewAddress(
    PIP_ADAPTER_INDEX_MAP adapter_info) noexcept;
DWORD WINAPI SuppressFlushIpNetTable(DWORD index) noexcept;

std::span<const char* const> SyntheticAdapterHookExports(
    ProcessRole role) noexcept;
void AppendSyntheticAdapterHookRequests(
    ProcessRole role,
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service
~~~

- [ ] **Step 4: Implement exact native structures and role-owned hook lists**

Create `SyntheticNetworkAdapter.cpp`:

~~~cpp
#include "SyntheticNetworkAdapter.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <string_view>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

std::atomic_flag g_adapter_query_logged = ATOMIC_FLAG_INIT;
std::atomic_flag g_notification_logged = ATOMIC_FLAG_INIT;
std::atomic_flag g_mutation_logged = ATOMIC_FLAG_INIT;

void log_first(std::atomic_flag& flag, const char* family) noexcept {
    if (flag.test_and_set(std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO << "SyntheticNetworkAdapter: first " << family;
    } catch (...) {
    }
}

template <std::size_t Size>
void copy_ascii(char (&destination)[Size], std::string_view source) noexcept {
    const auto count = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

template <std::size_t Size>
void copy_wide(
    wchar_t (&destination)[Size],
    std::wstring_view source) noexcept {
    const auto count = std::min(source.size(), Size - 1);
    std::wmemcpy(destination, source.data(), count);
    destination[count] = L'\0';
}

DWORD prepare_buffer(
    void* output,
    PULONG size_pointer,
    ULONG required,
    DWORD too_small) noexcept {
    if (size_pointer == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    const ULONG supplied = *size_pointer;
    *size_pointer = required;
    if (output == nullptr || supplied < required) {
        return too_small;
    }

    std::memset(output, 0, required);
    return NO_ERROR;
}

void fill_address(
    IP_ADDR_STRING& destination,
    std::string_view address,
    std::string_view mask = {}) noexcept {
    destination.Next = nullptr;
    copy_ascii(destination.IpAddress.String, address);
    if (!mask.empty()) {
        copy_ascii(destination.IpMask.String, mask);
    }
    destination.Context = 0;
}

constexpr std::array<const char*, 3> kGameExports{
    "GetAdaptersInfo",
    "NotifyAddrChange",
    "CancelIPChangeNotify",
};

constexpr std::array<const char*, 7> kServiceExports{
    "GetAdaptersInfo",
    "GetIfTable",
    "GetInterfaceInfo",
    "GetNetworkParams",
    "IpReleaseAddress",
    "IpRenewAddress",
    "FlushIpNetTable",
};

} // namespace

ULONG WINAPI SyntheticGetAdaptersInfo(
    PIP_ADAPTER_INFO adapter_info,
    PULONG size_pointer) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    constexpr ULONG required = sizeof(IP_ADAPTER_INFO);
    const DWORD status = prepare_buffer(
        adapter_info,
        size_pointer,
        required,
        ERROR_BUFFER_OVERFLOW);
    if (status != NO_ERROR) {
        return status;
    }

    adapter_info->Next = nullptr;
    adapter_info->ComboIndex = 0;
    copy_ascii(adapter_info->AdapterName, kSyntheticAdapterName);
    copy_ascii(
        adapter_info->Description,
        kSyntheticAdapterDescription);
    adapter_info->AddressLength =
        static_cast<UINT>(kSyntheticMac.size());
    std::memcpy(
        adapter_info->Address,
        kSyntheticMac.data(),
        kSyntheticMac.size());
    adapter_info->Index = kSyntheticInterfaceIndex;
    adapter_info->Type = MIB_IF_TYPE_ETHERNET;
    adapter_info->DhcpEnabled = TRUE;
    adapter_info->CurrentIpAddress = &adapter_info->IpAddressList;
    fill_address(
        adapter_info->IpAddressList,
        kSyntheticIpv4,
        kSyntheticMask);
    fill_address(adapter_info->GatewayList, kSyntheticGateway);
    fill_address(adapter_info->DhcpServer, kSyntheticDhcpServer);
    adapter_info->HaveWins = FALSE;
    adapter_info->LeaseObtained = 0;
    adapter_info->LeaseExpires = static_cast<time_t>(0x7FFFFFFF);
    return NO_ERROR;
}

DWORD WINAPI SyntheticGetIfTable(
    PMIB_IFTABLE table,
    PULONG size_pointer,
    BOOL) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    const ULONG required = SIZEOF_IFTABLE(1);
    const DWORD status = prepare_buffer(
        table,
        size_pointer,
        required,
        ERROR_INSUFFICIENT_BUFFER);
    if (status != NO_ERROR) {
        return status;
    }

    table->dwNumEntries = 1;
    auto& row = table->table[0];
    copy_wide(row.wszName, kSyntheticAdapterNameWide);
    row.dwIndex = kSyntheticInterfaceIndex;
    row.dwType = MIB_IF_TYPE_ETHERNET;
    row.dwMtu = kSyntheticMtu;
    row.dwSpeed = kSyntheticLinkSpeed;
    row.dwPhysAddrLen = static_cast<DWORD>(kSyntheticMac.size());
    std::memcpy(row.bPhysAddr, kSyntheticMac.data(), kSyntheticMac.size());
    row.dwAdminStatus = MIB_IF_ADMIN_STATUS_UP;
    row.dwOperStatus = IF_OPER_STATUS_OPERATIONAL;
    row.dwDescrLen =
        static_cast<DWORD>(std::size(kSyntheticAdapterDescription) - 1);
    std::memcpy(
        row.bDescr,
        kSyntheticAdapterDescription,
        row.dwDescrLen);
    return NO_ERROR;
}

DWORD WINAPI SyntheticGetInterfaceInfo(
    PIP_INTERFACE_INFO interface_info,
    PULONG size_pointer) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    constexpr ULONG required = sizeof(IP_INTERFACE_INFO);
    const DWORD status = prepare_buffer(
        interface_info,
        size_pointer,
        required,
        ERROR_INSUFFICIENT_BUFFER);
    if (status != NO_ERROR) {
        return status;
    }

    interface_info->NumAdapters = 1;
    interface_info->Adapter[0].Index = kSyntheticInterfaceIndex;
    copy_wide(
        interface_info->Adapter[0].Name,
        kSyntheticAdapterNameWide);
    return NO_ERROR;
}

DWORD WINAPI SyntheticGetNetworkParams(
    PFIXED_INFO fixed_info,
    PULONG size_pointer) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    constexpr ULONG required = sizeof(FIXED_INFO);
    const DWORD status = prepare_buffer(
        fixed_info,
        size_pointer,
        required,
        ERROR_BUFFER_OVERFLOW);
    if (status != NO_ERROR) {
        return status;
    }

    copy_ascii(fixed_info->HostName, "GCLoader");
    fixed_info->CurrentDnsServer = &fixed_info->DnsServerList;
    fill_address(fixed_info->DnsServerList, kSyntheticDnsServer);
    fixed_info->NodeType = BROADCAST_NODETYPE;
    fixed_info->EnableRouting = 0;
    fixed_info->EnableProxy = 0;
    fixed_info->EnableDns = 1;
    return NO_ERROR;
}

DWORD WINAPI SyntheticNotifyAddrChange(
    PHANDLE handle,
    LPOVERLAPPED) noexcept {
    log_first(g_notification_logged, "adapter-notification family");
    if (handle != nullptr) {
        *handle = nullptr;
    }
    SetLastError(ERROR_IO_PENDING);
    return ERROR_IO_PENDING;
}

BOOL WINAPI SyntheticCancelIPChangeNotify(LPOVERLAPPED) noexcept {
    log_first(g_notification_logged, "adapter-notification family");
    return TRUE;
}

DWORD WINAPI SuppressIpReleaseAddress(PIP_ADAPTER_INDEX_MAP) noexcept {
    log_first(g_mutation_logged, "mutation-suppression family");
    return NO_ERROR;
}

DWORD WINAPI SuppressIpRenewAddress(PIP_ADAPTER_INDEX_MAP) noexcept {
    log_first(g_mutation_logged, "mutation-suppression family");
    return NO_ERROR;
}

DWORD WINAPI SuppressFlushIpNetTable(DWORD) noexcept {
    log_first(g_mutation_logged, "mutation-suppression family");
    return NO_ERROR;
}

std::span<const char* const> SyntheticAdapterHookExports(
    ProcessRole role) noexcept {
    return role == ProcessRole::Game
        ? std::span<const char* const>{
              kGameExports.data(),
              kGameExports.size()}
        : std::span<const char* const>{
              kServiceExports.data(),
              kServiceExports.size()};
}

void AppendSyntheticAdapterHookRequests(
    ProcessRole role,
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"iphlpapi.dll",
        "GetAdaptersInfo",
        reinterpret_cast<LPVOID>(&SyntheticGetAdaptersInfo),
        nullptr,
    });

    if (role == ProcessRole::Game) {
        requests.push_back({
            L"iphlpapi.dll",
            "NotifyAddrChange",
            reinterpret_cast<LPVOID>(&SyntheticNotifyAddrChange),
            nullptr,
        });
        requests.push_back({
            L"iphlpapi.dll",
            "CancelIPChangeNotify",
            reinterpret_cast<LPVOID>(&SyntheticCancelIPChangeNotify),
            nullptr,
        });
        return;
    }

    requests.push_back({L"iphlpapi.dll", "GetIfTable",
        reinterpret_cast<LPVOID>(&SyntheticGetIfTable), nullptr});
    requests.push_back({L"iphlpapi.dll", "GetInterfaceInfo",
        reinterpret_cast<LPVOID>(&SyntheticGetInterfaceInfo), nullptr});
    requests.push_back({L"iphlpapi.dll", "GetNetworkParams",
        reinterpret_cast<LPVOID>(&SyntheticGetNetworkParams), nullptr});
    requests.push_back({L"iphlpapi.dll", "IpReleaseAddress",
        reinterpret_cast<LPVOID>(&SuppressIpReleaseAddress), nullptr});
    requests.push_back({L"iphlpapi.dll", "IpRenewAddress",
        reinterpret_cast<LPVOID>(&SuppressIpRenewAddress), nullptr});
    requests.push_back({L"iphlpapi.dll", "FlushIpNetTable",
        reinterpret_cast<LPVOID>(&SuppressFlushIpNetTable), nullptr});
}

} // namespace gc::nesys_service
~~~

This implementation intentionally has no original IP Helper trampoline pointers. The mutation wrappers therefore cannot call Windows mutation APIs, and `GetIpNetTable` cannot enter the request list.

- [ ] **Step 5: Wire the component into the DLL and run focused tests**

Add `SyntheticNetworkAdapter.cpp` to `SOURCES`. Then run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SyntheticNetworkAdapterTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R SyntheticNetworkAdapterTests'
~~~

Expected: both targets build and `SyntheticNetworkAdapterTests` passes all probe, short-buffer, exact-buffer, field, pointer, list, notification, mutation, and inventory checks.

- [ ] **Step 6: Commit**

~~~powershell
git add -- CMakeLists.txt SyntheticNetworkAdapter.h SyntheticNetworkAdapter.cpp tests/SyntheticNetworkAdapterTests.cpp
git commit -m "Virtualize NESYS adapter queries"
~~~

### Task 4: Guarded Service Ping Redirection

**Files:**
- Modify: `SyntheticNetworkAdapter.h`
- Modify: `SyntheticNetworkAdapter.cpp`
- Modify: `tests/SyntheticNetworkAdapterTests.cpp`
- Modify: `CMakeLists.txt:200-208`

**Interfaces:**
- Consumes: the service executable module base from the final lifecycle task.
- Produces:
  - `kServicePingRva = 0x8E40` and the exact 32-byte `kServicePingSignature`
  - `bool ValidateServicePingSignature(std::span<const std::uint8_t>) noexcept`
  - `bool PreflightServicePingRedirect(std::uintptr_t module_base) noexcept`
  - `bool PrepareServicePingRedirect(std::uintptr_t module_base) noexcept`
  - `bool ActivateServicePingRedirect() noexcept`
  - `void RollbackServicePingRedirect() noexcept`
  - `void ApplyServicePingTarget(std::uintptr_t* saved_eax) noexcept`

- [ ] **Step 1: Add failing fixed-RVA signature and register tests**

In `tests/SyntheticNetworkAdapterTests.cpp`, add:

~~~cpp
auto exact_ping_signature = kServicePingSignature;
failures += expect(
    ValidateServicePingSignature(exact_ping_signature),
    "exact ping signature");

auto changed_ping_signature = exact_ping_signature;
changed_ping_signature[17] ^= 0x01;
failures += expect(
    !ValidateServicePingSignature(changed_ping_signature),
    "changed target byte rejects ping hook");

std::vector<std::uint8_t> fake_image(
    kServicePingRva + kServicePingSignature.size() + 32,
    0x5A);
std::copy(
    kServicePingSignature.begin(),
    kServicePingSignature.end(),
    fake_image.begin() + kServicePingRva);
auto target_bytes = std::span<const std::uint8_t>{
    fake_image.data() + kServicePingRva,
    kServicePingSignature.size(),
};
failures += expect(
    ValidateServicePingSignature(target_bytes),
    "matching target accepts arbitrary surrounding image");
fake_image[0x20] ^= 0xFF;
failures += expect(
    ValidateServicePingSignature(target_bytes),
    "unrelated executable change ignored");

std::uintptr_t saved_eax = 0x12345678;
ApplyServicePingTarget(&saved_eax);
failures += expect(
    saved_eax == reinterpret_cast<std::uintptr_t>(kServicePingLoopback),
    "ping target becomes process-lifetime loopback");
failures += expect(
    std::strcmp(kServicePingLoopback, "127.0.0.1") == 0,
    "ping loopback text");
~~~

- [ ] **Step 2: Build the test and verify the red state**

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SyntheticNetworkAdapterTests'
~~~

Expected: compilation fails because the ping constants and helpers do not exist.

- [ ] **Step 3: Add the exact fixed-RVA public guard**

In `SyntheticNetworkAdapter.h`, add:

~~~cpp
inline constexpr std::uintptr_t kServicePingRva = 0x00008E40;
inline constexpr std::array<std::uint8_t, 32> kServicePingSignature{
    0x51, 0x53, 0x55, 0x56, 0x57, 0x50, 0x8B, 0xD9,
    0x8D, 0x6B, 0x04, 0x6A, 0x10, 0x55, 0xC7, 0x44,
    0x24, 0x1C, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x02,
    0x73, 0x02, 0x00, 0x83, 0xC4, 0x0C, 0x8D, 0x73,
};
inline constexpr char kServicePingLoopback[] = "127.0.0.1";

bool ValidateServicePingSignature(
    std::span<const std::uint8_t> bytes) noexcept;
bool PreflightServicePingRedirect(
    std::uintptr_t module_base) noexcept;
bool PrepareServicePingRedirect(
    std::uintptr_t module_base) noexcept;
bool ActivateServicePingRedirect() noexcept;
void RollbackServicePingRedirect() noexcept;
void ApplyServicePingTarget(std::uintptr_t* saved_eax) noexcept;
~~~

- [ ] **Step 4: Add guarded read, SafetyHook ownership, and EAX-only callback**

In `SyntheticNetworkAdapter.cpp`, add:

~~~cpp
#include <iomanip>
#include <utility>

#include <safetyhook.hpp>
~~~

Add these globals beside the first-hit flags:

~~~cpp
safetyhook::MidHook g_service_ping_hook{};
std::atomic_flag g_ping_logged = ATOMIC_FLAG_INIT;
~~~

Add these helpers inside the anonymous namespace:

~~~cpp
bool read_bytes_safe(
    std::uintptr_t address,
    void* output,
    std::size_t size) noexcept {
    if (address == 0 || output == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void service_ping_callback(safetyhook::Context& context) noexcept {
    ApplyServicePingTarget(&context.eax);
    if (!g_ping_logged.test_and_set(std::memory_order_relaxed)) {
        try {
            PLOG_INFO
                << "SyntheticNetworkAdapter: first ping-redirection family"
                << " target=" << kServicePingLoopback;
        } catch (...) {
        }
    }
}
~~~

Add the public implementations before `SyntheticAdapterHookExports`:

~~~cpp
bool ValidateServicePingSignature(
    std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= kServicePingSignature.size() &&
        std::equal(
            kServicePingSignature.begin(),
            kServicePingSignature.end(),
            bytes.begin());
}

void ApplyServicePingTarget(std::uintptr_t* saved_eax) noexcept {
    static_assert(sizeof(void*) == sizeof(std::uint32_t));
    if (saved_eax != nullptr) {
        *saved_eax =
            reinterpret_cast<std::uintptr_t>(kServicePingLoopback);
    }
}

bool PreflightServicePingRedirect(
    std::uintptr_t module_base) noexcept {
    std::array<std::uint8_t, kServicePingSignature.size()> actual{};
    const std::uintptr_t target = module_base + kServicePingRva;
    if (!read_bytes_safe(target, actual.data(), actual.size())) {
        try {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping signature read failed"
                << " rva=0x" << std::hex << kServicePingRva << std::dec;
        } catch (...) {
        }
        return false;
    }

    if (ValidateServicePingSignature(actual)) {
        return true;
    }

    std::size_t mismatch = 0;
    while (mismatch < actual.size() &&
           actual[mismatch] == kServicePingSignature[mismatch]) {
        ++mismatch;
    }
    try {
        PLOG_ERROR
            << "SyntheticNetworkAdapter: ping signature mismatch"
            << " rva=0x" << std::hex << kServicePingRva
            << " offset=0x" << mismatch
            << " expected=0x"
            << static_cast<unsigned>(kServicePingSignature[mismatch])
            << " actual=0x" << static_cast<unsigned>(actual[mismatch])
            << std::dec;
    } catch (...) {
    }
    return false;
}

bool PrepareServicePingRedirect(
    std::uintptr_t module_base) noexcept {
    try {
        auto created = safetyhook::MidHook::create(
            reinterpret_cast<void*>(module_base + kServicePingRva),
            service_ping_callback,
            safetyhook::MidHook::StartDisabled);
        if (!created.has_value()) {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping hook creation failed"
                << " rva=0x" << std::hex << kServicePingRva << std::dec;
            return false;
        }
        g_service_ping_hook = std::move(*created);
        return static_cast<bool>(g_service_ping_hook) &&
            !g_service_ping_hook.enabled();
    } catch (...) {
        try {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping hook threw during creation";
        } catch (...) {
        }
        return false;
    }
}

bool ActivateServicePingRedirect() noexcept {
    try {
        if (!g_service_ping_hook) {
            return false;
        }
        const auto enabled = g_service_ping_hook.enable();
        if (!enabled.has_value()) {
            g_service_ping_hook.reset();
            return false;
        }
        PLOG_INFO
            << "SyntheticNetworkAdapter: service ping hook active"
            << " rva=0x" << std::hex << kServicePingRva << std::dec;
        return true;
    } catch (...) {
        try {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping hook threw during enable";
        } catch (...) {
        }
        return false;
    }
}

void RollbackServicePingRedirect() noexcept {
    try {
        g_service_ping_hook.reset();
    } catch (...) {
    }
}
~~~

The callback does not call `skip_relocated_instruction`, alter `EIP`, fabricate a ping result, or skip cleanup. SafetyHook continues through the relocated original prologue.

- [ ] **Step 5: Link SafetyHook into the focused test and run it**

Add `safetyhook::safetyhook` to `SyntheticNetworkAdapterTests`:

~~~cmake
target_link_libraries(SyntheticNetworkAdapterTests PRIVATE
        safetyhook::safetyhook
)
~~~

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target SyntheticNetworkAdapterTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R SyntheticNetworkAdapterTests'
~~~

Expected: PASS. A target-byte change fails validation, an unrelated image-byte change does not, and the pure callback helper changes only the supplied EAX value.

- [ ] **Step 6: Commit**

~~~powershell
git add -- CMakeLists.txt SyntheticNetworkAdapter.h SyntheticNetworkAdapter.cpp tests/SyntheticNetworkAdapterTests.cpp
git commit -m "Redirect NESYS service pings to loopback"
~~~

### Task 5: Modern and Legacy Server Address Override

**Files:**
- Create: `ServerAddressOverride.h`
- Create: `ServerAddressOverride.cpp`
- Create: `tests/ServerAddressOverrideTests.cpp`
- Modify: `CMakeLists.txt:102-122,200-208`

**Interfaces:**
- Consumes: `ParseDottedDecimalIpv4`, `ProcessRole`, and `ApiHookRequest`.
- Produces:
  - immutable `ServerAddressState { octets, ansi, wide }`
  - `CreateServerAddressState(std::string_view)` with canonical dotted-decimal ANSI/Wide strings
  - `NormalizeAddrInfoW` and `NormalizeAddrInfoExW`
  - `AsyncResolverHintCache::Get(const ADDRINFOEXW*)` with process-lifetime immutable entries
  - testable `RedirectGetAddrInfoW`, `RedirectGetAddrInfoExW`, and `RedirectGetHostByName` dispatch functions
  - `InitializeServerAddressOverride` and `AppendServerAddressHookRequests` for runtime

- [ ] **Step 1: Write failing resolver-policy tests**

Create `tests/ServerAddressOverrideTests.cpp`. Define capture trampolines with the exact Winsock signatures:

~~~cpp
#include "ServerAddressOverride.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <thread>
#include <vector>

namespace {

struct ModernCapture {
    int calls{0};
    PCWSTR node{nullptr};
    PCWSTR service{nullptr};
    const ADDRINFOW* hints_pointer{nullptr};
    ADDRINFOW hints_value{};
    bool had_hints{false};
    PADDRINFOW result_value{
        reinterpret_cast<PADDRINFOW>(0x12345678)};
    INT return_value{WSAHOST_NOT_FOUND};
};

ModernCapture g_modern{};

INT WSAAPI fake_get_addr_info_w(
    PCWSTR node,
    PCWSTR service,
    const ADDRINFOW* hints,
    PADDRINFOW* result) {
    ++g_modern.calls;
    g_modern.node = node;
    g_modern.service = service;
    g_modern.hints_pointer = hints;
    g_modern.had_hints = hints != nullptr;
    if (hints != nullptr) {
        g_modern.hints_value = *hints;
    }
    *result = g_modern.result_value;
    return g_modern.return_value;
}

struct ExCapture {
    int calls{0};
    PCWSTR node{nullptr};
    PCWSTR service{nullptr};
    DWORD name_space{0};
    LPGUID provider{nullptr};
    const ADDRINFOEXW* hints_pointer{nullptr};
    ADDRINFOEXW hints_value{};
    bool had_hints{false};
    PADDRINFOEXW result_value{
        reinterpret_cast<PADDRINFOEXW>(0x23456789)};
    timeval* timeout{nullptr};
    LPOVERLAPPED overlapped{nullptr};
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion{nullptr};
    LPHANDLE cancel_handle{nullptr};
    INT return_value{WSA_IO_PENDING};
};

ExCapture g_ex{};

INT WSAAPI fake_get_addr_info_ex_w(
    PCWSTR node,
    PCWSTR service,
    DWORD name_space,
    LPGUID provider,
    const ADDRINFOEXW* hints,
    PADDRINFOEXW* result,
    timeval* timeout,
    LPOVERLAPPED overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
    LPHANDLE cancel_handle) {
    ++g_ex.calls;
    g_ex.node = node;
    g_ex.service = service;
    g_ex.name_space = name_space;
    g_ex.provider = provider;
    g_ex.hints_pointer = hints;
    g_ex.had_hints = hints != nullptr;
    if (hints != nullptr) {
        g_ex.hints_value = *hints;
    }
    *result = g_ex.result_value;
    g_ex.timeout = timeout;
    g_ex.overlapped = overlapped;
    g_ex.completion = completion;
    g_ex.cancel_handle = cancel_handle;
    return g_ex.return_value;
}

hostent g_passthrough_host{};
int g_legacy_calls = 0;

hostent* WSAAPI fake_get_host_by_name(const char*) {
    ++g_legacy_calls;
    return &g_passthrough_host;
}

std::unique_ptr<ADDRINFOEXW> reject_hint_allocation() {
    return nullptr;
}

void CALLBACK fake_completion(DWORD, DWORD, LPWSAOVERLAPPED) {
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool has_export(
    std::span<const char* const> exports,
    const char* name) {
    for (const char* value : exports) {
        if (std::strcmp(value, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;

    const auto state_result =
        CreateServerAddressState("10.23.45.67");
    failures += expect(state_result.has_value(), "valid server state");
    const auto& state = *state_result;
    failures += expect(
        state.octets == Ipv4Octets{10, 23, 45, 67} &&
            state.ansi == "10.23.45.67" &&
            state.wide == L"10.23.45.67",
        "canonical server state");

    ADDRINFOW hints{};
    hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL | AI_CANONNAME;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    PADDRINFOW result = nullptr;
    g_modern = {};
    const INT modern_status = RedirectGetAddrInfoW(
        state,
        fake_get_addr_info_w,
        L"original.example",
        L"443",
        &hints,
        &result);
    failures += expect(
        modern_status == WSAHOST_NOT_FOUND &&
            g_modern.calls == 1,
        "modern error returned without retry");
    failures += expect(
        std::wcscmp(g_modern.node, L"10.23.45.67") == 0 &&
            std::wcscmp(g_modern.service, L"443") == 0,
        "modern node replaced and service preserved");
    failures += expect(
        g_modern.had_hints &&
            g_modern.hints_value.ai_family == AF_INET &&
            (g_modern.hints_value.ai_flags & AI_NUMERICHOST) != 0 &&
            (g_modern.hints_value.ai_flags &
             (AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL)) == 0 &&
            (g_modern.hints_value.ai_flags & AI_CANONNAME) != 0 &&
            g_modern.hints_value.ai_socktype == SOCK_STREAM &&
            g_modern.hints_value.ai_protocol == IPPROTO_TCP,
        "modern hints normalized");
    failures += expect(
        result == g_modern.result_value,
        "Winsock result ownership preserved");

    g_modern = {};
    result = nullptr;
    RedirectGetAddrInfoW(
        state,
        fake_get_addr_info_w,
        nullptr,
        L"0",
        &hints,
        &result);
    failures += expect(
        g_modern.node == nullptr &&
            g_modern.hints_pointer == &hints,
        "null modern node passes through unchanged");

    AsyncResolverHintCache async_cache;
    ADDRINFOEXW ex_hints{};
    ex_hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
    ex_hints.ai_family = AF_INET6;
    ex_hints.ai_socktype = SOCK_DGRAM;
    ex_hints.ai_protocol = IPPROTO_UDP;
    GUID provider{};
    timeval timeout{2, 0};
    OVERLAPPED overlapped{};
    HANDLE cancel_handle = nullptr;
    PADDRINFOEXW ex_result = nullptr;
    g_ex = {};
    const INT ex_status = RedirectGetAddrInfoExW(
        state,
        async_cache,
        fake_get_addr_info_ex_w,
        L"original.example",
        L"12345",
        NS_DNS,
        &provider,
        &ex_hints,
        &ex_result,
        &timeout,
        &overlapped,
        fake_completion,
        &cancel_handle);
    const ADDRINFOEXW* persistent_hints = g_ex.hints_pointer;
    failures += expect(
        ex_status == WSA_IO_PENDING &&
            g_ex.calls == 1 &&
            std::wcscmp(g_ex.node, L"10.23.45.67") == 0,
        "async resolver redirected once");
    failures += expect(
        g_ex.service != nullptr &&
            std::wcscmp(g_ex.service, L"12345") == 0 &&
            g_ex.name_space == NS_DNS &&
            g_ex.provider == &provider &&
            g_ex.timeout == &timeout &&
            g_ex.overlapped == &overlapped &&
            g_ex.completion == fake_completion &&
            g_ex.cancel_handle == &cancel_handle,
        "async arguments preserved");
    failures += expect(
        persistent_hints != &ex_hints &&
            persistent_hints->ai_family == AF_INET &&
            (persistent_hints->ai_flags & AI_NUMERICHOST) != 0 &&
            persistent_hints->ai_socktype == SOCK_DGRAM &&
            persistent_hints->ai_protocol == IPPROTO_UDP,
        "async hints are normalized process storage");
    failures += expect(
        ex_result == g_ex.result_value,
        "async Winsock result ownership preserved");

    g_ex = {};
    RedirectGetAddrInfoExW(
        state,
        async_cache,
        fake_get_addr_info_ex_w,
        L"second.example",
        L"9999",
        NS_DNS,
        &provider,
        &ex_hints,
        &ex_result,
        &timeout,
        &overlapped,
        fake_completion,
        &cancel_handle);
    failures += expect(
        g_ex.hints_pointer == persistent_hints,
        "equal async hint key deduplicated");

    g_ex = {};
    g_ex.return_value = 0;
    ex_result = nullptr;
    failures += expect(
        RedirectGetAddrInfoExW(
            state,
            async_cache,
            fake_get_addr_info_ex_w,
            L"sync.example",
            L"8443",
            NS_DNS,
            &provider,
            &ex_hints,
            &ex_result,
            &timeout,
            nullptr,
            nullptr,
            nullptr) == 0 &&
            g_ex.hints_pointer != &ex_hints &&
            g_ex.hints_value.ai_family == AF_INET &&
            g_ex.overlapped == nullptr,
        "synchronous GetAddrInfoExW uses local normalized hints");

    g_ex = {};
    ex_result = nullptr;
    RedirectGetAddrInfoExW(
        state,
        async_cache,
        fake_get_addr_info_ex_w,
        nullptr,
        L"0",
        NS_DNS,
        &provider,
        &ex_hints,
        &ex_result,
        &timeout,
        &overlapped,
        fake_completion,
        &cancel_handle);
    failures += expect(
        g_ex.node == nullptr &&
            g_ex.hints_pointer == &ex_hints,
        "null GetAddrInfoExW node passes through unchanged");

    std::array<const ADDRINFOEXW*, 8> concurrent{};
    std::array<std::thread, 8> threads;
    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index] {
            concurrent[index] = async_cache.Get(&ex_hints);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    failures += expect(
        std::all_of(
            concurrent.begin(),
            concurrent.end(),
            [persistent_hints](const ADDRINFOEXW* value) {
                return value == persistent_hints;
            }),
        "concurrent async cache uses stable object");

    AsyncResolverHintCache failing_cache(reject_hint_allocation);
    g_ex = {};
    failures += expect(
        RedirectGetAddrInfoExW(
            state,
            failing_cache,
            fake_get_addr_info_ex_w,
            L"original.example",
            L"80",
            NS_DNS,
            nullptr,
            &ex_hints,
            &ex_result,
            nullptr,
            &overlapped,
            nullptr,
            nullptr) == WSA_NOT_ENOUGH_MEMORY &&
            g_ex.calls == 0,
        "async allocation failure does not call resolver");

    g_legacy_calls = 0;
    hostent* legacy = RedirectGetHostByName(
        state,
        fake_get_host_by_name,
        "original.example");
    failures += expect(
        legacy != nullptr &&
            std::strcmp(legacy->h_name, "original.example") == 0 &&
            legacy->h_aliases[0] == nullptr &&
            legacy->h_addrtype == AF_INET &&
            legacy->h_length == 4 &&
            legacy->h_addr_list[0] != nullptr &&
            legacy->h_addr_list[1] == nullptr &&
            std::memcmp(
                legacy->h_addr_list[0],
                state.octets.data(),
                state.octets.size()) == 0 &&
            g_legacy_calls == 0,
        "legacy synthetic hostent");
    failures += expect(
        RedirectGetHostByName(
            state,
            fake_get_host_by_name,
            nullptr) == &g_passthrough_host &&
            g_legacy_calls == 1,
        "legacy null name pass-through");

    std::uintptr_t other_thread_host = 0;
    std::thread legacy_thread([&] {
        other_thread_host = reinterpret_cast<std::uintptr_t>(
            RedirectGetHostByName(
                state,
                fake_get_host_by_name,
                "thread.example"));
    });
    legacy_thread.join();
    failures += expect(
        other_thread_host != reinterpret_cast<std::uintptr_t>(legacy),
        "legacy hostent is thread-local");

    const auto game_exports =
        ServerAddressHookExports(ProcessRole::Game);
    const auto service_exports =
        ServerAddressHookExports(ProcessRole::Service);
    failures += expect(
        game_exports.size() == 2 &&
            has_export(game_exports, "GetAddrInfoW") &&
            has_export(game_exports, "GetAddrInfoExW"),
        "game resolver inventory");
    failures += expect(
        service_exports.size() == 3 &&
            has_export(service_exports, "gethostbyname"),
        "service resolver inventory");
    failures += expect(
        !has_export(service_exports, "connect") &&
            !has_export(service_exports, "WSAConnect") &&
            !has_export(service_exports, "FreeAddrInfoW") &&
            !has_export(service_exports, "FreeAddrInfoExW"),
        "socket and free APIs remain unhooked");

    return failures == 0 ? 0 : 1;
}
~~~

- [ ] **Step 2: Register the resolver test and verify the red state**

Add:

~~~cmake
add_executable(ServerAddressOverrideTests
        NesysNetworkConfig.cpp
        ServerAddressOverride.cpp
        tests/ServerAddressOverrideTests.cpp
)
target_include_directories(ServerAddressOverrideTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
add_test(NAME ServerAddressOverrideTests COMMAND ServerAddressOverrideTests)
~~~

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ServerAddressOverrideTests'
~~~

Expected: compilation fails because `ServerAddressOverride.h` does not exist.

- [ ] **Step 3: Define immutable state, cache, dispatch, and hook interfaces**

Create `ServerAddressOverride.h`:

~~~cpp
#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "NesysHookTransaction.h"
#include "NesysNetworkConfig.h"
#include "NesysServiceProcess.h"

#include <compare>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gc::nesys_service {

using GetAddrInfoWFn = INT(WSAAPI*)(
    PCWSTR,
    PCWSTR,
    const ADDRINFOW*,
    PADDRINFOW*);
using GetAddrInfoExWFn = INT(WSAAPI*)(
    PCWSTR,
    PCWSTR,
    DWORD,
    LPGUID,
    const ADDRINFOEXW*,
    PADDRINFOEXW*,
    timeval*,
    LPOVERLAPPED,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE,
    LPHANDLE);
using GetHostByNameFn = hostent*(WSAAPI*)(const char*);

struct ServerAddressState {
    Ipv4Octets octets{};
    std::string ansi;
    std::wstring wide;
};

std::optional<ServerAddressState> CreateServerAddressState(
    std::string_view configured);

struct ResolverHintKey {
    int flags{0};
    int socket_type{0};
    int protocol{0};

    auto operator<=>(const ResolverHintKey&) const = default;
};

using ResolverHintAllocator =
    std::unique_ptr<ADDRINFOEXW>(*)();

class AsyncResolverHintCache {
public:
    explicit AsyncResolverHintCache(
        ResolverHintAllocator allocator = nullptr) noexcept;

    const ADDRINFOEXW* Get(const ADDRINFOEXW* source) noexcept;

private:
    ResolverHintAllocator allocator_;
    std::mutex mutex_;
    std::map<
        ResolverHintKey,
        std::unique_ptr<ADDRINFOEXW>> entries_;
};

ADDRINFOW NormalizeAddrInfoW(const ADDRINFOW* source) noexcept;
ADDRINFOEXW NormalizeAddrInfoExW(const ADDRINFOEXW* source) noexcept;

INT RedirectGetAddrInfoW(
    const ServerAddressState& state,
    GetAddrInfoWFn original,
    PCWSTR node,
    PCWSTR service,
    const ADDRINFOW* hints,
    PADDRINFOW* result) noexcept;

INT RedirectGetAddrInfoExW(
    const ServerAddressState& state,
    AsyncResolverHintCache& cache,
    GetAddrInfoExWFn original,
    PCWSTR node,
    PCWSTR service,
    DWORD name_space,
    LPGUID provider,
    const ADDRINFOEXW* hints,
    PADDRINFOEXW* result,
    timeval* timeout,
    LPOVERLAPPED overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
    LPHANDLE cancel_handle) noexcept;

hostent* RedirectGetHostByName(
    const ServerAddressState& state,
    GetHostByNameFn original,
    const char* requested_name) noexcept;

bool InitializeServerAddressOverride(
    std::string_view configured) noexcept;
std::span<const char* const> ServerAddressHookExports(
    ProcessRole role) noexcept;
void AppendServerAddressHookRequests(
    ProcessRole role,
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service
~~~

- [ ] **Step 4: Implement resolver normalization and process/thread lifetime storage**

Create `ServerAddressOverride.cpp`:

~~~cpp
#include "ServerAddressOverride.h"

#include <array>
#include <atomic>
#include <cstring>
#include <new>
#include <utility>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

GetAddrInfoWFn g_original_get_addr_info_w = nullptr;
GetAddrInfoExWFn g_original_get_addr_info_ex_w = nullptr;
GetHostByNameFn g_original_get_host_by_name = nullptr;
std::unique_ptr<const ServerAddressState> g_server_address;
AsyncResolverHintCache g_async_hints;
std::atomic_flag g_modern_logged = ATOMIC_FLAG_INIT;
std::atomic_flag g_legacy_logged = ATOMIC_FLAG_INIT;

constexpr std::array<const char*, 2> kGameExports{
    "GetAddrInfoW",
    "GetAddrInfoExW",
};
constexpr std::array<const char*, 3> kServiceExports{
    "GetAddrInfoW",
    "GetAddrInfoExW",
    "gethostbyname",
};

int normalized_flags(int flags) noexcept {
    constexpr int removed =
        AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL;
    return (flags | AI_NUMERICHOST) & ~removed;
}

std::string canonical_ipv4(const Ipv4Octets& octets) {
    return std::to_string(octets[0]) + "." +
        std::to_string(octets[1]) + "." +
        std::to_string(octets[2]) + "." +
        std::to_string(octets[3]);
}

std::unique_ptr<ADDRINFOEXW> allocate_hint() {
    return std::make_unique<ADDRINFOEXW>();
}

void log_first(
    std::atomic_flag& flag,
    const char* family,
    const wchar_t* original_node) noexcept {
    if (flag.test_and_set(std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO << "ServerAddressOverride: first " << family
                  << " node_present=" << (original_node != nullptr);
    } catch (...) {
    }
}

void log_first_legacy(const char* original_name) noexcept {
    if (g_legacy_logged.test_and_set(std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO << "ServerAddressOverride: first legacy-resolver family"
                  << " original_name="
                  << (original_name != nullptr
                          ? original_name
                          : "<null>");
    } catch (...) {
    }
}

INT WSAAPI get_addr_info_w_detour(
    PCWSTR node,
    PCWSTR service,
    const ADDRINFOW* hints,
    PADDRINFOW* result) {
    log_first(g_modern_logged, "modern-resolver family", node);
    if (g_server_address == nullptr) {
        return WSAEINVAL;
    }
    return RedirectGetAddrInfoW(
        *g_server_address,
        g_original_get_addr_info_w,
        node,
        service,
        hints,
        result);
}

INT WSAAPI get_addr_info_ex_w_detour(
    PCWSTR node,
    PCWSTR service,
    DWORD name_space,
    LPGUID provider,
    const ADDRINFOEXW* hints,
    PADDRINFOEXW* result,
    timeval* timeout,
    LPOVERLAPPED overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
    LPHANDLE cancel_handle) {
    log_first(g_modern_logged, "modern-resolver family", node);
    if (g_server_address == nullptr) {
        return WSAEINVAL;
    }
    return RedirectGetAddrInfoExW(
        *g_server_address,
        g_async_hints,
        g_original_get_addr_info_ex_w,
        node,
        service,
        name_space,
        provider,
        hints,
        result,
        timeout,
        overlapped,
        completion,
        cancel_handle);
}

hostent* WSAAPI get_host_by_name_detour(const char* requested_name) {
    log_first_legacy(requested_name);
    if (g_server_address == nullptr) {
        return nullptr;
    }
    return RedirectGetHostByName(
        *g_server_address,
        g_original_get_host_by_name,
        requested_name);
}

} // namespace

std::optional<ServerAddressState> CreateServerAddressState(
    std::string_view configured) {
    const auto octets = ParseDottedDecimalIpv4(configured);
    if (!octets.has_value()) {
        return std::nullopt;
    }

    ServerAddressState state{};
    state.octets = *octets;
    state.ansi = canonical_ipv4(*octets);
    state.wide.assign(state.ansi.begin(), state.ansi.end());
    return state;
}

ADDRINFOW NormalizeAddrInfoW(const ADDRINFOW* source) noexcept {
    ADDRINFOW normalized{};
    if (source != nullptr) {
        normalized.ai_flags = source->ai_flags;
        normalized.ai_socktype = source->ai_socktype;
        normalized.ai_protocol = source->ai_protocol;
    }
    normalized.ai_flags = normalized_flags(normalized.ai_flags);
    normalized.ai_family = AF_INET;
    return normalized;
}

ADDRINFOEXW NormalizeAddrInfoExW(
    const ADDRINFOEXW* source) noexcept {
    ADDRINFOEXW normalized{};
    if (source != nullptr) {
        normalized.ai_flags = source->ai_flags;
        normalized.ai_socktype = source->ai_socktype;
        normalized.ai_protocol = source->ai_protocol;
    }
    normalized.ai_flags = normalized_flags(normalized.ai_flags);
    normalized.ai_family = AF_INET;
    return normalized;
}

AsyncResolverHintCache::AsyncResolverHintCache(
    ResolverHintAllocator allocator) noexcept
    : allocator_(allocator != nullptr ? allocator : allocate_hint) {
}

const ADDRINFOEXW* AsyncResolverHintCache::Get(
    const ADDRINFOEXW* source) noexcept {
    const auto normalized = NormalizeAddrInfoExW(source);
    const ResolverHintKey key{
        normalized.ai_flags,
        normalized.ai_socktype,
        normalized.ai_protocol,
    };

    try {
        std::lock_guard lock(mutex_);
        if (const auto existing = entries_.find(key);
            existing != entries_.end()) {
            return existing->second.get();
        }

        auto entry = allocator_();
        if (entry == nullptr) {
            return nullptr;
        }
        *entry = normalized;
        const auto [inserted, was_inserted] =
            entries_.emplace(key, std::move(entry));
        return inserted->second.get();
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

INT RedirectGetAddrInfoW(
    const ServerAddressState& state,
    GetAddrInfoWFn original,
    PCWSTR node,
    PCWSTR service,
    const ADDRINFOW* hints,
    PADDRINFOW* result) noexcept {
    if (original == nullptr) {
        return WSAEINVAL;
    }
    if (node == nullptr) {
        return original(node, service, hints, result);
    }

    const ADDRINFOW normalized = NormalizeAddrInfoW(hints);
    return original(
        state.wide.c_str(),
        service,
        &normalized,
        result);
}

INT RedirectGetAddrInfoExW(
    const ServerAddressState& state,
    AsyncResolverHintCache& cache,
    GetAddrInfoExWFn original,
    PCWSTR node,
    PCWSTR service,
    DWORD name_space,
    LPGUID provider,
    const ADDRINFOEXW* hints,
    PADDRINFOEXW* result,
    timeval* timeout,
    LPOVERLAPPED overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
    LPHANDLE cancel_handle) noexcept {
    if (original == nullptr) {
        return WSAEINVAL;
    }
    if (node == nullptr) {
        return original(
            node,
            service,
            name_space,
            provider,
            hints,
            result,
            timeout,
            overlapped,
            completion,
            cancel_handle);
    }

    ADDRINFOEXW synchronous_hints{};
    const ADDRINFOEXW* normalized = nullptr;
    if (overlapped == nullptr) {
        synchronous_hints = NormalizeAddrInfoExW(hints);
        normalized = &synchronous_hints;
    } else {
        normalized = cache.Get(hints);
        if (normalized == nullptr) {
            return WSA_NOT_ENOUGH_MEMORY;
        }
    }

    return original(
        state.wide.c_str(),
        service,
        name_space,
        provider,
        normalized,
        result,
        timeout,
        overlapped,
        completion,
        cancel_handle);
}

hostent* RedirectGetHostByName(
    const ServerAddressState& state,
    GetHostByNameFn original,
    const char* requested_name) noexcept {
    if (requested_name == nullptr) {
        return original != nullptr ? original(requested_name) : nullptr;
    }

    struct LegacyStorage {
        std::string requested;
        std::array<char, 4> address{};
        std::array<char*, 1> aliases{};
        std::array<char*, 2> addresses{};
        hostent value{};
    };
    thread_local LegacyStorage storage;

    try {
        storage.requested.assign(requested_name);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    std::memcpy(
        storage.address.data(),
        state.octets.data(),
        state.octets.size());
    storage.aliases = {nullptr};
    storage.addresses = {storage.address.data(), nullptr};
    storage.value.h_name = storage.requested.data();
    storage.value.h_aliases = storage.aliases.data();
    storage.value.h_addrtype = AF_INET;
    storage.value.h_length = 4;
    storage.value.h_addr_list = storage.addresses.data();
    return &storage.value;
}

bool InitializeServerAddressOverride(
    std::string_view configured) noexcept {
    try {
        auto state = CreateServerAddressState(configured);
        if (!state.has_value()) {
            return false;
        }
        if (g_server_address != nullptr) {
            return g_server_address->ansi == state->ansi;
        }
        g_server_address =
            std::make_unique<const ServerAddressState>(std::move(*state));
        PLOG_INFO << "ServerAddressOverride: configured server IPv4="
                  << g_server_address->ansi;
        return true;
    } catch (...) {
        return false;
    }
}

std::span<const char* const> ServerAddressHookExports(
    ProcessRole role) noexcept {
    return role == ProcessRole::Game
        ? std::span<const char* const>{
              kGameExports.data(),
              kGameExports.size()}
        : std::span<const char* const>{
              kServiceExports.data(),
              kServiceExports.size()};
}

void AppendServerAddressHookRequests(
    ProcessRole role,
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"ws2_32.dll",
        "GetAddrInfoW",
        reinterpret_cast<LPVOID>(&get_addr_info_w_detour),
        reinterpret_cast<LPVOID*>(&g_original_get_addr_info_w),
    });
    requests.push_back({
        L"ws2_32.dll",
        "GetAddrInfoExW",
        reinterpret_cast<LPVOID>(&get_addr_info_ex_w_detour),
        reinterpret_cast<LPVOID*>(&g_original_get_addr_info_ex_w),
    });
    if (role == ProcessRole::Service) {
        requests.push_back({
            L"ws2_32.dll",
            "gethostbyname",
            reinterpret_cast<LPVOID>(&get_host_by_name_detour),
            reinterpret_cast<LPVOID*>(&g_original_get_host_by_name),
        });
    }
}

} // namespace gc::nesys_service
~~~

The normalized structures deliberately copy only flags, socket type, and protocol; every pointer-bearing or reserved field is zero. This prevents an asynchronous request from retaining caller-owned pointer fields while preserving every defined hint input relevant to this binary.

- [ ] **Step 5: Add the component to the DLL and run focused tests**

Add `ServerAddressOverride.cpp` to `SOURCES`. Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ServerAddressOverrideTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R ServerAddressOverrideTests'
~~~

Expected: PASS. Both original numeric resolver failures and `WSA_IO_PENDING` return unchanged; the fake original is invoked once per non-null modern call, never on async allocation failure, and never for non-null legacy names.

- [ ] **Step 6: Commit**

~~~powershell
git add -- CMakeLists.txt ServerAddressOverride.h ServerAddressOverride.cpp tests/ServerAddressOverrideTests.cpp
git commit -m "Override NESYS server resolution"
~~~

### Task 6: Fail-Closed NESYS Service Launcher

**Files:**
- Create: `NesysServiceLauncher.h`
- Create: `NesysServiceLauncher.cpp`
- Modify: `tests/NesysServicePatchTests.cpp:1-130`
- Modify: `CMakeLists.txt:102-122,200-208`

**Interfaces:**
- Consumes: existing launch matching and suspension helpers from `NesysServiceProcess` plus `ApiHookRequest`.
- Produces:
  - `ServiceChildApi` injectable Win32 cleanup/resume table
  - `ServiceChildResult FinalizeInjectedServiceChild(PROCESS_INFORMATION*, bool, bool, const ServiceChildApi&) noexcept`
  - `bool InitializeNesysServiceLauncher(HMODULE) noexcept`
  - `void AppendNesysServiceLauncherHookRequest(std::vector<ApiHookRequest>&)`
  - game-only `CreateProcessA` interception that treats remote `LoadLibraryW != 0` as the service readiness handshake

- [ ] **Step 1: Add failing child-finalization tests**

In `tests/NesysServicePatchTests.cpp`, include:

~~~cpp
#include "NesysServiceLauncher.h"
~~~

Add this fake backend in the anonymous namespace:

~~~cpp
struct FakeChildApiState {
    int terminate_calls{0};
    int wait_calls{0};
    int resume_calls{0};
    int close_calls{0};
    DWORD resume_result{0};
};

FakeChildApiState* g_child_api = nullptr;

BOOL WINAPI fake_terminate(HANDLE, UINT) {
    ++g_child_api->terminate_calls;
    return TRUE;
}

DWORD WINAPI fake_wait(HANDLE, DWORD timeout) {
    ++g_child_api->wait_calls;
    return timeout == INFINITE ? WAIT_OBJECT_0 : WAIT_FAILED;
}

DWORD WINAPI fake_resume(HANDLE) {
    ++g_child_api->resume_calls;
    return g_child_api->resume_result;
}

BOOL WINAPI fake_close(HANDLE) {
    ++g_child_api->close_calls;
    return TRUE;
}

gc::nesys_service::ServiceChildApi fake_child_api() {
    return {
        fake_terminate,
        fake_wait,
        fake_resume,
        fake_close,
    };
}

PROCESS_INFORMATION fake_process_information() {
    return {
        reinterpret_cast<HANDLE>(0x1000),
        reinterpret_cast<HANDLE>(0x2000),
        11,
        22,
    };
}
~~~

Add these assertions in `main()`:

~~~cpp
using gc::nesys_service::FinalizeInjectedServiceChild;

FakeChildApiState injection_failure{};
g_child_api = &injection_failure;
auto failed_child = fake_process_information();
const auto failed_result = FinalizeInjectedServiceChild(
    &failed_child,
    false,
    false,
    fake_child_api());
failures += expect_false(failed_result.success, "failed injection result");
failures += expect_dword(
    failed_result.error,
    ERROR_DLL_INIT_FAILED,
    "failed injection error");
failures += expect_true(
    injection_failure.terminate_calls == 1 &&
        injection_failure.wait_calls == 1 &&
        injection_failure.resume_calls == 0 &&
        injection_failure.close_calls == 2,
    "failed injection terminates waits and closes");
failures += expect_true(
    failed_child.hProcess == nullptr &&
        failed_child.hThread == nullptr &&
        failed_child.dwProcessId == 0 &&
        failed_child.dwThreadId == 0,
    "failed injection clears process information");

FakeChildApiState success_resume{};
g_child_api = &success_resume;
auto resumed_child = fake_process_information();
const auto resumed_result = FinalizeInjectedServiceChild(
    &resumed_child,
    false,
    true,
    fake_child_api());
failures += expect_true(resumed_result.success, "successful injection");
failures += expect_true(
    resumed_result.resumed &&
        success_resume.resume_calls == 1 &&
        success_resume.terminate_calls == 0 &&
        success_resume.close_calls == 0,
    "successful normal launch resumes and preserves caller handles");

FakeChildApiState success_suspended{};
g_child_api = &success_suspended;
auto suspended_child = fake_process_information();
const auto suspended_result = FinalizeInjectedServiceChild(
    &suspended_child,
    true,
    true,
    fake_child_api());
failures += expect_true(
    suspended_result.success &&
        !suspended_result.resumed &&
        success_suspended.resume_calls == 0 &&
        success_suspended.terminate_calls == 0 &&
        success_suspended.close_calls == 0,
    "successful caller-suspended launch stays suspended");

FakeChildApiState resume_failure{};
resume_failure.resume_result = static_cast<DWORD>(-1);
g_child_api = &resume_failure;
auto unresumable_child = fake_process_information();
const auto unresumable_result = FinalizeInjectedServiceChild(
    &unresumable_child,
    false,
    true,
    fake_child_api());
failures += expect_true(
    !unresumable_result.success &&
        resume_failure.resume_calls == 1 &&
        resume_failure.terminate_calls == 1 &&
        resume_failure.wait_calls == 1 &&
        resume_failure.close_calls == 2,
    "resume failure fails closed");
~~~

- [ ] **Step 2: Compile the expanded launcher test and verify the red state**

Change the target to:

~~~cmake
add_executable(NesysServicePatchTests
        NesysServiceLauncher.cpp
        NesysServiceProcess.cpp
        tests/NesysServicePatchTests.cpp
)
target_include_directories(NesysServicePatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${minhook_SOURCE_DIR}/include
        ${plog_SOURCE_DIR}/include
)
~~~

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests'
~~~

Expected: compilation fails because `NesysServiceLauncher.h` does not exist.

- [ ] **Step 3: Define the launcher and child-finalization interface**

Create `NesysServiceLauncher.h`:

~~~cpp
#pragma once

#include <Windows.h>

#include "NesysHookTransaction.h"

#include <vector>

namespace gc::nesys_service {

struct ServiceChildApi {
    decltype(&TerminateProcess) terminate_process;
    decltype(&WaitForSingleObject) wait_for_single_object;
    decltype(&ResumeThread) resume_thread;
    decltype(&CloseHandle) close_handle;
};

struct ServiceChildResult {
    bool success{false};
    bool resumed{false};
    DWORD error{ERROR_SUCCESS};
};

ServiceChildApi ProductionServiceChildApi() noexcept;

ServiceChildResult FinalizeInjectedServiceChild(
    LPPROCESS_INFORMATION process_information,
    bool caller_requested_suspended,
    bool injection_succeeded,
    const ServiceChildApi& api) noexcept;

bool InitializeNesysServiceLauncher(HMODULE loader_module) noexcept;
void AppendNesysServiceLauncherHookRequest(
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service
~~~

- [ ] **Step 4: Implement injection and fail-closed child ownership**

Create `NesysServiceLauncher.cpp` by moving the module-path, UTF-8 logging, remote allocation/write/thread, and `CreateProcessA` logic out of the current `NesysServicePatch.cpp`, with these complete lifecycle rules:

~~~cpp
#include "NesysServiceLauncher.h"

#include "NesysServiceProcess.h"

#include <string>
#include <string_view>
#include <vector>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

using CreateProcessAFn = BOOL(WINAPI*)(
    LPCSTR,
    LPSTR,
    LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES,
    BOOL,
    DWORD,
    LPVOID,
    LPCSTR,
    LPSTARTUPINFOA,
    LPPROCESS_INFORMATION);

HMODULE g_loader_module = nullptr;
CreateProcessAFn g_original_create_process_a = nullptr;

std::wstring loader_module_path() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD copied = GetModuleFileNameW(
            g_loader_module,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            return {};
        }
        if (copied < buffer.size() - 1) {
            return std::wstring{buffer.data(), copied};
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool inject_current_dll(HANDLE process) noexcept {
    try {
        const auto path = loader_module_path();
        if (path.empty()) {
            return false;
        }

        const SIZE_T byte_count =
            (path.size() + 1) * sizeof(wchar_t);
        LPVOID remote_path = VirtualAllocEx(
            process,
            nullptr,
            byte_count,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);
        if (remote_path == nullptr) {
            return false;
        }

        SIZE_T written = 0;
        if (WriteProcessMemory(
                process,
                remote_path,
                path.c_str(),
                byte_count,
                &written) == FALSE ||
            written != byte_count) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            return false;
        }

        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const auto load_library = kernel32 != nullptr
            ? reinterpret_cast<LPTHREAD_START_ROUTINE>(
                  GetProcAddress(kernel32, "LoadLibraryW"))
            : nullptr;
        if (load_library == nullptr) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            return false;
        }

        HANDLE injection_thread = CreateRemoteThread(
            process,
            nullptr,
            0,
            load_library,
            remote_path,
            0,
            nullptr);
        if (injection_thread == nullptr) {
            VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
            return false;
        }

        const DWORD wait =
            WaitForSingleObject(injection_thread, 5000);
        if (wait != WAIT_OBJECT_0) {
            CloseHandle(injection_thread);
            return false;
        }

        DWORD remote_module = 0;
        const BOOL got_exit_code =
            GetExitCodeThread(injection_thread, &remote_module);
        CloseHandle(injection_thread);
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        if (got_exit_code == FALSE || remote_module == 0) {
            return false;
        }

        PLOG_INFO
            << "NesysServiceLauncher: child DLL initialization succeeded";
        return true;
    } catch (...) {
        return false;
    }
}

BOOL WINAPI create_process_a_detour(
    LPCSTR application_name,
    LPSTR command_line,
    LPSECURITY_ATTRIBUTES process_attributes,
    LPSECURITY_ATTRIBUTES thread_attributes,
    BOOL inherit_handles,
    DWORD creation_flags,
    LPVOID environment,
    LPCSTR current_directory,
    LPSTARTUPINFOA startup_info,
    LPPROCESS_INFORMATION process_information) {
    if (!IsNesysServiceLaunchA(application_name, command_line)) {
        return g_original_create_process_a(
            application_name,
            command_line,
            process_attributes,
            thread_attributes,
            inherit_handles,
            creation_flags,
            environment,
            current_directory,
            startup_info,
            process_information);
    }

    if (process_information == nullptr) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    PLOG_INFO
        << "NesysServiceLauncher: intercepting suspended service child";
    const bool caller_requested_suspended =
        WasCreateSuspendedRequested(creation_flags);
    const BOOL created = g_original_create_process_a(
        application_name,
        command_line,
        process_attributes,
        thread_attributes,
        inherit_handles,
        AddCreateSuspendedFlag(creation_flags),
        environment,
        current_directory,
        startup_info,
        process_information);
    const DWORD original_error = GetLastError();
    if (created == FALSE) {
        SetLastError(original_error);
        return FALSE;
    }

    const bool injected =
        process_information->hProcess != nullptr &&
        inject_current_dll(process_information->hProcess);
    const auto finalized = FinalizeInjectedServiceChild(
        process_information,
        caller_requested_suspended,
        injected,
        ProductionServiceChildApi());
    if (!finalized.success) {
        PLOG_ERROR
            << "NesysServiceLauncher: child initialization failed;"
            << " terminated suspended child";
        SetLastError(finalized.error);
        return FALSE;
    }

    PLOG_INFO
        << "NesysServiceLauncher: child ready resume="
        << finalized.resumed;
    SetLastError(original_error);
    return TRUE;
}

} // namespace

ServiceChildApi ProductionServiceChildApi() noexcept {
    return {
        TerminateProcess,
        WaitForSingleObject,
        ResumeThread,
        CloseHandle,
    };
}

ServiceChildResult FinalizeInjectedServiceChild(
    LPPROCESS_INFORMATION process_information,
    bool caller_requested_suspended,
    bool injection_succeeded,
    const ServiceChildApi& api) noexcept {
    const auto fail_closed = [&]() noexcept {
        if (process_information != nullptr &&
            process_information->hProcess != nullptr) {
            api.terminate_process(
                process_information->hProcess,
                ERROR_DLL_INIT_FAILED);
            api.wait_for_single_object(
                process_information->hProcess,
                INFINITE);
        }
        if (process_information != nullptr &&
            process_information->hThread != nullptr) {
            api.close_handle(process_information->hThread);
        }
        if (process_information != nullptr &&
            process_information->hProcess != nullptr) {
            api.close_handle(process_information->hProcess);
        }
        if (process_information != nullptr) {
            *process_information = {};
        }
        return ServiceChildResult{
            false,
            false,
            ERROR_DLL_INIT_FAILED,
        };
    };

    if (!injection_succeeded ||
        process_information == nullptr ||
        process_information->hProcess == nullptr ||
        process_information->hThread == nullptr) {
        return fail_closed();
    }

    if (caller_requested_suspended) {
        return {true, false, ERROR_SUCCESS};
    }

    if (api.resume_thread(process_information->hThread) ==
        static_cast<DWORD>(-1)) {
        return fail_closed();
    }
    return {true, true, ERROR_SUCCESS};
}

bool InitializeNesysServiceLauncher(
    HMODULE loader_module) noexcept {
    if (loader_module == nullptr) {
        return false;
    }
    g_loader_module = loader_module;
    return true;
}

void AppendNesysServiceLauncherHookRequest(
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"kernel32.dll",
        "CreateProcessA",
        reinterpret_cast<LPVOID>(&create_process_a_detour),
        reinterpret_cast<LPVOID*>(&g_original_create_process_a),
    });
}

} // namespace gc::nesys_service
~~~

On an injection-thread timeout, leave the remote path allocated until `FinalizeInjectedServiceChild` terminates the child; process teardown then reclaims that address space without racing a still-running remote thread.

- [ ] **Step 5: Add the launcher source and run focused tests**

Add `NesysServiceLauncher.cpp` to `SOURCES`. Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests iDmacDrv32 && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R NesysServicePatchTests'
~~~

Expected: PASS. Failed injection and failed resume terminate/wait/close/clear; successful normal launch resumes exactly once; successful caller-suspended launch remains suspended with caller-owned handles intact.

- [ ] **Step 6: Commit**

~~~powershell
git add -- CMakeLists.txt NesysServiceLauncher.h NesysServiceLauncher.cpp tests/NesysServicePatchTests.cpp
git commit -m "Fail closed on NESYS service injection"
~~~

### Task 7: Transactional Role Lifecycle and DllMain Gate

**Files:**
- Modify: `NesysServiceProcess.h:8-29`
- Modify: `NesysServiceProcess.cpp:143-181`
- Modify: `tests/NesysServicePatchTests.cpp`
- Replace: `NesysServicePatch.h`
- Replace: `NesysServicePatch.cpp`
- Modify: `dllmain.cpp:74-111`
- Modify: `CMakeLists.txt:102-229`

**Interfaces:**
- Consumes:
  - `InitializeServerAddressOverride` and `AppendServerAddressHookRequests`
  - `AppendSyntheticAdapterHookRequests`, ping preflight/activation/rollback
  - `InitializeNesysServiceLauncher` and `AppendNesysServiceLauncherHookRequest`
  - `ResolveApiHooks` and `OwnedMinHookTransaction`
- Produces:
  - `NesysFeaturePlan ResolveNesysFeaturePlan(ProcessRole, bool) noexcept`
  - `bool NesysServicePatchInit(HMODULE, ProcessRole) noexcept`
  - a successful return only after all six game API hooks or all ten service API hooks plus the service ping hook are active
  - `FALSE` from process attach on enabled-mode installation failure

- [ ] **Step 1: Add failing role-plan tests**

Add to `tests/NesysServicePatchTests.cpp`:

~~~cpp
const auto disabled_game =
    gc::nesys_service::ResolveNesysFeaturePlan(
        gc::nesys_service::ProcessRole::Game,
        false);
failures += expect_true(
    !disabled_game.enabled &&
        !disabled_game.synthetic_adapter &&
        !disabled_game.server_address_override &&
        !disabled_game.service_launcher &&
        !disabled_game.service_ping_redirect &&
        disabled_game.api_hook_count == 0,
    "disabled game installs nothing");

const auto game_plan =
    gc::nesys_service::ResolveNesysFeaturePlan(
        gc::nesys_service::ProcessRole::Game,
        true);
failures += expect_true(
    game_plan.enabled &&
        game_plan.synthetic_adapter &&
        game_plan.server_address_override &&
        game_plan.service_launcher &&
        !game_plan.service_ping_redirect &&
        game_plan.api_hook_count == 6,
    "enabled game component plan");

const auto service_plan =
    gc::nesys_service::ResolveNesysFeaturePlan(
        gc::nesys_service::ProcessRole::Service,
        true);
failures += expect_true(
    service_plan.enabled &&
        service_plan.synthetic_adapter &&
        service_plan.server_address_override &&
        !service_plan.service_launcher &&
        service_plan.service_ping_redirect &&
        service_plan.api_hook_count == 10,
    "enabled service component plan");
~~~

- [ ] **Step 2: Build the process test and verify the red state**

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target NesysServicePatchTests'
~~~

Expected: compilation fails because `NesysFeaturePlan` and `ResolveNesysFeaturePlan` do not exist.

- [ ] **Step 3: Add the pure enabled/game/service component plan**

In `NesysServiceProcess.h`, add `#include <cstddef>` and:

~~~cpp
struct NesysFeaturePlan {
    bool enabled{false};
    bool synthetic_adapter{false};
    bool server_address_override{false};
    bool service_launcher{false};
    bool service_ping_redirect{false};
    std::size_t api_hook_count{0};
};

NesysFeaturePlan ResolveNesysFeaturePlan(
    ProcessRole role,
    bool enabled) noexcept;
~~~

In `NesysServiceProcess.cpp`, add:

~~~cpp
NesysFeaturePlan ResolveNesysFeaturePlan(
    ProcessRole role,
    bool enabled) noexcept {
    if (!enabled) {
        return {};
    }
    if (role == ProcessRole::Game) {
        return {
            true,
            true,
            true,
            true,
            false,
            6,
        };
    }
    return {
        true,
        true,
        true,
        false,
        true,
        10,
    };
}
~~~

- [ ] **Step 4: Replace the patch header with the Boolean DllMain gate**

Replace `NesysServicePatch.h` with:

~~~cpp
#pragma once

#include <Windows.h>

#include "NesysServiceProcess.h"

namespace gc::nesys_service {

bool NesysServicePatchInit(
    HMODULE loader_module,
    ProcessRole role) noexcept;

} // namespace gc::nesys_service
~~~

- [ ] **Step 5: Replace the old global-enable implementation with the complete role transaction**

Replace `NesysServicePatch.cpp` with:

~~~cpp
#include "NesysServicePatch.h"

#include "NesysHookTransaction.h"
#include "NesysServiceLauncher.h"
#include "ServerAddressOverride.h"
#include "SyntheticNetworkAdapter.h"
#include "config.h"

#include <Windows.h>

#include <atomic>
#include <iomanip>
#include <memory>
#include <vector>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

enum class InitializationState {
    Uninitialized,
    Initializing,
    Succeeded,
    Failed,
};

std::atomic<InitializationState> g_initialization{
    InitializationState::Uninitialized};
std::unique_ptr<OwnedMinHookTransaction> g_owned_hooks;

const char* stage_name(HookInstallStage stage) noexcept {
    switch (stage) {
    case HookInstallStage::None:
        return "none";
    case HookInstallStage::ResolveModule:
        return "resolve_module";
    case HookInstallStage::ResolveExport:
        return "resolve_export";
    case HookInstallStage::Initialize:
        return "initialize_minhook";
    case HookInstallStage::Create:
        return "create_hook";
    case HookInstallStage::QueueEnable:
        return "queue_enable";
    case HookInstallStage::ApplyQueued:
        return "apply_queued";
    }
    return "unknown";
}

void log_hook_error(const HookInstallError& error) noexcept {
    try {
        PLOG_ERROR
            << "NesysServicePatch: hook install failed"
            << " stage=" << stage_name(error.stage)
            << " export="
            << (error.export_name != nullptr
                    ? error.export_name
                    : "<none>")
            << " target=" << error.target
            << " minhook_status="
            << static_cast<int>(error.minhook_status)
            << " win32_error=" << error.win32_error;
    } catch (...) {
    }
}

bool initialize_enabled_feature(
    HMODULE loader_module,
    ProcessRole role,
    const NesysFeaturePlan& plan) {
    const auto& configured_ip =
        ConfigManager::instance().GetNesysServerIp();
    if (!InitializeServerAddressOverride(configured_ip)) {
        PLOG_ERROR
            << "NesysServicePatch: invalid NESYS server IPv4";
        return false;
    }

    const auto executable_base = reinterpret_cast<std::uintptr_t>(
        GetModuleHandleW(nullptr));
    if (executable_base == 0) {
        PLOG_ERROR
            << "NesysServicePatch: main executable module unavailable";
        return false;
    }

    std::vector<ApiHookRequest> requests;
    requests.reserve(plan.api_hook_count);
    AppendSyntheticAdapterHookRequests(role, requests);
    AppendServerAddressHookRequests(role, requests);
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
        PLOG_INFO
            << "NesysServicePatch: component active"
            << " name=synthetic_network_adapter";
        PLOG_INFO
            << "NesysServicePatch: component active"
            << " name=server_address_override";
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
            << " api_hooks=" << plan.api_hook_count
            << " synthetic_name=" << kSyntheticAdapterName
            << " synthetic_mac=DE-AD-BE-EF-00-01"
            << " synthetic_index=0x" << std::hex
            << kSyntheticInterfaceIndex
            << " synthetic_ipv4=" << kSyntheticIpv4
            << " link_state=up"
            << std::dec;
    } catch (...) {
    }
    return true;
}

} // namespace

bool NesysServicePatchInit(
    HMODULE loader_module,
    ProcessRole role) noexcept {
    InitializationState expected =
        InitializationState::Uninitialized;
    if (!g_initialization.compare_exchange_strong(
            expected,
            InitializationState::Initializing)) {
        return g_initialization.load() ==
            InitializationState::Succeeded;
    }

    bool success = false;
    try {
        const bool enabled = ConfigManager::instance()
            .GetEnableNesysServiceAdapterPatch();
        const auto plan = ResolveNesysFeaturePlan(role, enabled);
        PLOG_INFO
            << "NesysServicePatch: init"
            << " role=" << ProcessRoleName(role)
            << " enabled=" << enabled
            << " configured_server_ipv4="
            << ConfigManager::instance().GetNesysServerIp();

        success = !plan.enabled ||
            initialize_enabled_feature(
                loader_module,
                role,
                plan);
        if (!plan.enabled) {
            PLOG_INFO
                << "NesysServicePatch: disabled; installed no hooks";
        }
    } catch (const std::exception& error) {
        try {
            PLOG_ERROR
                << "NesysServicePatch: initialization exception="
                << error.what();
        } catch (...) {
        }
        success = false;
    } catch (...) {
        success = false;
    }

    g_initialization.store(
        success
            ? InitializationState::Succeeded
            : InitializationState::Failed);
    return success;
}

} // namespace gc::nesys_service
~~~

The ten service MinHook targets and the SafetyHook ping target are all created disabled before activation. MinHook applies its exact queued targets, then SafetyHook enables the prepared ping target while the launcher still holds the service primary thread suspended inside `LoadLibraryW`. No service network code runs between those operations; any prepare, queued-apply, or ping-enable failure removes both hook groups before `DllMain` returns `FALSE` and the parent terminates the child.

- [ ] **Step 6: Move the gate before every game-only initializer**

Replace the process-attach body in `dllmain.cpp` with:

~~~cpp
case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);
        InitSharedLog();

        PLOG_DEBUG << "DLL attach!" << std::endl;
        const auto role =
            gc::nesys_service::DetectCurrentProcessRole();
        PLOG_INFO
            << "NesysServicePatch: process role="
            << gc::nesys_service::ProcessRoleName(role);

        if (!gc::nesys_service::NesysServicePatchInit(
                hModule,
                role)) {
            PLOG_ERROR
                << "NesysServicePatch: fail-closed DLL attach";
            return FALSE;
        }

        if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
            RfidEmuInit();
            PLOG_DEBUG << "Rfid init complete!" << std::endl;

            FrameratePatchInit();
            PLOG_DEBUG
                << "120 FPS runtime patch init complete!"
                << std::endl;

            gc::switch_input::SwitchInputPatchInit();
            PLOG_DEBUG
                << "Switch gameplay input patch init complete!"
                << std::endl;
        } else {
            PLOG_INFO
                << "NesysServicePatch: service role skipping"
                << " game-only RFID/input/framerate initialization";
        }
        break;
    }
~~~

- [ ] **Step 7: Finalize the CMake source and test graph**

Ensure `SOURCES` contains each NESYS source exactly once:

~~~cmake
        NesysHookTransaction.cpp
        NesysNetworkConfig.cpp
        NesysServiceLauncher.cpp
        NesysServicePatch.cpp
        NesysServiceProcess.cpp
        ServerAddressOverride.cpp
        SyntheticNetworkAdapter.cpp
~~~

Keep these focused targets registered:

~~~cmake
add_test(NAME ConfigFeatureTests COMMAND ConfigFeatureTests)
add_test(NAME NesysHookTransactionTests COMMAND NesysHookTransactionTests)
add_test(NAME SyntheticNetworkAdapterTests COMMAND SyntheticNetworkAdapterTests)
add_test(NAME ServerAddressOverrideTests COMMAND ServerAddressOverrideTests)
add_test(NAME NesysServicePatchTests COMMAND NesysServicePatchTests)
~~~

- [ ] **Step 8: Build all NESYS targets and run focused tests**

Run:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target iDmacDrv32 ConfigGUI ConfigFeatureTests NesysHookTransactionTests SyntheticNetworkAdapterTests ServerAddressOverrideTests NesysServicePatchTests && ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure -R "ConfigFeatureTests|NesysHookTransactionTests|SyntheticNetworkAdapterTests|ServerAddressOverrideTests|NesysServicePatchTests"'
~~~

Expected: every target builds and all five focused tests pass.

- [ ] **Step 9: Prove the NESYS code has no global MinHook operation**

Run:

~~~powershell
rg -n "MH_(EnableHook|DisableHook|QueueEnableHook)\(MH_ALL_HOOKS\)" Nesys*.cpp Nesys*.h ServerAddressOverride.* SyntheticNetworkAdapter.*
~~~

Expected: no matches. `RfidEmu.cpp` is outside this feature-owned search and remains unchanged.

- [ ] **Step 10: Commit**

~~~powershell
git add -- CMakeLists.txt NesysServicePatch.h NesysServicePatch.cpp NesysServiceProcess.h NesysServiceProcess.cpp dllmain.cpp tests/NesysServicePatchTests.cpp
git commit -m "Activate NESYS virtualization transactionally"
~~~

### Task 8: Full Build, Deployment, Failure Gates, and Runtime Acceptance

**Files:**
- Runtime input only: `H:\gc\config.toml`
- Runtime executable under controlled backup: `H:\gc\NesysService.exe`
- Runtime output: `H:\gc\loader-log.txt`
- Build outputs: `build-msvc32-latest\iDmacDrv32.dll` and `build-msvc32-latest\ConfigGUI.exe`
- Temporary evidence: `$env:TEMP\gc-adapters-before.json`, `$env:TEMP\gc-adapters-after.json`, and `$env:TEMP\gc-nesys-network.etl`

**Interfaces:**
- Consumes: all code and tests from Tasks 1-7 plus a running local server at the configured IPv4.
- Produces: build/static proof, enabled and disabled runtime proof, packet-level DNS/IPv6/ICMP proof, real-adapter before/after proof, and an observed failed `LoadLibraryW` readiness handshake from a controlled service signature mismatch.

- [ ] **Step 1: Reconfigure and build every repository target**

Run from `H:\gc\artifacts\GCLoader`:

~~~powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake -S . -B build-msvc32-latest -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-msvc32-latest'
~~~

Expected: configuration and the complete build finish successfully, including `iDmacDrv32.dll`, `ConfigGUI.exe`, and every test executable.

- [ ] **Step 2: Run the complete CTest suite**

Run:

~~~powershell
ctest --test-dir build-msvc32-latest -C RelWithDebInfo --output-on-failure
~~~

Expected: all configured tests pass, including `ConfigFeatureTests`, `NesysHookTransactionTests`, `SyntheticNetworkAdapterTests`, `ServerAddressOverrideTests`, `NesysServicePatchTests`, and every pre-existing non-NESYS test.

- [ ] **Step 3: Verify the source-level compatibility boundaries**

Run:

~~~powershell
rg -n "FEAD3BD4|487402D4|SHA256|signature scan|pattern scan" Nesys*.cpp Nesys*.h ServerAddressOverride.* SyntheticNetworkAdapter.*
rg -n "0x00008E40|kServicePingSignature" SyntheticNetworkAdapter.h SyntheticNetworkAdapter.cpp tests/SyntheticNetworkAdapterTests.cpp
rg -n "\"(connect|WSAConnect|WinHttpConnect|WinHttpSendRequest|GetIpNetTable|FreeAddrInfoW|FreeAddrInfoExW)\"" Nesys*.cpp ServerAddressOverride.cpp SyntheticNetworkAdapter.cpp
~~~

Expected:

- First command: no executable hash gate, signature scan, or pattern scan implementation.
- Second command: only the fixed RVA and local 32-byte guard appear.
- Third command: excluded APIs do not appear in any `ApiHookRequest`. Mentions in tests that assert absence are acceptable.

- [ ] **Step 4: Prepare the required runtime configuration and deploy**

Back up `H:\gc\config.toml` outside the repository, then ensure it contains:

~~~toml
[nesys]
server_ip = '127.0.0.1'

[experimental]
enable_nesys_service_adapter_patch = true
~~~

Keep every other existing key and intentional value unchanged. Close `game471.exe` and `NesysService.exe`, then deploy:

~~~powershell
Copy-Item -LiteralPath 'H:\gc\artifacts\GCLoader\build-msvc32-latest\iDmacDrv32.dll' -Destination 'H:\gc\iDmacDrv32.dll' -Force
~~~

Expected: the copy succeeds and `Get-Item H:\gc\iDmacDrv32.dll` reports the new build time.

- [ ] **Step 5: Capture real adapter state before enabled-mode testing**

Run in an elevated PowerShell:

~~~powershell
Get-NetAdapter -IncludeHidden |
    Sort-Object InterfaceIndex |
    Select-Object Name,InterfaceDescription,InterfaceIndex,Status,MacAddress,LinkSpeed |
    ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath "$env:TEMP\gc-adapters-before.json"
Get-NetIPConfiguration -All |
    Sort-Object InterfaceIndex |
    Select-Object InterfaceAlias,InterfaceIndex,IPv4Address,IPv6Address,IPv4DefaultGateway,DNSServer |
    ConvertTo-Json -Depth 8 |
    Add-Content -LiteralPath "$env:TEMP\gc-adapters-before.json"
~~~

Expected: the temporary file records physical, virtual, VPN, disconnected, IPv4, IPv6, gateway, and DNS state before the run.

- [ ] **Step 6: Run enabled acceptance under hostile host networking**

Set up all of these conditions together:

- Connect the VPN-style adapter and leave it preferred ahead of physical adapters.
- Leave Windows IPv6 enabled.
- Disconnect external internet access.
- Make external DNS unavailable or deliberately invalid.
- Run the local server at `127.0.0.1`.
- Start Groove Coaster from `H:\gc`.

Start a packet capture before launch:

~~~powershell
pktmon start --capture --comp nics --pkt-size 0 --file-name "$env:TEMP\gc-nesys-network.etl"
~~~

After the game reaches the NESYS HTTP and legacy TCP paths, stop capture:

~~~powershell
pktmon stop
pktmon etl2pcap "$env:TEMP\gc-nesys-network.etl" --out "$env:TEMP\gc-nesys-network.pcapng"
~~~

Expected functional evidence:

- Startup does not stall with the VPN adapter present or preferred.
- The local server receives resolver-based HTTP and legacy TCP on each request's original port.
- WinHTTP continues to present the original request hostname above the resolver boundary.
- No external DNS availability is required.
- The service and game stay responsive while real IPv6 remains enabled.

Expected first-hit log families in `H:\gc\loader-log.txt`:

~~~text
NesysServicePatch: process role=game
NesysServicePatch: all role hooks active role=game api_hooks=6
SyntheticNetworkAdapter: first adapter-query family
SyntheticNetworkAdapter: first adapter-notification family
ServerAddressOverride: first modern-resolver family
NesysServiceLauncher: intercepting suspended service child
NesysServiceLauncher: child DLL initialization succeeded
NesysServiceLauncher: child ready resume=1
NesysServicePatch: process role=service
NesysServicePatch: all role hooks active role=service api_hooks=10
ServerAddressOverride: first legacy-resolver family
SyntheticNetworkAdapter: first mutation-suppression family
SyntheticNetworkAdapter: first ping-redirection family target=127.0.0.1
~~~

Open the PCAPNG in Wireshark and apply:

~~~text
dns || ipv6 || icmp
~~~

Expected: no DNS request for an original server hostname, no IPv6 server destination, and no ICMP packet for the original gateway/broadcast ping target leaves a real adapter. Loopback IPv4 traffic and unrelated host traffic are outside this assertion.

- [ ] **Step 7: Compare real adapter state after enabled mode**

Run:

~~~powershell
Get-NetAdapter -IncludeHidden |
    Sort-Object InterfaceIndex |
    Select-Object Name,InterfaceDescription,InterfaceIndex,Status,MacAddress,LinkSpeed |
    ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath "$env:TEMP\gc-adapters-after.json"
Get-NetIPConfiguration -All |
    Sort-Object InterfaceIndex |
    Select-Object InterfaceAlias,InterfaceIndex,IPv4Address,IPv6Address,IPv4DefaultGateway,DNSServer |
    ConvertTo-Json -Depth 8 |
    Add-Content -LiteralPath "$env:TEMP\gc-adapters-after.json"
Compare-Object (
    Get-Content -LiteralPath "$env:TEMP\gc-adapters-before.json"
) (
    Get-Content -LiteralPath "$env:TEMP\gc-adapters-after.json"
)
~~~

Expected: no loader-caused release, renewal, neighbor-table flush effect, address change, gateway change, status change, or synthetic Windows adapter appears. Ignore unrelated timestamp/counter churn not present in the selected fields.

- [ ] **Step 8: Verify missing and invalid configuration fail before hooks**

With the game and service closed, test two temporary `H:\gc\config.toml` variants:

1. Remove the entire `[nesys]` table.
2. Restore the table and set `server_ip = 'localhost'`.

Launch once per variant.

Expected for both: startup stops; no `all role hooks active` line appears; the process does not continue with original hostname resolution. Restore the valid runtime configuration afterward.

- [ ] **Step 9: Force the fixed-RVA mismatch and observe the launcher readiness failure**

Close both processes. Back up the service and record its hash:

~~~powershell
Copy-Item -LiteralPath 'H:\gc\NesysService.exe' -Destination 'H:\gc\NesysService.exe.nesys-test-backup' -Force
Get-FileHash -Algorithm SHA256 -LiteralPath 'H:\gc\NesysService.exe','H:\gc\NesysService.exe.nesys-test-backup'
~~~

Use this PE-aware PowerShell block to change only the first byte at `RVA 0x8E40` in the runtime copy:

~~~powershell
$path = 'H:\gc\NesysService.exe'
$bytes = [System.IO.File]::ReadAllBytes($path)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)
$sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
$optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
$sectionTable = $pe + 24 + $optionalSize
$rva = 0x8E40
$fileOffset = $null
for ($index = 0; $index -lt $sectionCount; ++$index) {
    $section = $sectionTable + (40 * $index)
    $virtualSize = [BitConverter]::ToUInt32($bytes, $section + 8)
    $virtualAddress = [BitConverter]::ToUInt32($bytes, $section + 12)
    $rawSize = [BitConverter]::ToUInt32($bytes, $section + 16)
    $rawPointer = [BitConverter]::ToUInt32($bytes, $section + 20)
    $mappedSize = [Math]::Max($virtualSize, $rawSize)
    if ($rva -ge $virtualAddress -and $rva -lt ($virtualAddress + $mappedSize)) {
        $fileOffset = $rawPointer + ($rva - $virtualAddress)
        break
    }
}
if ($null -eq $fileOffset) {
    throw 'RVA 0x8E40 is not mapped by a PE section'
}
if ($bytes[$fileOffset] -ne 0x51) {
    throw ('Unexpected original byte at RVA 0x8E40: 0x{0:X2}' -f $bytes[$fileOffset])
}
$bytes[$fileOffset] = 0x50
[System.IO.File]::WriteAllBytes($path, $bytes)
~~~

Launch with the valid enabled configuration.

Expected service-side/parent-side sequence:

~~~text
SyntheticNetworkAdapter: ping signature mismatch rva=0x8e40
NesysServicePatch: fail-closed DLL attach
NesysServiceLauncher: child initialization failed; terminated suspended child
~~~

Expected behavior: remote `LoadLibraryW` returns zero, the parent never resumes the child, `CreateProcessA` returns `FALSE` with `ERROR_DLL_INIT_FAILED`, the service process exits, and no partial service hook policy remains active.

Restore and verify the exact original:

~~~powershell
Copy-Item -LiteralPath 'H:\gc\NesysService.exe.nesys-test-backup' -Destination 'H:\gc\NesysService.exe' -Force
Get-FileHash -Algorithm SHA256 -LiteralPath 'H:\gc\NesysService.exe','H:\gc\NesysService.exe.nesys-test-backup'
~~~

Expected: the two restored hashes are identical. Keep or remove the backup only according to the operator's normal runtime-backup policy; never stage it in the repository.

- [ ] **Step 10: Verify disabled mode preserves original behavior**

Set only:

~~~toml
[experimental]
enable_nesys_service_adapter_patch = false
~~~

Keep required valid `[nesys].server_ip` present. Launch once.

Expected log:

~~~text
NesysServicePatch: disabled; installed no hooks
~~~

Expected absence: no adapter/resolver/ping hook activation, no service-child interception, and no loader-driven service injection. The original binaries retain their original adapter and network behavior. Restore `enable_nesys_service_adapter_patch = true` after acceptance if that is the desired operator state.

- [ ] **Step 11: Confirm repository cleanliness**

Run from the source repository:

~~~powershell
git status --short
git log -7 --oneline
~~~

Expected: only intentional source/plan history is present, `.superpowers/` remains untracked and unstaged, and no `H:\gc` runtime file appears in Git. Task 8 creates no repository commit.

## Self-Review

- **Spec coverage:** Task 1 covers required schema, GUI default/edit/save behavior, syntax validation, runtime validation, and round-trip. Tasks 3-4 cover every approved adapter field, native buffer contract, stable notification, mutation suppression, unhooked ARP reads, fixed-RVA guard, unrelated binary changes, and EAX-only ping redirect. Task 5 covers modern synchronous/asynchronous resolution, process-lifetime hints, null pass-through, original error propagation, Winsock result ownership, and service-only thread-local legacy resolution. Task 6 covers suspended injection and every success/failure cleanup branch. Task 7 composes the exact role hook sets, resolve-first preflight, owned transaction, DllMain ordering, disabled mode, and rollback. Task 8 covers full build, packet/runtime acceptance, real-adapter immutability, invalid/missing config, service signature mismatch/readiness failure, and disabled behavior.
- **Non-goals preserved:** no pipe emulation, URL/hostname rewrite above the resolver, port rewrite, connect/WinHTTP detour, numeric raw-TCP redirect, real NIC/route/DNS/DHCP creation, IPv6 OS change, fabricated ARP table, or multi-server rules.
- **Compatibility boundary:** only loaded exported API addresses and the 32 bytes at module base plus `0x8E40` gate installation. The provenance hashes never enter code.
- **Type consistency:** `ProcessRole`, `NesysFeaturePlan`, `ApiHookRequest`, `ResolvedApiHook`, `OwnedMinHookTransaction`, `ServerAddressState`, `ServiceChildApi`, and every append/init signature are defined before the task that consumes them.
- **Placeholder scan:** all created files, methods, test cases, commands, failure results, and runtime restore steps are named explicitly; no implementation decision is deferred.
