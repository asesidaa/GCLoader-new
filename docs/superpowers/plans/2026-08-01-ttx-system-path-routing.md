# Ttx System Path Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one validated registry system root drive both NESYS registry paths and game-process `TtxUpdateDownloader.dll` filesystem access, with default-only fallback and an explicit fatal guard for failed downloader initialization.

**Architecture:** A strict config-document layer migrates the three legacy NESYS leaf paths into `[registry].system_path` and atomically persists only approved game-role changes. A focused `gc_system_path` library resolves and preflights the root, routes the exact `D:\system` namespace, publishes startup failures, and owns the SafetyHook guard for the binary-verified Ttx initializer. The existing game-process Kernel32 MinHook layer composes RFID/JVS, test-mode storage, and system-root policies while passing every unowned call to its original trampoline.

**Tech Stack:** C++23, Windows x86, `std::filesystem`, toml++, reflect-cpp, Win32 ANSI/wide conversion and atomic replacement APIs, MinHook through `gc_hooking`, SafetyHook, CMake/Ninja presets, CTest, MSVC static runtime.

**Design:** `docs/superpowers/specs/2026-08-01-ttx-system-path-routing-design.md`

## Global Constraints

- Keep source, tests, plans, configuration, and commits in `H:\gc\artifacts\GCLoader`; inspect but do not deploy to or modify `H:\gc`.
- Preserve game-process and NESYS-process boundaries. Root preflight, Kernel32 system routing, and the Ttx guard are game-only; the NESYS process receives only derived registry strings.
- Keep `[registry].system_path` strict and required even when registry virtualization is disabled.
- Accept absolute and relative roots, including `system` and `.\system`; resolve relative values once against the directory containing `config.toml`.
- Keep `D:\system` when its complete required tree exists or can be created. Fall back to `.\system` only when registry virtualization is enabled and the configured value is semantically the unchanged shipped default.
- An unavailable custom path is fatal. Registry-disabled mode validates the real `D:\system`, never redirects it, and never automatically rewrites `config.toml`.
- Use Unicode-native `std::filesystem::path` values for resolution and routed filesystem work. Return service `RegQueryValueExA` strings only after lossless active-code-page conversion; recommend `.\system` when an absolute custom path is not representable.
- Persist enabled-mode schema migration or fallback with a same-directory atomic replacement. Never continue with an in-memory config that the NESYS child would not read from disk.
- Hook shared Kernel32 exports through the existing MinHook transaction. Do not patch Ttx instruction bytes, literals, or its IAT.
- Route only the exact case-insensitive Windows path component `D:\system` and its descendants. Do not match `D:\system2`, other drives, relative inputs, or the test-mode hash namespace.
- Apply that component rule process-wide in the game process; do not add a caller-address or caller-module filter. Every unmatched call keeps the original API and original pointer arguments.
- Resolve module `TtxUpdateDownloader.dll` and exactly the export `?TtxUDLInit@@YAHKKKK@Z`; call it as `int __cdecl(unsigned int, unsigned int, unsigned int, unsigned int)` through SafetyHook. Do not guess an RVA or ordinal for another binary.
- Do not hook `TtxUDLGetStatus`; failed initialization must terminate before returning to `game471.exe`.
- No exception may cross `DllMain`, a hooked Win32 function, or the Ttx detour. Preserve original arguments, return values, and last-error behavior on every pass-through path.
- Keep successful hook and filesystem paths free of per-call logging.
- Use TDD for production Tasks 1-7. Each failing test must name a plausible production regression and fail for missing behavior, not a fixture or build typo. Task 8 is postimplementation integration verification and may pass on its first run.
- Automated verification is build/static evidence. Booting the game, launching the NESYS child, and confirming updater behavior remain separate user runtime acceptance.

---

## File Structure

### New production files

- `src/Config/ConfigDocument.h`: parsed-document metadata, injectable atomic-write contract, and game-root preparation/persistence transaction surface.
- `src/Config/ConfigDocument.cpp`: TOML syntax migration, strict reparse, canonical serialization, sibling temporary files, atomic replacement, and transactional root preparation.
- `src/SystemPath/CMakeLists.txt`: `gc_system_path` target and dependencies.
- `src/SystemPath/SystemRoot.h`: logical-root constants, runtime root, availability actions, preparation result, and structured errors.
- `src/SystemPath/SystemRoot.cpp`: UTF-8/native path conversion, required-tree creation, default-only fallback, and disabled-mode behavior.
- `src/SystemPath/SystemPathRouter.h`: non-throwing path-route result and router interface.
- `src/SystemPath/SystemPathRouter.cpp`: component-aware `D:\system` matching, ANSI conversion, and Unicode destination construction.
- `src/SystemPath/StartupFatal.h`: injectable one-shot fatal publication actions.
- `src/SystemPath/StartupFatal.cpp`: logging, Unicode modal, termination, and fail-fast fallback.
- `src/SystemPath/TtxInitGuard.h`: verified Ttx ABI, install/runtime action seams, structured errors, and owning guard.
- `src/SystemPath/TtxInitGuard.cpp`: exact export resolution, disabled SafetyHook construction, trampoline call, and initialization-failure publication.

### New test files

- `tests/Config/ConfigDocumentTests.cpp`: legacy migration, ambiguity rejection, canonical serialization, and atomic-write behavior.
- `tests/Config/SystemPathConfigTests.cpp`: enabled/disabled persistence decisions and ConfigManager-facing orchestration.
- `tests/SystemPath/CMakeLists.txt`: focused system-path test targets.
- `tests/SystemPath/SystemRootTests.cpp`: default, fallback, custom, relative, Unicode, and required-tree contracts.
- `tests/SystemPath/SystemPathRouterTests.cpp`: exact match boundaries, A/W conversion, and move-operand behavior.
- `tests/SystemPath/TtxInitGuardTests.cpp`: ABI forwarding, failure publication, exact export resolution, and SafetyHook install rollback seams.
- `tests/SystemPath/SystemPathIntegrationTests.cpp`: public-API startup flow from migrated TOML through fallback, derived registry leaves, and routed downloader paths.
- `tests/Rfid/FeatureHookLayerTests.cpp`: MinHook/SafetyHook layer ordering and rollback after partial setup.

### Existing files to modify

- `config.toml`: add `[registry].system_path`; remove the three derived NESYS leaf assignments.
- `src/CMakeLists.txt`: build `SystemPath` before Config and link the new library into the game DLL path.
- `tests/CMakeLists.txt`: register `SystemPath` tests.
- `src/Config/RegistryConfig.h`: move path ownership to `RegistryConfig::system_path` and declare derived registry-path conversion.
- `src/Config/RegistryConfig.cpp`: validate the root and derive ANSI `NewsPath`, `EventPath`, and `LogPath` values.
- `src/Config/config.h`: expose parsed-document-aware ConfigManager state and game-root preparation.
- `src/Config/config.cpp`: delegate parsing to ConfigDocument, retain migration metadata, prepare/persist the game root, and update the in-memory canonical config.
- `src/Config/CMakeLists.txt`: compile ConfigDocument and link `gc_system_path`.
- `tests/Config/CMakeLists.txt`: add ConfigDocument and system-path orchestration targets.
- `tests/Config/ConfigFeatureTests.cpp`: enforce the new strict schema and root validation.
- `tools/ConfigGUI/Main.cpp`: replace three leaf inputs with one system-root input; keep shared production validation and canonical save.
- `src/Nesys/Registry/RegistryConfigOverride.cpp`: construct immutable service values from the derived paths.
- `tests/Nesys/Registry/RegistryConfigOverrideTests.cpp`: verify derived relative and absolute service values and invalid conversion rejection.
- `src/Platform/Win32/Hooking/MinHookTransaction.h`: raise the owned Kernel32 hook capacity to 32.
- `src/Platform/Win32/Hooking/MinHookTransaction.cpp`: make reusable transaction diagnostics feature-neutral.
- `src/Win32Hooks/Kernel32Hooks.h`: compose the system router and add `MoveFileA/W` originals, methods, and detours.
- `src/Win32Hooks/Kernel32Hooks.cpp`: build a deduplicated policy union and route the binary-observed Ttx path APIs.
- `src/Win32Hooks/CMakeLists.txt`: link `gc_system_path`.
- `tests/Win32Hooks/Kernel32HookTests.cpp`: verify policy-union counts, pass-through, A-to-W dispatch, last error, and move routing.
- `src/Rfid/Feature.h`: accept the prepared system root and expose a testable two-layer hook transaction.
- `src/Rfid/Feature.cpp`: own the router and Ttx guard beside the existing shared Kernel32 transaction and roll back partial setup.
- `src/Loader/DllMain.cpp`: prepare the game root before NESYS registry/launcher hooks and pass it into shared game-hook initialization.
- `tests/Rfid/CMakeLists.txt`: register the feature-layer rollback test.

---

### Task 1: Replace the three registry leaf paths with one authoritative root

**Files:**
- Modify: `src/Config/RegistryConfig.h:20-67`
- Modify: `src/Config/RegistryConfig.cpp:17-58`
- Modify: `src/Config/config.cpp:47-85`
- Modify: `config.toml:51-64`
- Modify: `tools/ConfigGUI/Main.cpp:988-1074`
- Modify: `tests/Config/ConfigFeatureTests.cpp:216-266,471-491`
- Modify: `src/Nesys/Registry/RegistryConfigOverride.h:36-49`
- Modify: `src/Nesys/Registry/RegistryConfigOverride.cpp:231-250`
- Modify: `tests/Nesys/Registry/RegistryConfigOverrideTests.cpp:390-405,515-531,998-1002`

**Interfaces:**
- Consumes: UTF-8 TOML strings, `GameCountry`, existing registry DWORD fields, and the service's `RegQueryValueExA` contract.
- Produces: `RegistryConfig::system_path`, `gc::registry_config::DerivedNesysPaths`, and `DeriveNesysPaths(std::string_view)` for immutable registry override construction.

- [ ] **Step 1: Write failing strict-schema and derivation tests**

Change the distributed-config expectations so canonical documents require `system_path`; a document containing `system_path` plus any old leaf remains invalid. Do not add a permanent assertion that a complete leaf-only legacy document is invalid, because Task 2 deliberately migrates that one compatibility shape before strict deserialization. Add production-facing derivation cases:

```cpp
const auto relative = gc::registry_config::DeriveNesysPaths(".\\system");
failures += Expect(
    relative &&
        relative->news == ".\\system\\DUA\\news" &&
        relative->event == ".\\system\\DUA\\event" &&
        relative->log == ".\\system\\CmdFile\\log",
    "relative registry paths derive from one root");

const auto absolute = gc::registry_config::DeriveNesysPaths("R:\\cabinet");
failures += Expect(
    absolute && absolute->news == "R:\\cabinet\\DUA\\news",
    "absolute registry paths derive from one root");

failures += Expect(
    !gc::registry_config::DeriveNesysPaths("C:\\😀").has_value(),
    "ANSI-incompatible absolute service root is rejected");
```

In `RegistryConfigOverrideTests.cpp`, configure only the root and independently expect the three leaves:

```cpp
RegistryConfig config{};
config.system_path = "R:\\cabinet";

constexpr std::array<StringCase, 3> string_cases{{
    {"NewsPath", "R:\\cabinet\\DUA\\news"},
    {"EventPath", "R:\\cabinet\\DUA\\event"},
    {"LogPath", "R:\\cabinet\\CmdFile\\log"},
}};
```

- [ ] **Step 2: Run focused tests to verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ConfigFeatureTests RegistryConfigOverrideTests
ctest --preset msvc32-debug -R "^(ConfigFeatureTests|RegistryConfigOverrideTests)$"
```

Expected: compilation fails because `system_path`, `DerivedNesysPaths`, and `DeriveNesysPaths` do not exist, while the old leaf members still do.

- [ ] **Step 3: Implement the strict root and lossless service derivation**

Use this public surface in `RegistryConfig.h`:

```cpp
struct RegistryNesysConfig {
    rfl::Rename<"game_kind", std::int64_t> game_kind = 303801;
    rfl::Rename<"event_next_time", std::int64_t> event_next_time = 900;
    rfl::Rename<"condition_time", std::int64_t> condition_time = 300;
    rfl::Rename<"log_level", std::int64_t> log_level = 3;
};

struct RegistryConfig {
    rfl::Rename<"enabled", bool> enabled = false;
    rfl::Rename<"system_path", std::string> system_path = "D:\\system";
    rfl::Rename<"game", RegistryGameConfig> game;
    rfl::Rename<"nesys", RegistryNesysConfig> nesys;
};

namespace gc::registry_config {
struct DerivedNesysPaths {
    std::string news;
    std::string event;
    std::string log;
};

[[nodiscard]] std::expected<DerivedNesysPaths, std::string>
DeriveNesysPaths(std::string_view system_path) noexcept;
}
```

Implementation rules:

1. Decode TOML UTF-8 with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ...)` into a native `std::filesystem::path`; do not use `path::string()` for Unicode conversion.
2. Append path components without lexical normalization so `.\system` remains explicit in registry strings.
3. Convert each native wide leaf with `WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, ...)` and reject conversion when `used_default_char != FALSE`.
4. Reject empty values and any derived ANSI leaf whose byte count excluding NUL exceeds 259.
5. Change `RegistryValidationResult` to one `system_path` result instead of three leaf results; have `ValidateInputConfig` return the derivation error text when conversion fails. The text must name the service's ANSI limitation and recommend an ASCII relative spelling such as `.\system`.
6. Have `CreateRegistryOverrideValues` call `DeriveNesysPaths` once and move the three derived strings into immutable state.

- [ ] **Step 4: Update the distributed config and ConfigGUI in the same schema commit**

Use this TOML shape:

```toml
[registry]
enabled = false
system_path = 'D:\system'

[registry.nesys]
game_kind = 303801
event_next_time = 900
condition_time = 300
log_level = 3
```

Replace the three GUI inputs with one shared control:

```cpp
auto& system_path = registry.system_path();
if (ImGui::InputText("Registry system path", &system_path)) {
    dirty = true;
}
const auto derived = gc::registry_config::DeriveNesysPaths(system_path);
if (!derived) {
    DrawInlineValidationError(false, derived.error().c_str());
}
ImGui::TextDisabled(
    "NewsPath, EventPath, and LogPath are derived from this root.");
```

Do not add GUI-only defaults or a second validation implementation.

- [ ] **Step 5: Run focused tests and build ConfigGUI to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target ConfigFeatureTests RegistryConfigOverrideTests ConfigGUI
ctest --preset msvc32-debug -R "^(ConfigFeatureTests|RegistryConfigOverrideTests)$"
```

Expected: both tests pass; `ConfigGUI.exe` builds with no references to the removed leaf members.

- [ ] **Step 6: Commit the authoritative schema**

```powershell
git add -- config.toml src/Config/RegistryConfig.h src/Config/RegistryConfig.cpp src/Config/config.cpp tools/ConfigGUI/Main.cpp tests/Config/ConfigFeatureTests.cpp src/Nesys/Registry/RegistryConfigOverride.h src/Nesys/Registry/RegistryConfigOverride.cpp tests/Nesys/Registry/RegistryConfigOverrideTests.cpp
git commit -m "Make registry system path authoritative"
```

---

### Task 2: Add structural legacy migration and atomic canonical persistence

**Files:**
- Create: `src/Config/ConfigDocument.h`
- Create: `src/Config/ConfigDocument.cpp`
- Modify: `src/Config/config.cpp:13-131`
- Modify: `src/Config/CMakeLists.txt:1-16`
- Create: `tests/Config/ConfigDocumentTests.cpp`
- Modify: `tests/Config/CMakeLists.txt:1-18`

**Interfaces:**
- Consumes: TOML source text, strict `InputConfig`, and a target `config.toml` path.
- Produces: `ParsedInputConfigDocument`, `ParseAndValidateInputConfigDocument`, `AtomicConfigWriteActions`, and `WriteInputConfigAtomically`.

- [ ] **Step 1: Write failing migration tests and register the target**

Create `ConfigDocumentTests.cpp`, register it in `tests/Config/CMakeLists.txt`, and build legacy inputs by transforming the distributed config: remove `system_path` and insert all three old leaves below `[registry.nesys]`. Cover these exact outcomes:

```cpp
const auto migrated = gc::config::ParseAndValidateInputConfigDocument(
    LegacyConfig(
        "D:\\system\\DUA\\news",
        "D:\\system\\DUA\\event",
        "D:\\system\\CmdFile\\log"));
failures += Expect(
    migrated && migrated->registry_paths_migrated &&
        migrated->config.registry().system_path() == "D:\\system",
    "legacy default leaves migrate to one root");

const auto custom = gc::config::ParseAndValidateInputConfigDocument(
    LegacyConfig(
        ".\\cabinet\\DUA\\news",
        ".\\cabinet\\DUA\\event",
        ".\\cabinet\\CmdFile\\log"));
failures += Expect(
    custom && custom->config.registry().system_path() == ".\\cabinet",
    "consistent relative legacy leaves preserve their root");

failures += ExpectFailure(
    LegacyConfig("N:\\news", "E:\\event", "L:\\log"),
    "do not share one system root");
failures += ExpectFailure(
    ConfigWithNewAndLegacyPaths(),
    "both system_path and legacy");
failures += ExpectFailure(
    LegacyConfigMissing("event_path"),
    "legacy registry paths must be complete");
```

Also prove unknown nonlegacy fields remain strict errors after migration.

- [ ] **Step 2: Write failing atomic-write action tests**

Use injected actions, not directory permissions, to independently exercise each state transition:

```cpp
struct AtomicFake {
    bool fail_write{};
    bool fail_replace{};
    int writes{};
    int replaces{};
    int removes{};
    std::filesystem::path temporary;
    std::string serialized;
};

const auto success = gc::config::WriteInputConfigAtomically(
    L"X:\\cabinet\\config.toml", config, AtomicActions(fake));
failures += Expect(
    success && fake.writes == 1 && fake.replaces == 1 &&
        fake.removes == 0 &&
        fake.temporary.parent_path() == L"X:\\cabinet" &&
        fake.serialized.find("system_path") != std::string::npos &&
        fake.serialized.find("news_path") == std::string::npos,
    "atomic writer commits one canonical sibling replacement");
```

For write failure, require zero replacements and one cleanup attempt. For replacement failure, require one write, one replacement, and one cleanup attempt. Require the returned error to name the failed stage and target path.

- [ ] **Step 3: Run the new test target to verify RED**

```powershell
cmake --build --preset msvc32-debug --target ConfigDocumentTests
```

Expected: compilation fails because `ConfigDocument.h` and its production interfaces do not exist; an unknown test target is a CMake setup defect and must be corrected before continuing.

- [ ] **Step 4: Implement migrated-document parsing before strict deserialization**

Create this exact public contract:

```cpp
namespace gc::config {
struct ParsedInputConfigDocument {
    InputConfig config;
    bool registry_paths_migrated{};
};

[[nodiscard]] std::expected<ParsedInputConfigDocument, std::string>
ParseAndValidateInputConfigDocument(std::string_view text);
}
```

Implementation sequence:

```cpp
auto syntax = ParseTomlSyntax(text);
if (!syntax) {
    return std::unexpected(syntax.error());
}
auto migration = MigrateLegacyRegistryPaths(*syntax);
if (!migration) {
    return std::unexpected(migration.error());
}
std::ostringstream canonical_text;
canonical_text << *syntax;
auto parsed = rfl::toml::read<InputConfig>(canonical_text.str());
if (!parsed) {
    return std::unexpected(
        "Failed to parse config file: " + parsed.error().what());
}
auto validated = ValidateInputConfig(parsed.value());
if (!validated) {
    return std::unexpected(validated.error());
}
return ParsedInputConfigDocument{
    .config = std::move(parsed.value()),
    .registry_paths_migrated = *migration,
};
```

Define the internal helper as `std::expected<bool, std::string> MigrateLegacyRegistryPaths(toml::table&)`. It must compare Windows path components case-insensitively, strip exactly `DUA/news`, `DUA/event`, and `CmdFile/log`, and remove all three old TOML keys only after one common root is proven. Preserve the existing obsolete-input and obsolete-framerate checks. Keep `ParseAndValidateInputConfig` as a compatibility wrapper that returns only `.config`.

- [ ] **Step 5: Implement injected atomic replacement with production Win32 actions**

Declare:

```cpp
struct AtomicConfigWriteActions {
    void* context{};
    std::expected<void, std::string> (*write)(
        void*, const std::filesystem::path&, std::string_view) noexcept{};
    std::expected<void, std::string> (*replace)(
        void*, const std::filesystem::path& destination,
        const std::filesystem::path& replacement) noexcept{};
    void (*remove)(void*, const std::filesystem::path&) noexcept{};
};

[[nodiscard]] std::expected<void, std::string>
WriteInputConfigAtomically(
    const std::filesystem::path& config_path,
    const InputConfig& config,
    AtomicConfigWriteActions actions =
        ProductionAtomicConfigWriteActions()) noexcept;
```

Production `write` opens the unique sibling temporary path in binary truncate mode, writes `rfl::toml::write(config)`, flushes, closes, and checks all stream states. Production `replace` calls `ReplaceFileW(destination, replacement, nullptr, 0, nullptr, nullptr)`; do not pass the unsupported `REPLACEFILE_WRITE_THROUGH` flag. On either failure, remove only the temporary path created by this call. Catch every exception and return an error string.

- [ ] **Step 6: Run focused tests to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target ConfigDocumentTests ConfigFeatureTests ConfigGUI
ctest --preset msvc32-debug -R "^(ConfigDocumentTests|ConfigFeatureTests)$"
```

Expected: legacy fixtures migrate in memory, ambiguity remains fatal, atomic action ordering passes, and the canonical distributed config remains strict.

- [ ] **Step 7: Commit document migration and persistence**

```powershell
git add -- src/Config/ConfigDocument.h src/Config/ConfigDocument.cpp src/Config/config.cpp src/Config/CMakeLists.txt tests/Config/ConfigDocumentTests.cpp tests/Config/CMakeLists.txt
git commit -m "Add registry path config migration"
```

---

### Task 3: Resolve and preflight the complete system-root tree

**Files:**
- Create: `src/SystemPath/CMakeLists.txt`
- Create: `src/SystemPath/SystemRoot.h`
- Create: `src/SystemPath/SystemRoot.cpp`
- Modify: `src/CMakeLists.txt:1-12`
- Create: `tests/SystemPath/CMakeLists.txt`
- Create: `tests/SystemPath/SystemRootTests.cpp`
- Modify: `tests/CMakeLists.txt:1-10`

**Interfaces:**
- Consumes: registry-enabled state, configured UTF-8 root, config directory, and injectable directory-creation actions.
- Produces: `gc::system_path::PreparedRoot`, `PrepareGameSystemRoot`, required-tree constants, and structured `RootPrepareError`.

- [ ] **Step 1: Write failing root-preparation tests and register the target**

Create the `tests/SystemPath` CMake entry and `SystemRootTests` target, then use a fake that records every native path and can fail a selected root. Cover the approved matrix:

```cpp
const auto fallback = PrepareGameSystemRoot(
    {
        .registry_enabled = true,
        .configured_path = "D:\\system",
        .config_directory = L"H:\\遊戲",
    },
    DirectoryActions(fake));
failures += Expect(
    fallback && fallback->configured_path_changed &&
        fallback->runtime.configured_path == ".\\system" &&
        fallback->runtime.resolved_path == L"H:\\遊戲\\system" &&
        fallback->runtime.redirect_enabled,
    "unavailable shipped default falls back beside config");

const auto relative = PrepareGameSystemRoot(
    {
        .registry_enabled = true,
        .configured_path = ".\\custom",
        .config_directory = L"H:\\遊戲",
    },
    DirectoryActions(success));
failures += Expect(
    relative && relative->runtime.resolved_path == L"H:\\遊戲\\custom" &&
        relative->runtime.redirect_enabled,
    "relative custom root resolves once against config directory");
```

Also require:

- available/creatable `D:\system` stays configured and does not redirect;
- an unavailable custom absolute or relative root fails without probing fallback;
- disabled mode ignores the configured alternate root, reports the effective runtime spelling as `D:\system`, probes real `D:\system`, never marks config changed, and fails explicitly if real D is unavailable;
- every successful root creates `CmdFile/log`, `DUA/data`, `DUA/decrypt`, `DUA/download`, `DUA/event`, `DUA/news`, `DUA/unpack`, and `DUA/work`;
- an action returning `false` with a clear `std::error_code` is accepted as an already-existing directory, matching `std::filesystem::create_directories`;
- case/slash- and lexical-equivalent spellings such as `d:/SYSTEM/.` and `D:\temp\..\system` count as the shipped default;
- an empty or invalid UTF-8 configured path returns `RootPrepareStage::invalid_configured_path` without calling the filesystem fake.

- [ ] **Step 2: Run the new target to verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target SystemRootTests
```

Expected: configure or compilation fails because `gc_system_path`, `SystemRoot.h`, and the production contract do not exist; an unknown test target is not the intended failure.

- [ ] **Step 3: Add the runtime root and preparation contract**

Use these exact types:

```cpp
namespace gc::system_path {
inline constexpr std::wstring_view kLogicalSystemRoot = L"D:\\system";
inline constexpr std::string_view kDefaultConfiguredPath = "D:\\system";
inline constexpr std::string_view kFallbackConfiguredPath = ".\\system";

struct RuntimeRoot {
    std::string configured_path;
    std::filesystem::path resolved_path;
    bool redirect_enabled{};
};

enum class RootPrepareStage {
    invalid_configured_path,
    configured_tree,
    fallback_tree,
};

struct RootPrepareError {
    RootPrepareStage stage{};
    std::filesystem::path path;
    std::error_code error;
    bool registry_enabled{};
    bool configured_was_default{};
};

struct RootPrepareRequest {
    bool registry_enabled{};
    std::string_view configured_path;
    std::filesystem::path config_directory;
};

struct PreparedRoot {
    RuntimeRoot runtime;
    bool configured_path_changed{};
};

struct DirectoryActions {
    void* context{};
    bool (*create_directories)(
        void*, const std::filesystem::path&, std::error_code&) noexcept{};
};

[[nodiscard]] std::expected<PreparedRoot, RootPrepareError>
PrepareGameSystemRoot(
    RootPrepareRequest request,
    DirectoryActions actions = ProductionDirectoryActions()) noexcept;
}
```

- [ ] **Step 4: Implement default-only fallback and the complete directory tree**

Convert UTF-8 explicitly to a native path, resolve relative paths against `config_directory`, normalize lexically, and call the injected action for each required leaf. Use this decision order:

```cpp
auto EnsureTree(RuntimeRoot runtime, bool configured_path_changed)
    -> std::expected<PreparedRoot, RootPrepareError>;

if (!request.registry_enabled) {
    return EnsureTree(
        RuntimeRoot{
            .configured_path = std::string{kDefaultConfiguredPath},
            .resolved_path = std::filesystem::path{kLogicalSystemRoot},
            .redirect_enabled = false,
        },
        false);
}

auto configured = EnsureTree(configured_runtime, false);
if (configured) {
    return configured;
}
if (!configured_is_default) {
    return std::unexpected(configured.error());
}
return EnsureTree(
    RuntimeRoot{
        .configured_path = std::string{kFallbackConfiguredPath},
        .resolved_path =
            (request.config_directory / L"system").lexically_normal(),
        .redirect_enabled = true,
    },
    true);
```

Set `configured_runtime.redirect_enabled` only when the enabled-mode resolved root is not equivalent to `D:\system`; custom absolute and relative roots therefore route, while the available shipped default is a no-op. Pass `false` as the second `EnsureTree` argument for the configured-root probe. For each leaf, clear the error code, call `create_directories`, and treat only a nonzero error code as failure—the Boolean return is false when the directory already exists. The fallback sets configured spelling exactly to `.\system`, `configured_path_changed = true`, and `redirect_enabled = true`. Return the first concrete failed path and error code; do not log inside the pure resolver.

- [ ] **Step 5: Run root tests to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target SystemRootTests
ctest --preset msvc32-debug -R "^SystemRootTests$"
```

Expected: all matrix and required-tree assertions pass without touching a real `D:` drive.

- [ ] **Step 6: Commit root preparation**

```powershell
git add -- src/SystemPath/CMakeLists.txt src/SystemPath/SystemRoot.h src/SystemPath/SystemRoot.cpp src/CMakeLists.txt tests/SystemPath/CMakeLists.txt tests/SystemPath/SystemRootTests.cpp tests/CMakeLists.txt
git commit -m "Add system root preparation"
```

---

### Task 4: Orchestrate enabled-mode persistence and fail startup explicitly

**Files:**
- Modify: `src/Config/config.h:109-227`
- Modify: `src/Config/config.cpp:135-163`
- Modify: `src/Config/ConfigDocument.h`
- Modify: `src/Config/ConfigDocument.cpp`
- Create: `tests/Config/SystemPathConfigTests.cpp`
- Modify: `tests/Config/CMakeLists.txt`
- Create: `src/SystemPath/StartupFatal.h`
- Create: `src/SystemPath/StartupFatal.cpp`
- Modify: `src/SystemPath/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp:23-141`
- Modify: `src/CMakeLists.txt:45-67`

**Interfaces:**
- Consumes: parsed-document migration metadata, `PrepareGameSystemRoot`, atomic writer actions, process role, and existing startup logging.
- Produces: `PrepareAndPersistGameSystemPathConfiguration`, `ConfigManager::PrepareGameSystemPath`, and reusable one-shot `PublishStartupFatal` behavior.

- [ ] **Step 1: Write failing preparation-and-persistence transaction tests**

Create `SystemPathConfigTests.cpp` around the injected transaction function. Require:

```cpp
const auto enabled_migration = PrepareAndPersistGameSystemPathConfiguration(
    migrated_config,
    true,
    L"H:\\game\\config.toml",
    GameSystemPathPreparationActions{
        .directories = DirectoryActions(available),
        .config_write = AtomicActions(writer_success),
    });
failures += Expect(
    enabled_migration && enabled_migration->persisted &&
        writer_success.replaces == 1 &&
        enabled_migration->config.registry().system_path() == "D:\\system",
    "enabled legacy migration is persisted before publication");

const auto disabled_migration = PrepareAndPersistGameSystemPathConfiguration(
    disabled_migrated_config,
    true,
    L"H:\\game\\config.toml",
    GameSystemPathPreparationActions{
        .directories = DirectoryActions(available_d),
        .config_write = AtomicActions(writer_disabled),
    });
failures += Expect(
    disabled_migration && !disabled_migration->persisted &&
        writer_disabled.writes == 0 && writer_disabled.replaces == 0,
    "disabled legacy parsing never invokes persistence");

const auto fallback = PrepareAndPersistGameSystemPathConfiguration(
    default_config,
    false,
    L"H:\\game\\config.toml",
    GameSystemPathPreparationActions{
        .directories = DirectoryActions(default_fails_fallback_succeeds),
        .config_write = AtomicActions(writer_fallback),
    });
failures += Expect(
    fallback && fallback->persisted && writer_fallback.replaces == 1 &&
        fallback->config.registry().system_path() == ".\\system" &&
        writer_fallback.serialized.find("system_path") != std::string::npos &&
        writer_fallback.serialized.find(".\\\\system") != std::string::npos,
    "enabled fallback persists explicit relative spelling before return");
```

Add an atomic-replacement failure case. It must return `std::unexpected`, expose no prepared config to publish, and report the persistence stage and config path. Independently require that the disabled-mode case validates real `D:\system` while leaving the migrated input spelling and writer untouched.

Register `SystemPathConfigTests` in `tests/Config/CMakeLists.txt` before the RED build.

- [ ] **Step 2: Write failing fatal-publication tests**

Use injected actions and a local `std::atomic_bool` latch:

```cpp
PublishStartupFatal(
    latch,
    "System path preparation failed stage=configured_tree error=5",
    L"The configured system path is unavailable. Use .\\system or fix permissions.",
    21,
    FatalActions(fake));
PublishStartupFatal(latch, "duplicate", L"duplicate", 21, FatalActions(fake));

failures += Expect(
    fake.logs == 1 && fake.modals == 1 && fake.terminations == 1 &&
        fake.fail_fast_calls == 1 && fake.exit_code == 21,
    "startup fatal is one-shot and exhausts termination fallbacks");
```

The fake callbacks deliberately return so the test can observe all four actions. Production termination does not return.

- [ ] **Step 3: Run the focused targets to verify RED**

```powershell
cmake --build --preset msvc32-debug --target SystemPathConfigTests SystemRootTests
```

Expected: FAIL because the orchestration and fatal publication interfaces do not exist.

- [ ] **Step 4: Implement the ConfigManager preparation transaction**

Add this complete transaction surface to `ConfigDocument.h`, which already owns `InputConfig` document persistence and can include `SystemPath/SystemRoot.h` without making `config.h` include itself:

```cpp
namespace gc::config {
struct GameSystemPathPreparationActions {
    gc::system_path::DirectoryActions directories;
    AtomicConfigWriteActions config_write;
};

[[nodiscard]] GameSystemPathPreparationActions
ProductionGameSystemPathPreparationActions() noexcept;

struct PreparedGameSystemPathConfig {
    InputConfig config;
    gc::system_path::RuntimeRoot runtime;
    bool persisted{};
};

[[nodiscard]] std::expected<PreparedGameSystemPathConfig, std::string>
PrepareAndPersistGameSystemPathConfiguration(
    InputConfig config,
    bool registry_schema_migrated,
    const std::filesystem::path& config_path,
    GameSystemPathPreparationActions actions =
        ProductionGameSystemPathPreparationActions()) noexcept;
}

class ConfigManager {
public:
    [[nodiscard]] std::expected<gc::system_path::RuntimeRoot, std::string>
    PrepareGameSystemPath() noexcept;
private:
    std::filesystem::path config_path_;
    bool registry_schema_migrated_{};
    InputConfig config;
};
```

Only the `ConfigManager` declaration belongs in `config.h`; it includes `SystemPath/SystemRoot.h` for the method result but does not include `ConfigDocument.h`. `config.cpp` includes both headers. This keeps the ownership direction `ConfigDocument.h -> config.h` and avoids an include cycle.

The transaction performs these operations in order:

1. Call `PrepareGameSystemRoot` using `config.registry().enabled()`, the configured spelling, and `config_path.parent_path()`.
2. If enabled-mode fallback changed the spelling, update the copied `InputConfig` to exactly `.\system`.
3. Set `must_persist` only when registry virtualization is enabled and either schema migration or fallback changed the file.
4. If `must_persist`, call `WriteInputConfigAtomically` before constructing the successful result.
5. Return the updated copy, prepared runtime root, and `.persisted = must_persist`; catch all exceptions and return a contextual error.

`ConfigManager` must parse with `ParseAndValidateInputConfigDocument`. `PrepareGameSystemPath()` calls this transaction on a copy, assigns the returned `config` member only after success, and then clears migration metadata. A writer failure therefore cannot publish an in-memory value that the NESYS child would not read from disk.

Format every root error with configured path, failed native path, numeric/system error, and one of these fixes:

- disabled: create a writable `D:\system` or enable registry overrides;
- custom: correct `[registry].system_path` or permissions;
- default fallback: make the config directory writable or set `.\system` manually.

- [ ] **Step 5: Implement reusable fatal publication and game-only startup order**

Use this action surface:

```cpp
struct StartupFatalActions {
    void* context{};
    void (*log_error)(void*, const char*) noexcept{};
    void (*show_error)(void*, const wchar_t*, const wchar_t*) noexcept{};
    void (*terminate_process)(void*, DWORD) noexcept{};
    void (*fail_fast)(void*) noexcept{};
};

void PublishStartupFatal(
    std::atomic_bool& latch,
    std::string_view log,
    std::wstring_view modal,
    DWORD exit_code,
    StartupFatalActions actions = ProductionStartupFatalActions()) noexcept;
```

Implement the one-shot gate with `if (latch.exchange(true, std::memory_order_acq_rel)) return;`. The production actions write one `PLOG_ERROR`, show `MessageBoxW`, call `TerminateProcess(GetCurrentProcess(), exit_code)`, and finally call `RaiseFailFastException(nullptr, nullptr, 0)` if termination unexpectedly returns. Materialize null-terminated text inside guarded code and fall back to fixed ASCII/wide literals if formatting or allocation fails; every action adapter remains `noexcept`.

In `DllMain`, retain the prepared root in game-role attach scope and run preparation before `NesysServicePatchInit`:

```cpp
auto& config = ConfigManager::instance();
ApplyConfiguredLogLevel(config);

std::optional<gc::system_path::RuntimeRoot> system_root;
if (gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
    auto prepared = config.PrepareGameSystemPath();
    if (!prepared) {
        PublishSystemPathPreparationFatal(prepared.error());
        return FALSE;
    }
    system_root = std::move(*prepared);
}

if (!gc::nesys_service::NesysServicePatchInit(hModule, role)) {
    return FALSE;
}
```

Change `ApplyConfiguredLogLevel` to accept `const ConfigManager&` so it does not reacquire the singleton. Do not run root preparation in the NESYS process.

- [ ] **Step 6: Run focused config, root, and NESYS tests to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target SystemPathConfigTests SystemRootTests ConfigDocumentTests ConfigFeatureTests NesysServicePatchTests
ctest --preset msvc32-debug -R "^(SystemPathConfigTests|SystemRootTests|ConfigDocumentTests|ConfigFeatureTests|NesysServicePatchTests)$"
```

Expected: enabled changes complete persistence before returning, disabled mode never writes, all root/config/NESYS regressions pass, and fatal publication is one-shot. Inspect the resulting `DllMain` diff in this step to confirm the preparation call textually precedes `NesysServicePatchInit`; do not add a production orchestration abstraction solely to test source order.

- [ ] **Step 7: Commit startup preparation**

```powershell
git add -- src/Config/config.h src/Config/config.cpp src/Config/ConfigDocument.h src/Config/ConfigDocument.cpp tests/Config/SystemPathConfigTests.cpp tests/Config/CMakeLists.txt src/SystemPath/StartupFatal.h src/SystemPath/StartupFatal.cpp src/SystemPath/CMakeLists.txt src/Loader/DllMain.cpp src/CMakeLists.txt
git commit -m "Prepare system path before game startup"
```

---

### Task 5: Build the Unicode system-path router as a pure component

**Files:**
- Create: `src/SystemPath/SystemPathRouter.h`
- Create: `src/SystemPath/SystemPathRouter.cpp`
- Modify: `src/SystemPath/CMakeLists.txt`
- Create: `tests/SystemPath/SystemPathRouterTests.cpp`
- Modify: `tests/SystemPath/CMakeLists.txt`

**Interfaces:**
- Consumes: a prepared `RuntimeRoot` and nullable ANSI/wide Win32 path inputs.
- Produces: `gc::system_path::SystemPathRouter`, `RouteResult`, `RoutePathA`, `RoutePathW`, and lossless ANSI-to-native conversion for Kernel32 detours.

- [ ] **Step 1: Write failing exact-match and Unicode-route tests**

Create and register `SystemPathRouterTests`, then construct a router with an enabled Unicode destination and independently verify matching:

```cpp
SystemPathRouter router{
    RuntimeRoot{
        .configured_path = ".\\system",
        .resolved_path = L"H:\\遊戲\\system",
        .redirect_enabled = true,
    }};

const auto exact = router.RoutePathW(L"D:\\system");
const auto mixed = router.RoutePathW(L"d:/SYSTEM/DUA/work/file.bin");
failures += Expect(
    exact && exact->matched && exact->path == L"H:\\遊戲\\system" &&
        mixed && mixed->matched &&
        mixed->path == L"H:\\遊戲\\system\\DUA\\work\\file.bin",
    "wide logical system paths route by components");
```

Require all of these to return `{.matched = false}` without error:

```text
nullptr
D:\system2
D:\system-file
D:\system\..\outside
C:\system
D:system
\\?\D:\system
.\system
D:\0123456789abcdef0123456789abcdef_000\TestModeFile
```

Add ANSI cases for exact/descendant matching, active-code-page conversion, and a malformed input that returns `ERROR_NO_UNICODE_TRANSLATION`. Add a disabled router case proving every input passes through.

- [ ] **Step 2: Run the router target to verify RED**

```powershell
cmake --build --preset msvc32-debug --target SystemPathRouterTests
```

Expected: compilation fails because `SystemPathRouter` does not exist; an unknown test target is a CMake setup defect.

- [ ] **Step 3: Implement the non-throwing route contract**

Use this public surface:

```cpp
namespace gc::system_path {
struct RouteResult {
    bool matched{};
    std::filesystem::path path;
};

class SystemPathRouter {
public:
    explicit SystemPathRouter(RuntimeRoot root) noexcept;

    [[nodiscard]] std::expected<RouteResult, DWORD>
    RoutePathA(LPCSTR path) const noexcept;
    [[nodiscard]] std::expected<RouteResult, DWORD>
    RoutePathW(LPCWSTR path) const noexcept;
    [[nodiscard]] std::expected<std::filesystem::path, DWORD>
    ConvertAnsiPath(LPCSTR path) const noexcept;
    [[nodiscard]] bool enabled() const noexcept;

private:
    RuntimeRoot root_;
};
}
```

Implementation requirements:

1. Return unmatched immediately for `nullptr`, disabled routing, a non-rooted path, a drive other than `D:`, or a first rooted directory component other than case-insensitive `system`.
2. Parse with Windows path components and `lexically_normal`; never use a raw prefix comparison that could capture `system2`, and reject a normalized path that escapes above the logical `system` component.
3. Append only components after the logical `system` component to the already-absolute destination.
4. Convert matching ANSI input with `MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, ...)` before constructing the native path.
5. Catch `std::bad_alloc`, `std::filesystem::filesystem_error`, and all other exceptions; return `ERROR_NOT_ENOUGH_MEMORY`, the filesystem error code when available, or `ERROR_INVALID_NAME`.
6. Do not log successful, unmatched, or failed per-call routing from this pure component.

- [ ] **Step 4: Run router and root tests to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target SystemPathRouterTests SystemRootTests
ctest --preset msvc32-debug -R "^(SystemPathRouterTests|SystemRootTests)$"
```

Expected: all component-boundary, Unicode, conversion-error, and disabled-route cases pass.

- [ ] **Step 5: Commit the router**

```powershell
git add -- src/SystemPath/SystemPathRouter.h src/SystemPath/SystemPathRouter.cpp src/SystemPath/CMakeLists.txt tests/SystemPath/SystemPathRouterTests.cpp tests/SystemPath/CMakeLists.txt
git commit -m "Add system path router"
```

---

### Task 6: Compose system routing into the existing Kernel32 MinHook layer

**Files:**
- Modify: `src/Platform/Win32/Hooking/MinHookTransaction.h:10-94`
- Modify: `src/Platform/Win32/Hooking/MinHookTransaction.cpp:45-177`
- Modify: `src/Win32Hooks/Kernel32Hooks.h:1-120`
- Modify: `src/Win32Hooks/Kernel32Hooks.cpp:1-560`
- Modify: `src/Win32Hooks/CMakeLists.txt`
- Modify: `tests/Win32Hooks/Kernel32HookTests.cpp:289-624,857-1035,1288-1400`
- Modify: `src/Rfid/Feature.h:1-30`
- Modify: `src/Rfid/Feature.cpp:20-162`
- Modify: `src/CMakeLists.txt:14-27`
- Modify: `src/Loader/DllMain.cpp:92-136`

**Interfaces:**
- Consumes: prepared `RuntimeRoot`, `SystemPathRouter`, existing RFID/JVS runtime, test-mode storage policy, and `MinHookTransaction`.
- Produces: one deduplicated Kernel32 request union, routed binary-observed Ttx APIs, `MoveFileA/W` detours, and `InitializeFeature(const RuntimeRoot&)`.

- [ ] **Step 1: Extend the fake original API and write failing request-union tests**

Add `move_file_a` and `move_file_w` to `OriginalCall`, `OriginalKernel32Api`, and the fake recorder. Build four policy combinations and require these exact export counts:

```cpp
Kernel32Hooks no_paths{runtime, storage_disabled, router_disabled, OriginalApi()};
Kernel32Hooks storage_only{runtime, storage_enabled, router_disabled, OriginalApi()};
Kernel32Hooks system_only{runtime, storage_disabled, router_enabled, OriginalApi()};
Kernel32Hooks both{runtime, storage_enabled, router_enabled, OriginalApi()};

failures += expect(no_paths.BuildRequests().requests().size() == 14,
                   "RFID-only request count");
failures += expect(storage_only.BuildRequests().requests().size() == 24,
                   "test-mode storage request count");
failures += expect(system_only.BuildRequests().requests().size() == 22,
                   "system-routing request count");
failures += expect(both.BuildRequests().requests().size() == 26,
                   "combined request union count");
```

For every set, independently require unique export names. The combined set must contain `MoveFileA` and `MoveFileW` exactly once and remain below `kMaxOwnedKernel32Hooks == 32`.

- [ ] **Step 2: Write failing routed API tests**

Exercise the production dispatch methods with a destination containing non-ASCII components:

```cpp
const auto created = hooks.CreateFileA(
    "D:\\system\\DUA\\data\\state.bin",
    GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
failures += expect(
    created == reinterpret_cast<HANDLE>(0x8102) &&
        original.calls[call_index(OriginalCall::create_file_a)] == 0 &&
        original.calls[call_index(OriginalCall::create_file_w)] == 1 &&
        original.path_w == L"H:\\遊戲\\system\\DUA\\data\\state.bin",
    "matching CreateFileA uses Unicode original trampoline");
```

Add observable cases for:

- `CreateFileW`, `FindFirstFileW`, and `CreateDirectoryW` routed to the W originals;
- `DeleteFileA/W` and `GetFileAttributesA/W` routed, with A calls using W originals;
- `MoveFileA/W` with neither, source-only, destination-only, and both operands matching, plus a matching operand paired with a null operand;
- `COM2` still intercepted before any path policy;
- test-mode hash paths still use their existing A/W originals;
- `D:\system2` and null pointers pass through unchanged;
- the last error set by each selected original trampoline remains visible;
- a successful pass-through original that deliberately leaves last error untouched sees and preserves the caller's incoming last error despite policy inspection;
- conversion failure returns each API's native failure sentinel and sets the router error.

- [ ] **Step 3: Run Kernel32HookTests to verify RED**

```powershell
cmake --build --preset msvc32-debug --target Kernel32HookTests
ctest --preset msvc32-debug -R "^Kernel32HookTests$"
```

Expected: compilation fails because the router constructor dependency and `MoveFileA/W` surface are absent; request counts still reflect the old 24-hook maximum.

- [ ] **Step 4: Make the MinHook transaction generic and raise its fixed capacity**

Change:

```cpp
inline constexpr std::size_t kMaxOwnedKernel32Hooks = 32;
```

Replace low-level prefixes such as `RFID hooks:` with `MinHookTransaction:`. Keep the feature-level installation error in `Feature.cpp`, but label it `Game Kernel32 hooks:` and include active RFID/storage/system policy booleans. Do not add success-path per-export logs.

- [ ] **Step 5: Build the unique request union**

Append these two fields to `OriginalKernel32Api`, then add the router constructor dependency:

```cpp
decltype(&::MoveFileA) move_file_a{};
decltype(&::MoveFileW) move_file_w{};

Kernel32Hooks(
    gc::rfid::Runtime& rfid,
    gc::testmode_storage::Hooks& storage,
    gc::system_path::SystemPathRouter& system,
    OriginalKernel32Api originals = {}) noexcept;

[[nodiscard]] HookRequestSet BuildRequests() noexcept;
```

Append every always-on RFID export first. Then append each path export once when either owning policy needs it:

```cpp
const bool storage = storage_.enabled();
const bool system = system_.enabled();
append_if(storage, "FindFirstFileA", FindFirstFileADetour,
          &originals_.find_first_file_a);
append_if(storage || system, "FindFirstFileW", FindFirstFileWDetour,
          &originals_.find_first_file_w);
append_if(storage, "CreateDirectoryA", CreateDirectoryADetour,
          &originals_.create_directory_a);
append_if(storage || system, "CreateDirectoryW", CreateDirectoryWDetour,
          &originals_.create_directory_w);
append_if(storage || system, "DeleteFileA", DeleteFileADetour,
          &originals_.delete_file_a);
append_if(storage || system, "DeleteFileW", DeleteFileWDetour,
          &originals_.delete_file_w);
append_if(storage || system, "GetFileAttributesA", GetFileAttributesADetour,
          &originals_.get_file_attributes_a);
append_if(storage || system, "GetFileAttributesW", GetFileAttributesWDetour,
          &originals_.get_file_attributes_w);
append_if(storage, "GetDiskFreeSpaceExA", GetDiskFreeSpaceExADetour,
          &originals_.get_disk_free_space_ex_a);
append_if(storage, "GetDiskFreeSpaceExW", GetDiskFreeSpaceExWDetour,
          &originals_.get_disk_free_space_ex_w);
append_if(system, "MoveFileA", MoveFileADetour,
          &originals_.move_file_a);
append_if(system, "MoveFileW", MoveFileWDetour,
          &originals_.move_file_w);
```

Keep `CreateFileA/W` in the always-on set because RFID already owns them.

- [ ] **Step 6: Route each API and preserve pass-through semantics**

After the existing `COM2` interception, use this dispatch pattern for ANSI APIs with compatible W counterparts:

```cpp
const DWORD incoming_last_error = GetLastError();
const auto system = system_.RoutePathA(file_name);
if (!system) {
    SetLastError(system.error());
    return INVALID_HANDLE_VALUE;
}
if (system->matched) {
    SetLastError(incoming_last_error);
    return originals_.create_file_w(
        system->path.c_str(), desired_access, share_mode,
        security_attributes, creation_disposition,
        flags_and_attributes, template_file);
}
const auto storage = storage_.RoutePathA(file_name);
SetLastError(incoming_last_error);
return originals_.create_file_a(
    storage.get(), desired_access, share_mode, security_attributes,
    creation_disposition, flags_and_attributes, template_file);
```

Restore `incoming_last_error` immediately before every selected original trampoline, including matching W dispatch, so conversions and policy probes cannot perturb success cases where the vendor original leaves last error untouched. For `MoveFileA`, route both operands first. If either matches, convert each nonmatching, non-null ANSI operand with `SystemPathRouter::ConvertAnsiPath`, preserve a null operand as null, and call `originals_.move_file_w`; otherwise call `originals_.move_file_a` with the original pointers. `MoveFileW` follows the same two-operand decision without ANSI conversion and likewise preserves null. Add static detours for both APIs. Every detour must catch all exceptions and return the correct failure sentinel with a stable last error. An ordinary failure returned by an original filesystem API remains an ordinary API result; only `TtxUDLInit` returning zero crosses the explicit startup-fatal boundary.

- [ ] **Step 7: Give the shared game hook owner the prepared root**

Change:

```cpp
[[nodiscard]] std::expected<void, FeatureError>
InitializeFeature(const gc::system_path::RuntimeRoot& system_root) noexcept;
```

Construct `SystemPathRouter` in `FeatureState`, pass it into `Kernel32Hooks`, and call `BuildRequests()` without a duplicated policy argument. In `DllMain`, require the game-only `system_root` optional to be engaged before calling `InitializeFeature(*system_root)`. The NESYS branch must continue skipping the entire game-only feature.

- [ ] **Step 8: Run focused hook and feature builds to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target Kernel32HookTests iDmacDrv32
ctest --preset msvc32-debug -R "^Kernel32HookTests$"
```

Expected: all four request unions, routing/forwarding behavior, last-error assertions, and the game DLL build pass.

- [ ] **Step 9: Commit composed Kernel32 routing**

```powershell
git add -- src/Platform/Win32/Hooking/MinHookTransaction.h src/Platform/Win32/Hooking/MinHookTransaction.cpp src/Win32Hooks/Kernel32Hooks.h src/Win32Hooks/Kernel32Hooks.cpp src/Win32Hooks/CMakeLists.txt tests/Win32Hooks/Kernel32HookTests.cpp src/Rfid/Feature.h src/Rfid/Feature.cpp src/CMakeLists.txt src/Loader/DllMain.cpp
git commit -m "Route Ttx system paths through Kernel32 hooks"
```

---

### Task 7: Guard the exact Ttx initializer and roll back partial hook setup

**Files:**
- Create: `src/SystemPath/TtxInitGuard.h`
- Create: `src/SystemPath/TtxInitGuard.cpp`
- Modify: `src/SystemPath/CMakeLists.txt`
- Create: `tests/SystemPath/TtxInitGuardTests.cpp`
- Modify: `tests/SystemPath/CMakeLists.txt`
- Modify: `src/Rfid/Feature.h`
- Modify: `src/Rfid/Feature.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Create: `tests/Rfid/FeatureHookLayerTests.cpp`
- Modify: `tests/Rfid/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: exact Ttx module/export identity, verified cdecl ABI, prepared root, SafetyHook, `PublishStartupFatal`, and the committed MinHook transaction.
- Produces: owning `TtxInitGuard`, testable install/runtime action seams, structured install errors, and `InstallFeatureHookLayers` rollback orchestration.

- [ ] **Step 1: Write failing ABI-forwarding and fatal-boundary tests**

Create and register `TtxInitGuardTests`, then use the exact verified ABI constants:

```cpp
static_assert(std::is_same_v<
    gc::system_path::TtxUdlInitFn,
    int(__cdecl*)(unsigned int, unsigned int, unsigned int, unsigned int)>);
failures += Expect(
    std::string_view{gc::system_path::kTtxUdlInitExport} ==
        "?TtxUDLInit@@YAHKKKK@Z",
    "Ttx guard uses observed decorated export");
```

The runtime fake records all four arguments and returns either one or zero:

```cpp
const int success = InvokeTtxUdlInitGuard(
    3, 471, 9, 0x20, root, RuntimeActions(original_success));
failures += Expect(
    success == 1 && fake.args == std::array<unsigned int, 4>{3, 471, 9, 0x20} &&
        fake.get_last_error_calls == 0 && fake.publish_calls == 0,
    "successful Ttx init returns unchanged");

const int failure = InvokeTtxUdlInitGuard(
    3, 471, 9, 0x20, root, RuntimeActions(original_failure));
failures += Expect(
    failure == 0 && fake.get_last_error_calls == 1 &&
        fake.captured_error == ERROR_PATH_NOT_FOUND &&
        fake.publish_calls == 1,
    "failed Ttx init captures last error before publication");
```

The fake publisher returns so the helper can return zero in tests; production publication terminates and fail-fasts.

- [ ] **Step 2: Write failing exact-export installation tests**

Use injected module, export, create-disabled, enable, and reset callbacks. Require:

- module resolution asks only for `TtxUpdateDownloader.dll`;
- export resolution asks only for `?TtxUDLInit@@YAHKKKK@Z`;
- module failure records `GetLastError` and performs no later action;
- export failure performs no hook creation;
- creation failure calls reset once and never enables;
- enable failure calls reset once;
- success creates disabled, enables once, and does not reset;
- there is no RVA or ordinal fallback.
- the production feature installs this guard even when registry virtualization is disabled.

Representative assertion:

```cpp
const auto installed = InstallTtxInitGuard(TtxGuardInstallActionsFrom(fake));
failures += Expect(
    installed && fake.module_name == L"TtxUpdateDownloader.dll" &&
        fake.export_name == "?TtxUDLInit@@YAHKKKK@Z" &&
        fake.create_disabled_calls == 1 && fake.enable_calls == 1 &&
        fake.reset_calls == 0,
    "exact supported Ttx export installs disabled then enables");
```

- [ ] **Step 3: Write failing feature-layer rollback tests**

Create and register `FeatureHookLayerTests`, expose a small feature-specific transaction seam, and verify call order:

```cpp
struct FeatureHookLayerActions {
    void* context{};
    std::expected<void, gc::win32_hooks::HookInstallError>
        (*install_kernel32)(void*) noexcept{};
    std::expected<void, gc::system_path::TtxGuardInstallError>
        (*install_ttx)(void*) noexcept{};
    void (*rollback_kernel32)(void*) noexcept{};
    void (*deactivate_kernel32)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, FeatureError>
InstallFeatureHookLayers(FeatureHookLayerActions actions) noexcept;

const auto failed = gc::rfid::InstallFeatureHookLayers(
    Actions(kernel_success_ttx_failure));
failures += Expect(
    !failed && fake.order == std::vector<Operation>{
        Operation::install_kernel32,
        Operation::install_ttx,
        Operation::rollback_kernel32,
        Operation::deactivate_kernel32,
    },
    "Ttx failure rolls back committed Kernel32 layer");
```

Kernel32 failure must return `FeatureFailureStage::hook_installation`, deactivate dispatch, and avoid Ttx. Ttx failure must preserve `TtxGuardInstallError` in `FeatureError` with `FeatureFailureStage::ttx_guard_installation`. Full success must install Kernel32 before Ttx and perform no rollback.

- [ ] **Step 4: Run new targets to verify RED**

```powershell
cmake --build --preset msvc32-debug --target TtxInitGuardTests FeatureHookLayerTests
```

Expected: FAIL because the Ttx guard and layer transaction do not exist.

- [ ] **Step 5: Implement the exact guard public surface**

Declare:

```cpp
namespace gc::system_path {
inline constexpr wchar_t kTtxModuleName[] = L"TtxUpdateDownloader.dll";
inline constexpr char kTtxUdlInitExport[] = "?TtxUDLInit@@YAHKKKK@Z";
using TtxUdlInitFn = int(__cdecl*)(
    unsigned int, unsigned int, unsigned int, unsigned int);

enum class TtxGuardInstallStage {
    invalid_actions,
    resolve_module,
    resolve_export,
    create_hook,
    enable_hook,
};

struct TtxGuardInstallError {
    TtxGuardInstallStage stage{};
    DWORD win32_error{ERROR_SUCCESS};
    std::uint32_t safetyhook_error{};
};

struct TtxGuardRuntimeActions {
    void* context{};
    int (*call_original)(
        void*, unsigned int, unsigned int,
        unsigned int, unsigned int) noexcept{};
    DWORD (*get_last_error)(void*) noexcept{};
    void (*publish_failure)(
        void*, DWORD, const RuntimeRoot&) noexcept{};
};

[[nodiscard]] int InvokeTtxUdlInitGuard(
    unsigned int priority,
    unsigned int game_version,
    unsigned int update_step,
    unsigned int update_options,
    const RuntimeRoot& root,
    TtxGuardRuntimeActions actions) noexcept;

struct TtxGuardInstallActions {
    void* context{};
    void* detour{};
    HMODULE (*get_module)(void*, LPCWSTR) noexcept{};
    FARPROC (*get_export)(void*, HMODULE, LPCSTR) noexcept{};
    DWORD (*get_last_error)(void*) noexcept{};
    std::expected<void, std::uint32_t> (*create_disabled)(
        void*, void* target, void* detour) noexcept{};
    std::expected<void, std::uint32_t> (*enable)(void*) noexcept{};
    void (*reset)(void*) noexcept{};
};

[[nodiscard]] std::expected<void, TtxGuardInstallError>
InstallTtxInitGuard(TtxGuardInstallActions actions) noexcept;

class TtxInitGuard {
public:
    explicit TtxInitGuard(RuntimeRoot root);
    ~TtxInitGuard() noexcept;
    TtxInitGuard(const TtxInitGuard&) = delete;
    TtxInitGuard& operator=(const TtxInitGuard&) = delete;

    [[nodiscard]] std::expected<void, TtxGuardInstallError>
    Install() noexcept;
    void Reset() noexcept;

private:
    static int __cdecl Detour(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options) noexcept;
    int Invoke(
        unsigned int priority,
        unsigned int game_version,
        unsigned int update_step,
        unsigned int update_options) noexcept;

    static std::atomic<TtxInitGuard*> active_;
    RuntimeRoot root_;
    safetyhook::InlineHook hook_;
};
}
```

`InstallTtxInitGuard` validates every callback and the detour pointer, resolves only the named module and export, and converts the SafetyHook error type to the stored `std::uint32_t`. The class's create callback moves the disabled hook into `hook_` and publishes `active_` before enable; its reset callback resets the hook and clears `active_`. Because the hook is still disabled while `active_` is published, the detour cannot run with half-created state.

- [ ] **Step 6: Implement SafetyHook installation and the detour**

Production creation must use:

```cpp
auto created = safetyhook::InlineHook::create(
    reinterpret_cast<void*>(target),
    reinterpret_cast<void*>(&TtxInitGuard::Detour),
    safetyhook::InlineHook::StartDisabled);
```

Link `gc_system_path` publicly to the existing `safetyhook::safetyhook` target because `TtxInitGuard.h` owns a concrete `safetyhook::InlineHook`; do not fetch or wrap a second hook library.

Move the created hook into owned state, then call `.enable()`. On any failure, `.reset()` before returning the structured error. The trampoline callback must call:

```cpp
return hook_.unsafe_ccall<int>(
    priority, game_version, update_step, update_options);
```

`Detour` loads `active_` and delegates to `Invoke`; `Invoke` supplies the trampoline, immediate `GetLastError`, and fatal publisher callbacks to `InvokeTtxUdlInitGuard`. The destructor calls `Reset()`. `Reset()` first disables/resets the owned hook and then clears `active_`; no active detour may outlive the owning `FeatureState`.

When the original returns zero, call `GetLastError` immediately, format a message containing the configured spelling, resolved native path, and error code, then invoke `PublishStartupFatal` with a Ttx-specific title and exit code. Catch all formatting/logging failures and use a fixed fallback modal before terminating. Do not return to the game in production.

- [ ] **Step 7: Integrate the two hook layers transactionally**

Store `TtxInitGuard` in `FeatureState`. Activate Kernel32 dispatch, install its transaction, then install the Ttx guard. Implement `InstallFeatureHookLayers` with the exact ordering proven by the test. On Ttx failure, call `MinHookTransaction::Rollback()` before `Kernel32Hooks::Deactivate()`. Publish `g_feature_state` only after both layers succeed.

Extend the existing error contract without erasing its MinHook detail:

```cpp
enum class FeatureFailureStage {
    configuration,
    allocation,
    hook_installation,
    ttx_guard_installation,
};

struct FeatureError {
    FeatureFailureStage stage{};
    DWORD win32_error{ERROR_SUCCESS};
    gc::win32_hooks::HookInstallError hook{};
    gc::system_path::TtxGuardInstallError ttx{};
};
```

The loader must format the exact module, decorated export, install stage, Win32 error, and SafetyHook error, then call `PublishStartupFatal` with an unsupported-downloader/setup message before returning `FALSE`. Use the same fatal publisher for a MinHook-layer setup failure, with the MinHook stage and status. The installed guard owns the separate runtime-failure modal only when the original initializer later returns zero.

- [ ] **Step 8: Run focused tests and build the DLL to verify GREEN**

```powershell
cmake --build --preset msvc32-debug --target TtxInitGuardTests FeatureHookLayerTests Kernel32HookTests iDmacDrv32
ctest --preset msvc32-debug -R "^(TtxInitGuardTests|FeatureHookLayerTests|Kernel32HookTests)$"
```

Expected: ABI forwarding, exact export resolution, create/enable rollback, fatal boundary, cross-layer rollback, and the game DLL build all pass.

- [ ] **Step 9: Commit the Ttx initialization guard**

```powershell
git add -- src/SystemPath/TtxInitGuard.h src/SystemPath/TtxInitGuard.cpp src/SystemPath/CMakeLists.txt tests/SystemPath/TtxInitGuardTests.cpp tests/SystemPath/CMakeLists.txt src/Rfid/Feature.h src/Rfid/Feature.cpp src/Loader/DllMain.cpp tests/Rfid/FeatureHookLayerTests.cpp tests/Rfid/CMakeLists.txt src/CMakeLists.txt
git commit -m "Guard Ttx downloader initialization"
```

---

### Task 8: Prove the complete startup data flow and run both preset graphs

**Files:**
- Create: `tests/SystemPath/SystemPathIntegrationTests.cpp`
- Modify: `tests/SystemPath/CMakeLists.txt`
- Modify only if verification finds a production defect: files owned by Tasks 1-7 that directly cause the failing assertion.

**Interfaces:**
- Consumes: public config-document parsing, root preparation, canonical serialization actions, registry derivation, and `SystemPathRouter` APIs.
- Produces: one end-to-end regression test proving the approved fallback path is coherent across all configuration and routing consumers.

- [ ] **Step 1: Write and register the public-API integration test**

Start from a legacy config whose three leaves are the shipped defaults, enable registry virtualization, and use fakes where `D:\system` fails but the config-directory tree succeeds. Exercise only public APIs:

```cpp
const auto parsed = ParseAndValidateInputConfigDocument(legacy_enabled_text);
AtomicFake persisted_config;
const auto prepared = PrepareAndPersistGameSystemPathConfiguration(
    parsed->config,
    parsed->registry_paths_migrated,
    L"H:\\遊戲\\config.toml",
    GameSystemPathPreparationActions{
        .directories = DirectoryActions(default_fails_fallback_succeeds),
        .config_write = AtomicActions(persisted_config),
    });
const auto derived = DeriveNesysPaths(
    prepared->config.registry().system_path());
SystemPathRouter router{prepared->runtime};
const auto ttx_path = router.RoutePathW(L"D:\\system\\DUA\\download\\item.dat");

failures += Expect(
    prepared->persisted && persisted_config.replaces == 1 &&
        persisted_config.serialized.find("system_path") != std::string::npos &&
        persisted_config.serialized.find("news_path") == std::string::npos &&
        prepared->config.registry().system_path() == ".\\system" &&
        derived && derived->news == ".\\system\\DUA\\news" &&
        ttx_path && ttx_path->matched &&
        ttx_path->path == L"H:\\遊戲\\system\\DUA\\download\\item.dat",
    "fallback root is shared by persisted config, NESYS values, and Ttx routing");
```

Add a second flow for registry-disabled mode: real `D:\system` succeeds, `.persisted` is false, the atomic writer records zero calls, derived overrides are not consumed, and the router is disabled. This is a cross-component regression test, not a copy of component edge cases.

Register the target in the same change:

```cmake
add_executable(SystemPathIntegrationTests SystemPathIntegrationTests.cpp)
target_link_libraries(SystemPathIntegrationTests PRIVATE
        gc_config
        gc_system_path
)
add_test(NAME SystemPathIntegrationTests COMMAND SystemPathIntegrationTests)
```

- [ ] **Step 2: Run the integration target**

```powershell
cmake --build --preset msvc32-debug --target SystemPathIntegrationTests
ctest --preset msvc32-debug -R "^SystemPathIntegrationTests$"
```

Expected: the new target builds and passes if Tasks 1-7 expose one coherent flow. A failure must identify a real mismatch between public configuration, persistence, derivation, or routing behavior; target-registration errors are setup mistakes, not an accepted RED state.

- [ ] **Step 3: Make only producer-owned integration fixes if needed**

If the test exposes an interface mismatch, fix the producing task's production code; do not weaken the assertions or add test-only routing behavior.

- [ ] **Step 4: Run all focused feature tests**

```powershell
cmake --build --preset msvc32-debug --target ConfigFeatureTests ConfigDocumentTests SystemPathConfigTests RegistryConfigOverrideTests SystemRootTests SystemPathRouterTests TtxInitGuardTests SystemPathIntegrationTests Kernel32HookTests FeatureHookLayerTests ConfigGUI iDmacDrv32
ctest --preset msvc32-debug -R "^(ConfigFeatureTests|ConfigDocumentTests|SystemPathConfigTests|RegistryConfigOverrideTests|SystemRootTests|SystemPathRouterTests|TtxInitGuardTests|SystemPathIntegrationTests|Kernel32HookTests|FeatureHookLayerTests)$"
```

Expected: every focused target builds and all named tests pass.

- [ ] **Step 5: Run the complete Debug and Release preset graphs**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
```

Expected: both complete x86 builds and both complete CTest suites pass. Record exact test counts and elapsed times in the execution handoff.

- [ ] **Step 6: Inspect the built configuration and preserved iDmac ABI**

```powershell
Get-Content -LiteralPath 'build-msvc32-release\dist\config.toml'
dumpbin /exports 'build-msvc32-release\dist\iDmacDrv32.dll'
git diff --check
git status --short
```

Require the built config to contain `system_path` and no legacy leaf assignments. Compare the export list to `src/Driver/iDmac/iDmacDrv32.def`; no export name or ordinal may change. `git diff --check` must be silent, and only intended source/test/config files may be modified.

- [ ] **Step 7: Commit the end-to-end regression test and any direct integration fix**

```powershell
git add -- tests/SystemPath/SystemPathIntegrationTests.cpp tests/SystemPath/CMakeLists.txt
git diff --cached --name-only
git commit -m "Test system path startup integration"
```

If integration exposed a production defect, return to the owning Task 1-7 step, apply its focused test and verification commands, and commit only the exact production paths listed there before this integration-test commit. Do not use `git add --update`, a wildcard, or a repository-wide add. Before committing, require `git diff --cached --name-only` to contain only the two integration-test paths.

- [ ] **Step 8: Hand off runtime acceptance without claiming it**

Report automated evidence separately from these unperformed user checks:

1. Start the game with registry virtualization enabled and no usable `D:` drive; verify `.\system` is persisted and the updater no longer crashes.
2. Start with registry virtualization disabled and no usable `D:\system`; verify the actionable modal stops startup.
3. Start with a writable custom root; verify downloader and NESYS process writes land below the same tree using `loader-log.txt` and `loader-service-log.txt`.
4. Force an original `TtxUDLInit` failure; verify the Ttx-specific modal appears and the later `RtlpWaitOnCriticalSection` crash does not occur.

Do not deploy to `H:\gc` or perform these checks unless the user explicitly authorizes deployment/runtime mutation.
