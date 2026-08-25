# Configuration Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the TOML-shaped runtime service locator with a strict mutable document, a pure complete-error compiler, explicit startup repair/persistence, and feature-owned immutable launch settings without changing the TOML layout or feature policy.

**Architecture:** `ConfigDocument` remains the only reflect-cpp/TOML-shaped mutable type. `ConfigCompiler` validates leaves with reflect-cpp validators, evaluates dependency rules only after their leaves pass, and produces feature-owned settings grouped temporarily in `ValidatedConfig`. A loader startup transaction reads once, performs only the three approved startup corrections, recompiles, atomically persists once when required, and passes settings by value into long-lived feature owners. ConfigGUI edits the raw document and uses the same compiler for all semantic errors and Save gating.

**Tech Stack:** Windows x86 C++23, MSVC, CMake presets, reflect-cpp v0.25.0, toml++, ImGui, standalone CTest executables, CLion MCP diagnostics and formatting.

**Spec:** `docs/superpowers/specs/2026-08-26-configuration-architecture-design.md`

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`. Do not copy or deploy binaries to `H:\gc`, push, create a PR, or change external/runtime files.
- Follow `AGENTS.md`. No exception may cross `DllMain`, an exported iDmac entry point, a hook, COM boundary, worker entry point, or audio callback.
- Keep current-schema loading strict and complete: required fields remain required and `rfl::NoExtraFields` remains active. ConfigGUI defaults never become missing-field defaults.
- Preserve the existing TOML table and field names. This is an architecture change, not a format redesign.
- Runtime values are a fixed launch snapshot. External edits after loading apply only to the next process launch.
- Runtime persistence is forbidden except for recognized migrations, system-path fallback correction, and forced test-mode-storage redirect when native storage is unavailable. Combine applicable changes into one candidate and one atomic write.
- The game process owns startup persistence and game-only probes. The NESYS service applies migrations in memory but never probes game storage or writes `config.toml`.
- Long-lived features own strings, vectors, bindings, and settings by value. No hook, callback, lazy singleton, or worker may retain a reference into `ConfigDocument`, `ValidatedConfig`, `PreparedProcessConfiguration`, or a `DllMain` stack object.
- Use CLion MCP `reformat_file` for changed C/C++ files. Do not run an external clang-format command. Use CLion MCP diagnostics after CMake reloads. Use the shell for Git, CMake, builds, and CTest.
- Run build/test commands from an x86 MSVC developer PowerShell with `$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'`.
- Add only the two focused config executables named in this plan. Do not resurrect the deleted nominal suite, copy the complete TOML fixture, add source-text assertions, mirror production validators/default tables in tests, or expose test-only runtime state.
- Treat Debug/Release builds and CTest as local/static proof only. Do not claim in-game or cabinet acceptance.

## File and Responsibility Map

| Area | Final responsibility | Key files |
|---|---|---|
| Serialized document | TOML-shaped mutable values, strict shape, migrations, serialization, atomic write | `src/Config/ConfigDocument.h`, `src/Config/ConfigDocument.cpp` |
| Semantic errors | Typed paths, stable codes, deterministic rendering | `src/Config/ConfigError.h`, `src/Config/ConfigError.cpp` |
| Pure compiler | Leaf validators, guarded cross-field rules, conversion to settings | `src/Config/ConfigCompiler.h`, `src/Config/ConfigCompiler.cpp` |
| Feature settings | Reflect-free, read-only, consumer-owned values | `src/Audio/AudioSettings.h`, `src/Input/Types/InputSettings.h`, `src/Input/Switch/SwitchInputSettings.h`, `src/Logging/LoggingSettings.h`, `src/Nesys/NesysSettings.h`, `src/Patches/AbsoluteJudgement/JudgementSettings.h`, `src/Patches/Framerate/FramerateSettings.h`, `src/Rfid/FeatureSettings.h`, `src/SystemPath/SystemPathSettings.h` |
| Startup transaction | Read once, role policy, approved repairs, final compile, one write, publication | `src/Loader/StartupConfiguration.h`, `src/Loader/StartupConfiguration.cpp` |
| Composition root | Format outer errors and distribute settings by value | `src/Loader/DllMain.cpp` |
| GUI | Edit invalid drafts, show all errors, preflight ASIO on Save, atomic write | `tools/ConfigGUI/Main.cpp`, `tools/ConfigGUI/AudioOperationWorker.*`, `tools/ConfigGUI/AudioBackendEditorModel.*`, `tools/ConfigGUI/InputEditorModel.*` |
| Contract tests | Document/compiler boundary, complete errors, concrete variants, ownership | `tests/Config/ConfigContractTests.cpp` |
| Startup tests | Role/probe/write transaction and approved repair behavior | `tests/Config/ConfigStartupTests.cpp` |

---

### Task 1: Separate strict document loading from semantic acceptance

**Files:**

- Modify: `src/Config/ConfigDocument.h`
- Modify: `src/Config/ConfigDocument.cpp`
- Modify: `src/Config/config.h`
- Modify: `src/Config/config.cpp`
- Modify: `src/Config/CMakeLists.txt`
- Create: `tests/Config/ConfigContractTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: existing `InputConfig`, migration logic, `rfl::toml::read<..., rfl::NoExtraFields>`, and atomic writer actions.
- Produces: mutable `ConfigDocument`, typed document-load/persistence errors, `ParseConfigDocument`, `SerializeConfigDocument`, and `WriteConfigDocumentAtomically`.
- Temporary compatibility: the runtime-only old parse-and-semantic-validate entry point may delegate to the new parser plus the old validator until Task 10; ConfigGUI and new tests must not use it.

- [ ] Add the first contract test target around the distributed configuration instead of adding a copied fixture.

```cmake
add_executable(gc_config_contract_tests
        Config/ConfigContractTests.cpp)
target_link_libraries(gc_config_contract_tests PRIVATE gc_config)
target_compile_definitions(gc_config_contract_tests PRIVATE
        GC_TEST_CONFIG_PATH="${PROJECT_SOURCE_DIR}/config.toml")
add_test(NAME ConfigContract COMMAND gc_config_contract_tests)
```

Use the repository's existing standalone `main`/failure-count/`Expect` style. The first test reads `GC_TEST_CONFIG_PATH`, calls `ParseConfigDocument`, and expects success.

- [ ] Add a semantic-invalid/document-valid test against the new boundary and run it before implementation.

```cpp
auto parsed = ParseDistributedDocument();
parsed.document.experimental().target_fps = 59;
const auto serialized = gc::config::SerializeConfigDocument(parsed.document);
Expect(serialized.has_value(), "invalid semantic draft still serializes");
const auto reparsed = gc::config::ParseConfigDocument(*serialized);
Expect(reparsed.has_value(), "semantic invalidity does not destroy document shape");
```

Run:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_config_contract_tests
ctest --preset msvc32-debug -R '^ConfigContract$' --output-on-failure
```

Expected before implementation: the target fails to compile because `ParseConfigDocument` and `SerializeConfigDocument` do not exist. The only current document entry point also performs first-error semantic validation, which is the behavior this task removes from the new boundary.

- [ ] Rename the root serialized type and expose distinct boundary errors.

```cpp
namespace gc::config {

enum class ConfigDocumentLoadErrorCode : std::uint8_t {
    toml_syntax,
    obsolete_schema,
    unsupported_schema,
    strict_shape,
    serialization,
};

struct ConfigDocumentLoadError {
    ConfigDocumentLoadErrorCode code{};
    std::string message;
};

enum class ConfigPersistenceStage : std::uint8_t {
    serialize,
    temporary_write,
    atomic_replace,
};

struct ConfigPersistenceError {
    ConfigPersistenceStage stage{};
    std::string message;
};

struct ConfigDocument {
    rfl::Rename<"input_schema_version", std::uint32_t>
        input_schema_version{gc::config::kInputSchemaVersion};
    rfl::Rename<"input_poll_hz", std::uint32_t> input_poll_hz{1000};
    rfl::Rename<"input_mode", input::InputMode>
        input_mode{input::InputMode::Keyboard};
    rfl::Rename<"gameplay_input_style", input::GameplayInputStyle>
        gameplay_input_style{input::GameplayInputStyle::Arcade};
    rfl::Rename<"axis_press_threshold_percent", std::uint32_t>
        axis_press_threshold_percent{50};
    rfl::Rename<"axis_release_threshold_percent", std::uint32_t>
        axis_release_threshold_percent{40};
    rfl::Rename<"keyboard", NativeKeyboardConfig> keyboard;
    rfl::Rename<"controller", ControllerConfig> controller;
    rfl::Rename<"nesys", NesysConfig> nesys;
    rfl::Rename<"registry", RegistryConfig> registry;
    rfl::Rename<"logging", LoggingConfig> logging;
    rfl::Rename<"experimental", ExperimentalConfig> experimental;
};

struct ParsedConfigDocument {
    ConfigDocument document;
    ConfigDocumentMigrations migrations;
};

[[nodiscard]] std::expected<ParsedConfigDocument, ConfigDocumentLoadError>
ParseConfigDocument(std::string_view text) noexcept;

[[nodiscard]] std::expected<std::string, ConfigDocumentLoadError>
SerializeConfigDocument(const ConfigDocument& document) noexcept;

[[nodiscard]] std::expected<void, ConfigPersistenceError>
WriteConfigDocumentAtomically(
    const std::filesystem::path& path,
    const ConfigDocument& document,
    const AtomicConfigWriteActions& actions =
        ProductionAtomicConfigWriteActions()) noexcept;

} // namespace gc::config
```

Keep the raw fields as plain editable values. Do not wrap fields in `rfl::Validator`; doing so would make semantic-invalid GUI drafts fail structural parsing.

Move the TOML-shaped `NesysConfig`, `LoggingConfig`, and `ExperimentalConfig` declarations from `config.h` into `ConfigDocument.h` beside the root. Keep `NativeKeyboardConfig`/`ControllerConfig` and registry nested types in their existing raw config headers. Remove the current `ConfigDocument.h` -> `config.h` include cycle; during the transition, `config.h` includes `ConfigDocument.h` and contains only the legacy manager/validator facade.

- [ ] Split `ParseConfigDocument` at the exact boundary: TOML syntax, obsolete/schema policy, recognized migrations, and `rfl::NoExtraFields` stay; the call to `ValidateInputConfig` leaves the document parser.

- [ ] Keep a narrowly named temporary runtime compatibility function in `config.cpp` that parses and invokes the legacy validator so current `ConfigManager` behavior does not weaken before the compiler exists. Mark its deletion in Task 10; do not call it from ConfigGUI or the new tests.

- [ ] Use CLion MCP to reformat the changed C++ files and inspect diagnostics for `ConfigDocument.cpp`, `config.cpp`, and `ConfigContractTests.cpp`.

- [ ] Rebuild and rerun `ConfigContract`; confirm both the distributed file and semantic-invalid draft pass strict document construction.

- [ ] Commit.

```powershell
git add -- src/Config/ConfigDocument.h src/Config/ConfigDocument.cpp src/Config/config.h src/Config/config.cpp src/Config/CMakeLists.txt tests/Config/ConfigContractTests.cpp tests/CMakeLists.txt
git diff --cached --check
git commit -m "Separate config document parsing from validation"
```

---

### Task 2: Define reflect-free feature settings and concrete alternatives

**Files:**

- Create: `src/Logging/LoggingSettings.h`
- Create: `src/Audio/AudioSettings.h`
- Create: `src/Input/Types/InputSettings.h`
- Create: `src/Input/Switch/SwitchInputSettings.h`
- Create: `src/Patches/Framerate/FramerateSettings.h`
- Create: `src/Patches/AbsoluteJudgement/JudgementSettings.h`
- Create: `src/Nesys/NesysSettings.h`
- Create: `src/Rfid/FeatureSettings.h`
- Create: `src/SystemPath/SystemPathSettings.h`
- Modify: `src/Config/ConfigDocument.h`

**Interfaces:**

- Consumes: existing domain enums such as `PhysicalKey`, `LogicalAction`, `InputMode`, `GameplayInputStyle`, and exact audio clock domain.
- Produces: passive, read-only, reflect-free values with compiler-only construction.
- Does not produce: runtime initialization changes, filesystem behavior, or a new global settings registry.

- [ ] Add a compile-time include smoke section to `ConfigContractTests.cpp` that names every settings type. Build first and observe missing-header failures.

```cpp
static_assert(std::is_copy_constructible_v<gc::audio::AudioSettings>);
static_assert(std::is_move_constructible_v<gc::input::InputSettings>);
static_assert(std::is_move_constructible_v<gc::nesys_service::NesysSettings>);
```

- [ ] Introduce typed controller bindings. Each alternative contains exactly the fields meaningful to that control. Keep the existing raw `DigitalControlBinding` for document editing, discovery, descriptors, and capture.

```cpp
namespace gc::input {

struct HidControlAddress {
    std::uint16_t usage_page{};
    std::uint16_t usage{};
    std::uint16_t link_collection{};
    std::uint8_t report_id{};
};

class XInputButtonBinding final {
public:
    [[nodiscard]] LogicalAction action() const noexcept { return action_; }
    [[nodiscard]] XInputControl control() const noexcept { return control_; }
private:
    XInputButtonBinding(
        LogicalAction action,
        XInputControl control) noexcept
        : action_(action), control_(control) {}
    friend class gc::config::ConfigCompiler;
    LogicalAction action_{};
    XInputControl control_{};
};

class XInputAxisBinding final {
public:
    [[nodiscard]] LogicalAction action() const noexcept { return action_; }
    [[nodiscard]] XInputControl control() const noexcept { return control_; }
    [[nodiscard]] ControlDirection direction() const noexcept {
        return direction_;
    }
private:
    XInputAxisBinding(
        LogicalAction action,
        XInputControl control,
        ControlDirection direction) noexcept
        : action_(action), control_(control), direction_(direction) {}
    friend class gc::config::ConfigCompiler;
    LogicalAction action_{};
    XInputControl control_{};
    ControlDirection direction_{};
};

class XInputTriggerBinding final {
public:
    [[nodiscard]] LogicalAction action() const noexcept { return action_; }
    [[nodiscard]] XInputControl control() const noexcept { return control_; }
private:
    XInputTriggerBinding(
        LogicalAction action,
        XInputControl control) noexcept
        : action_(action), control_(control) {}
    friend class gc::config::ConfigCompiler;
    LogicalAction action_{};
    XInputControl control_{};
};

class RawHidButtonBinding final {
public:
    [[nodiscard]] LogicalAction action() const noexcept { return action_; }
    [[nodiscard]] const HidControlAddress& address() const noexcept {
        return address_;
    }
private:
    RawHidButtonBinding(
        LogicalAction action,
        HidControlAddress address) noexcept
        : action_(action), address_(address) {}
    friend class gc::config::ConfigCompiler;
    LogicalAction action_{};
    HidControlAddress address_{};
};

class RawHidValueBinding final {
public:
    [[nodiscard]] LogicalAction action() const noexcept { return action_; }
    [[nodiscard]] const HidControlAddress& address() const noexcept {
        return address_;
    }
    [[nodiscard]] ControlDirection direction() const noexcept {
        return direction_;
    }
    [[nodiscard]] std::int32_t neutral_value() const noexcept {
        return neutral_value_;
    }
private:
    RawHidValueBinding(
        LogicalAction action,
        HidControlAddress address,
        ControlDirection direction,
        std::int32_t neutral_value) noexcept
        : action_(action),
          address_(address),
          direction_(direction),
          neutral_value_(neutral_value) {}
    friend class gc::config::ConfigCompiler;
    LogicalAction action_{};
    HidControlAddress address_{};
    ControlDirection direction_{};
    std::int32_t neutral_value_{};
};

class RawHidHatBinding final {
public:
    [[nodiscard]] LogicalAction action() const noexcept { return action_; }
    [[nodiscard]] const HidControlAddress& address() const noexcept {
        return address_;
    }
    [[nodiscard]] ControlDirection direction() const noexcept {
        return direction_;
    }
private:
    RawHidHatBinding(
        LogicalAction action,
        HidControlAddress address,
        ControlDirection direction) noexcept
        : action_(action), address_(address), direction_(direction) {}
    friend class gc::config::ConfigCompiler;
    LogicalAction action_{};
    HidControlAddress address_{};
    ControlDirection direction_{};
};

using ControllerBinding = std::variant<
    XInputButtonBinding,
    XInputAxisBinding,
    XInputTriggerBinding,
    RawHidButtonBinding,
    RawHidValueBinding,
    RawHidHatBinding>;

[[nodiscard]] LogicalAction BindingAction(
    const ControllerBinding& binding) noexcept {
    return std::visit(
        [](const auto& concrete) noexcept { return concrete.action(); },
        binding);
}

} // namespace gc::input
```

Keep these definitions header-only so `gc_config` can construct passive values without linking feature implementations. The compiler still validates that each `XInputControl` and `ControlDirection` belongs to the narrower semantic category represented by its class before invoking the private constructor.

- [ ] Define backend and input settings so invalid device identities cannot be publicly assembled.

```cpp
class XInputControllerSettings final {
public:
    [[nodiscard]] std::uint32_t slot() const noexcept { return slot_; }
    [[nodiscard]] std::span<const ControllerBinding> bindings() const noexcept {
        return bindings_;
    }
private:
    XInputControllerSettings(
        std::uint32_t slot,
        std::vector<ControllerBinding> bindings)
        : slot_(slot), bindings_(std::move(bindings)) {}
    friend class gc::config::ConfigCompiler;
    std::uint32_t slot_{};
    std::vector<ControllerBinding> bindings_;
};

class RawHidControllerSettings final {
public:
    [[nodiscard]] const std::string& device_path() const noexcept {
        return device_path_;
    }
    [[nodiscard]] std::span<const ControllerBinding> bindings() const noexcept {
        return bindings_;
    }
private:
    RawHidControllerSettings(
        std::string device_path,
        std::vector<ControllerBinding> bindings)
        : device_path_(std::move(device_path)),
          bindings_(std::move(bindings)) {}
    friend class gc::config::ConfigCompiler;
    std::string device_path_;
    std::vector<ControllerBinding> bindings_;
};

using ControllerSettings = std::variant<
    XInputControllerSettings,
    RawHidControllerSettings>;

class InputSettings final {
public:
    [[nodiscard]] std::uint32_t poll_hz() const noexcept { return poll_hz_; }
    [[nodiscard]] bool absolute_publication_enabled() const noexcept {
        return absolute_publication_enabled_;
    }
    [[nodiscard]] InputMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint32_t press_percent() const noexcept {
        return press_percent_;
    }
    [[nodiscard]] std::uint32_t release_percent() const noexcept {
        return release_percent_;
    }
    [[nodiscard]] std::span<const KeyboardBinding> keyboard() const noexcept {
        return keyboard_;
    }
    [[nodiscard]] const ControllerSettings& controller() const noexcept {
        return controller_;
    }
private:
    InputSettings(
        std::uint32_t poll_hz,
        bool absolute_publication_enabled,
        InputMode mode,
        std::uint32_t press_percent,
        std::uint32_t release_percent,
        std::vector<KeyboardBinding> keyboard,
        ControllerSettings controller)
        : poll_hz_(poll_hz),
          absolute_publication_enabled_(absolute_publication_enabled),
          mode_(mode),
          press_percent_(press_percent),
          release_percent_(release_percent),
          keyboard_(std::move(keyboard)),
          controller_(std::move(controller)) {}
    friend class gc::config::ConfigCompiler;
    std::uint32_t poll_hz_{};
    bool absolute_publication_enabled_{};
    InputMode mode_{};
    std::uint32_t press_percent_{};
    std::uint32_t release_percent_{};
    std::vector<KeyboardBinding> keyboard_;
    ControllerSettings controller_;
};
```

- [ ] Define audio as a concrete selection and make the exact-clock requirement an audio-owned derived policy.

```cpp
namespace gc::audio {

enum class AudioBackend : std::uint8_t {
    directsound,
    wasapi_exclusive,
    asio,
};

[[nodiscard]] constexpr std::string_view AudioBackendName(
    AudioBackend backend) noexcept {
    switch (backend) {
    case AudioBackend::directsound: return "directsound";
    case AudioBackend::wasapi_exclusive: return "wasapi_exclusive";
    case AudioBackend::asio: return "asio";
    }
    return "unknown";
}

struct DirectSoundSettings final {};

class WasapiExclusiveSettings final {
public:
    [[nodiscard]] std::uint32_t buffer_ms() const noexcept {
        return buffer_ms_;
    }
private:
    explicit WasapiExclusiveSettings(std::uint32_t buffer_ms) noexcept
        : buffer_ms_(buffer_ms) {}
    friend class gc::config::ConfigCompiler;
    std::uint32_t buffer_ms_{};
};

class AsioSettings final {
public:
    [[nodiscard]] const std::string& driver_name() const noexcept {
        return driver_name_;
    }
    [[nodiscard]] std::uint32_t buffer_frames() const noexcept {
        return buffer_frames_;
    }
    [[nodiscard]] std::uint32_t output_base_channel() const noexcept {
        return output_base_channel_;
    }
private:
    AsioSettings(
        std::string driver_name,
        std::uint32_t buffer_frames,
        std::uint32_t output_base_channel)
        : driver_name_(std::move(driver_name)),
          buffer_frames_(buffer_frames),
          output_base_channel_(output_base_channel) {}
    friend class gc::config::ConfigCompiler;
    std::string driver_name_;
    std::uint32_t buffer_frames_{};
    std::uint32_t output_base_channel_{};
};

using AudioBackendSettings = std::variant<
    DirectSoundSettings,
    WasapiExclusiveSettings,
    AsioSettings>;

class AudioSettings final {
public:
    [[nodiscard]] AudioBackend backend() const noexcept { return backend_; }
    [[nodiscard]] const AudioBackendSettings& selection() const noexcept {
        return selection_;
    }
    [[nodiscard]] bool exact_clock_required() const noexcept {
        return exact_clock_required_;
    }
private:
    AudioSettings(
        AudioBackend backend,
        AudioBackendSettings selection,
        bool exact_clock_required)
        : backend_(backend),
          selection_(std::move(selection)),
          exact_clock_required_(exact_clock_required) {}
    friend class gc::config::ConfigCompiler;
    AudioBackend backend_{};
    AudioBackendSettings selection_;
    bool exact_clock_required_{};
};

} // namespace gc::audio
```

- [ ] Define the remaining consumer settings with the same compiler-only construction rule:

  - `LoggingSettings`: `LoaderLogLevel`.
  - `SwitchInputSettings`: `GameplayInputStyle`.
  - `FramerateSettings`: target FPS and timer-freeze enablement.
  - `JudgementSettings`: enabled flag, target FPS, input rate, audio backend name/domain needed by diagnostics and exact-clock startup.
  - `NesysSettings`: adapter-patch policy, owned parsed IPv4 text/octets, and optional owned `RegistryOverrideValues`.
  - `FeatureSettings`: card-read `PhysicalKey` and test-mode-storage redirect enablement.
  - `SystemPathSettings`: registry-enabled flag and owned configured path.

Use these concrete read-only shapes; define their short accessors and private constructors inline in the listed headers so the config library needs no reverse link to feature implementations.

```cpp
namespace gc::logging {

enum class LoaderLogLevel : std::uint8_t { Info, Debug, Verbose };

class LoggingSettings final {
public:
    [[nodiscard]] LoaderLogLevel level() const noexcept { return level_; }
private:
    explicit LoggingSettings(LoaderLogLevel level) noexcept : level_(level) {}
    friend class gc::config::ConfigCompiler;
    LoaderLogLevel level_{};
};

} // namespace gc::logging

namespace gc::switch_input {

class SwitchInputSettings final {
public:
    [[nodiscard]] input::GameplayInputStyle style() const noexcept {
        return style_;
    }
private:
    explicit SwitchInputSettings(input::GameplayInputStyle style) noexcept
        : style_(style) {}
    friend class gc::config::ConfigCompiler;
    input::GameplayInputStyle style_{};
};

} // namespace gc::switch_input

namespace gc::framerate {

class FramerateSettings final {
public:
    [[nodiscard]] std::uint32_t target_fps() const noexcept {
        return target_fps_;
    }
    [[nodiscard]] bool timer_freeze_enabled() const noexcept {
        return timer_freeze_enabled_;
    }
private:
    FramerateSettings(
        std::uint32_t target_fps,
        bool timer_freeze_enabled) noexcept
        : target_fps_(target_fps),
          timer_freeze_enabled_(timer_freeze_enabled) {}
    friend class gc::config::ConfigCompiler;
    std::uint32_t target_fps_{};
    bool timer_freeze_enabled_{};
};

} // namespace gc::framerate

namespace gc::absolute_judgement {

class JudgementSettings final {
public:
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] std::uint32_t target_fps() const noexcept {
        return target_fps_;
    }
    [[nodiscard]] std::uint32_t input_rate_hz() const noexcept {
        return input_rate_hz_;
    }
    [[nodiscard]] audio::AudioBackend audio_backend() const noexcept {
        return audio_backend_;
    }
    [[nodiscard]] std::optional<audio::ExactOutputClockDomain>
    expected_clock_domain() const noexcept {
        return expected_clock_domain_;
    }
private:
    JudgementSettings(
        bool enabled,
        std::uint32_t target_fps,
        std::uint32_t input_rate_hz,
        audio::AudioBackend audio_backend,
        std::optional<audio::ExactOutputClockDomain> expected_clock_domain) noexcept
        : enabled_(enabled),
          target_fps_(target_fps),
          input_rate_hz_(input_rate_hz),
          audio_backend_(audio_backend),
          expected_clock_domain_(expected_clock_domain) {}
    friend class gc::config::ConfigCompiler;
    bool enabled_{};
    std::uint32_t target_fps_{};
    std::uint32_t input_rate_hz_{};
    audio::AudioBackend audio_backend_{};
    std::optional<audio::ExactOutputClockDomain> expected_clock_domain_;
};

} // namespace gc::absolute_judgement

namespace gc::rfid {

class FeatureSettings final {
public:
    [[nodiscard]] input::PhysicalKey card_read_key() const noexcept {
        return card_read_key_;
    }
    [[nodiscard]] bool testmode_storage_redirect_enabled() const noexcept {
        return testmode_storage_redirect_enabled_;
    }
private:
    FeatureSettings(
        input::PhysicalKey card_read_key,
        bool redirect_enabled) noexcept
        : card_read_key_(card_read_key),
          testmode_storage_redirect_enabled_(redirect_enabled) {}
    friend class gc::config::ConfigCompiler;
    input::PhysicalKey card_read_key_{};
    bool testmode_storage_redirect_enabled_{};
};

} // namespace gc::rfid

namespace gc::system_path {

class SystemPathSettings final {
public:
    [[nodiscard]] bool registry_enabled() const noexcept {
        return registry_enabled_;
    }
    [[nodiscard]] const std::string& configured_path() const noexcept {
        return configured_path_;
    }
private:
    SystemPathSettings(bool registry_enabled, std::string configured_path)
        : registry_enabled_(registry_enabled),
          configured_path_(std::move(configured_path)) {}
    friend class gc::config::ConfigCompiler;
    bool registry_enabled_{};
    std::string configured_path_;
};

} // namespace gc::system_path
```

Use the same closed construction seam for NESYS values:

```cpp
namespace gc::nesys_service {

class ServerAddressState final {
public:
    [[nodiscard]] const Ipv4Octets& octets() const noexcept { return octets_; }
    [[nodiscard]] const std::string& ansi() const noexcept { return ansi_; }
    [[nodiscard]] const std::wstring& wide() const noexcept { return wide_; }
private:
    ServerAddressState(
        Ipv4Octets octets,
        std::string ansi,
        std::wstring wide)
        : octets_(octets),
          ansi_(std::move(ansi)),
          wide_(std::move(wide)) {}
    friend class gc::config::ConfigCompiler;
    Ipv4Octets octets_{};
    std::string ansi_;
    std::wstring wide_;
};

class RegistryOverrideValues final {
public:
    [[nodiscard]] std::uint32_t country() const noexcept { return country_; }
    [[nodiscard]] std::uint32_t game_kind() const noexcept { return game_kind_; }
    [[nodiscard]] std::uint32_t event_next_time() const noexcept {
        return event_next_time_;
    }
    [[nodiscard]] std::uint32_t condition_time() const noexcept {
        return condition_time_;
    }
    [[nodiscard]] std::uint32_t traffic_count() const noexcept {
        return traffic_count_;
    }
    [[nodiscard]] std::uint32_t log_level() const noexcept { return log_level_; }
    [[nodiscard]] const std::string& news_path() const noexcept {
        return news_path_;
    }
    [[nodiscard]] const std::string& event_path() const noexcept {
        return event_path_;
    }
    [[nodiscard]] const std::string& log_path() const noexcept {
        return log_path_;
    }
private:
    RegistryOverrideValues(
        std::uint32_t country,
        std::uint32_t game_kind,
        std::uint32_t event_next_time,
        std::uint32_t condition_time,
        std::uint32_t traffic_count,
        std::uint32_t log_level,
        std::string news_path,
        std::string event_path,
        std::string log_path)
        : country_(country),
          game_kind_(game_kind),
          event_next_time_(event_next_time),
          condition_time_(condition_time),
          traffic_count_(traffic_count),
          log_level_(log_level),
          news_path_(std::move(news_path)),
          event_path_(std::move(event_path)),
          log_path_(std::move(log_path)) {}
    friend class gc::config::ConfigCompiler;
    std::uint32_t country_{};
    std::uint32_t game_kind_{};
    std::uint32_t event_next_time_{};
    std::uint32_t condition_time_{};
    std::uint32_t traffic_count_{};
    std::uint32_t log_level_{};
    std::string news_path_;
    std::string event_path_;
    std::string log_path_;
};

class NesysSettings final {
public:
    [[nodiscard]] bool adapter_patch_enabled() const noexcept {
        return adapter_patch_enabled_;
    }
    [[nodiscard]] const ServerAddressState& server_address() const noexcept {
        return server_address_;
    }
    [[nodiscard]] const std::optional<RegistryOverrideValues>&
    registry_override() const noexcept {
        return registry_override_;
    }
private:
    NesysSettings(
        bool adapter_patch_enabled,
        ServerAddressState server_address,
        std::optional<RegistryOverrideValues> registry_override)
        : adapter_patch_enabled_(adapter_patch_enabled),
          server_address_(std::move(server_address)),
          registry_override_(std::move(registry_override)) {}
    friend class gc::config::ConfigCompiler;
    bool adapter_patch_enabled_{};
    ServerAddressState server_address_;
    std::optional<RegistryOverrideValues> registry_override_;
};

} // namespace gc::nesys_service
```

`RegistryOverrideValues` moves from `RegistryConfigOverride.h` into `NesysSettings.h`; it remains a value object. `ServerAddressState` likewise becomes part of `NesysSettings.h`, so runtime overrides receive already validated state rather than raw document strings.

- [ ] Move the serialized audio enum field to `gc::audio::AudioBackend` and the logging enum field to the logging domain header. Reflect-cpp may serialize domain enums, but no feature settings header may include reflect-cpp or a config header.

- [ ] Use CLion MCP formatting and diagnostics on all new headers and the document header.

- [ ] Build the contract target and the full Debug graph to catch include direction or incomplete-type errors.

```powershell
cmake --build --preset msvc32-debug --target gc_config_contract_tests
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -R '^ConfigContract$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- src/Logging/LoggingSettings.h src/Audio/AudioSettings.h src/Input/Types/InputSettings.h src/Input/Switch/SwitchInputSettings.h src/Patches/Framerate/FramerateSettings.h src/Patches/AbsoluteJudgement/JudgementSettings.h src/Nesys/NesysSettings.h src/Rfid/FeatureSettings.h src/SystemPath/SystemPathSettings.h src/Config/ConfigDocument.h tests/Config/ConfigContractTests.cpp
git diff --cached --check
git commit -m "Define feature-owned configuration settings"
```

---

### Task 3: Implement complete semantic compilation

**Files:**

- Create: `src/Config/ConfigError.h`
- Create: `src/Config/ConfigError.cpp`
- Create: `src/Config/ConfigCompiler.h`
- Create: `src/Config/ConfigCompiler.cpp`
- Modify: `src/Config/CMakeLists.txt`
- Modify: `src/Config/config.h`
- Modify: `src/Config/config.cpp`
- Modify: `tests/Config/ConfigContractTests.cpp`

**Interfaces:**

- Consumes: complete `ConfigDocument` and feature settings private construction seams.
- Produces: `std::expected<ValidatedConfig, ConfigErrors>` with every independently detectable semantic failure in deterministic document/index order.
- Temporary compatibility: `ConfigManager` compiles once after parsing and recompiles after its old preparation step, exposing only const settings copies to the composition root while consumers migrate.

- [ ] Add failing tests for complete ordered errors, dependent-rule suppression, the missing WASAPI buffer check, and concrete variants.

```cpp
using ErrorKey = std::pair<std::string, gc::config::ConfigErrorCode>;

std::vector<ErrorKey> ErrorKeys(const gc::config::ConfigErrors& errors) {
    std::vector<ErrorKey> keys;
    keys.reserve(errors.size());
    for (const auto& error : errors) {
        keys.emplace_back(error.path.Render(), error.code);
    }
    return keys;
}

void TestCompleteOrderedErrors() {
    auto parsed = ParseDistributedDocument();
    auto& document = parsed.document;
    document.input_poll_hz = 333;
    document.axis_press_threshold_percent = 101;
    document.keyboard().test = {
        .make_code = 0,
        .prefix = gc::input::ScanCodePrefix::None,
    };
    document.nesys().server_ip = "999.1.1.1";
    document.experimental().target_fps = 59;

    const auto result = gc::config::ConfigCompiler::Compile(document);
    Expect(!result.has_value(), "multi-error document is rejected");
    Expect(
        ErrorKeys(result.error()) == std::vector<ErrorKey>{
            {"input_poll_hz", gc::config::ConfigErrorCode::unsupported_value},
            {"axis_press_threshold_percent", gc::config::ConfigErrorCode::out_of_range},
            {"keyboard.test", gc::config::ConfigErrorCode::invalid_value},
            {"nesys.server_ip", gc::config::ConfigErrorCode::invalid_value},
            {"experimental.target_fps", gc::config::ConfigErrorCode::out_of_range},
        },
        "compiler returns every independent error in declaration order");
}
```

Add focused assertions that:

- `audio_backend = wasapi_exclusive` plus `wasapi_exclusive_buffer_ms = 0` returns `out_of_range` at `experimental.wasapi_exclusive_buffer_ms`;
- invalid `input_poll_hz` suppresses the absolute-judgement/1000-Hz dependent error, while an independently valid DirectSound selection still produces the audio-backend dependency error;
- valid ASIO raw fields produce `AsioSettings` with the exact owned driver/buffer/channel values;
- one valid XInput-axis binding produces `XInputAxisBinding`, and backend/type mismatch produces one primary binding error at `controller.bindings[0].type` with a related `controller.backend` path.

Run the target and confirm the new compiler API/tests fail to build before implementation.

- [ ] Implement typed paths and stable error codes.

```cpp
namespace gc::config {

using ConfigPathSegment = std::variant<std::string, std::size_t>;

class ConfigPath final {
public:
    ConfigPath(std::initializer_list<ConfigPathSegment> segments);
    [[nodiscard]] ConfigPath Child(std::string field) const;
    [[nodiscard]] ConfigPath Index(std::size_t index) const;
    [[nodiscard]] std::string Render() const;
    auto operator<=>(const ConfigPath&) const = default;
private:
    std::vector<ConfigPathSegment> segments_;
};

enum class ConfigErrorCode : std::uint8_t {
    invalid_value,
    unsupported_value,
    out_of_range,
    required_value,
    invalid_encoding,
    invalid_path,
    incompatible_fields,
    unmet_dependency,
};

struct ConfigError {
    ConfigPath path;
    ConfigErrorCode code{};
    std::string message;
    std::vector<ConfigPath> related_paths;
};

using ConfigErrors = std::vector<ConfigError>;

[[nodiscard]] std::string FormatConfigErrors(
    std::span<const ConfigError> errors);

} // namespace gc::config
```

Render fields with dots and indices as brackets, for example `controller.bindings[3].direction`. Insert errors directly in declaration/index order; do not sort human-readable strings after collection.

- [ ] Implement the compiler and immutable grouping API.

```cpp
class ValidatedConfig final {
public:
    [[nodiscard]] const logging::LoggingSettings& logging() const noexcept;
    [[nodiscard]] const input::InputSettings& input() const noexcept;
    [[nodiscard]] const switch_input::SwitchInputSettings& switch_input() const noexcept;
    [[nodiscard]] const audio::AudioSettings& audio() const noexcept;
    [[nodiscard]] const framerate::FramerateSettings& framerate() const noexcept;
    [[nodiscard]] const absolute_judgement::JudgementSettings& judgement() const noexcept;
    [[nodiscard]] const nesys_service::NesysSettings& nesys() const noexcept;
    [[nodiscard]] const rfid::FeatureSettings& rfid() const noexcept;
    [[nodiscard]] const system_path::SystemPathSettings& system_path() const noexcept;
    [[nodiscard]] bool unlock_all_songs_and_difficulties() const noexcept;
private:
    ValidatedConfig(
        logging::LoggingSettings,
        input::InputSettings,
        switch_input::SwitchInputSettings,
        audio::AudioSettings,
        framerate::FramerateSettings,
        absolute_judgement::JudgementSettings,
        nesys_service::NesysSettings,
        rfid::FeatureSettings,
        system_path::SystemPathSettings,
        bool);
    friend class ConfigCompiler;
    logging::LoggingSettings logging_;
    input::InputSettings input_;
    switch_input::SwitchInputSettings switch_input_;
    audio::AudioSettings audio_;
    framerate::FramerateSettings framerate_;
    absolute_judgement::JudgementSettings judgement_;
    nesys_service::NesysSettings nesys_;
    rfid::FeatureSettings rfid_;
    system_path::SystemPathSettings system_path_;
    bool unlock_all_songs_and_difficulties_{};
};

class ConfigCompiler final {
public:
    [[nodiscard]] static std::expected<ValidatedConfig, ConfigErrors>
    Compile(const ConfigDocument& document) noexcept;
};
```

Feature APIs receive copies/moves of the accessor values during the migration. Do not expose a mutable `ValidatedConfig` API or store it globally after Task 10.

- [ ] Use reflect-cpp's exception-contained `from_value` API for leaf rules. Keep the wrappers private to `ConfigCompiler.cpp`.

```cpp
using TargetFpsValidator = rfl::Validator<
    std::uint32_t,
    rfl::Minimum<60>,
    rfl::Maximum<500>>;

using InputPollValidator = rfl::Validator<
    std::uint32_t,
    rfl::OneOf<
        rfl::EqualTo<125>,
        rfl::EqualTo<250>,
        rfl::EqualTo<500>,
        rfl::EqualTo<1000>>>;

using PercentValidator = rfl::Validator<
    std::uint32_t,
    rfl::Maximum<100>>;

using NonZeroWasapiBufferValidator = rfl::Validator<
    std::uint32_t,
    rfl::Minimum<1>>;

template <class Validator, class T>
bool ValidateLeaf(
    const T& value,
    ConfigPath path,
    ConfigErrorCode code,
    std::string message,
    ConfigErrors& errors) {
    if (Validator::from_value(value)) {
        return true;
    }
    errors.push_back({
        .path = std::move(path),
        .code = code,
        .message = std::move(message),
    });
    return false;
}
```

Use custom reflect-cpp rules for valid physical keys and bounded UTF-8 text. Use `Minimum`, `Maximum`, and `OneOf<EqualTo<...>>` for numeric/enumerated leaf rules. The returned validator object is temporary evidence only; copy the raw value into project-owned settings.

- [ ] Track leaf validity and guard every dependent rule. For example, evaluate `release < press` only if both percent leaves passed; evaluate absolute judgement's poll requirement only if the poll leaf passed; evaluate tagged binding details only after the binding/backend enum and required participating leaves are usable.

```cpp
if (press_valid && release_valid &&
    document.axis_release_threshold_percent() >=
        document.axis_press_threshold_percent()) {
    errors.push_back({
        .path = ConfigPath{"axis_release_threshold_percent"},
        .code = ConfigErrorCode::incompatible_fields,
        .message = "release threshold must be below press threshold",
        .related_paths = {ConfigPath{"axis_press_threshold_percent"}},
    });
}
```

Preserve existing strictness where a field is always semantically required even when a runtime feature is disabled: keyboard keys, controller identity/bindings, NESYS address, and registry values/derived paths still validate because they are complete current-schema document fields. Backend-specific audio fields validate only for their selected concrete alternative, matching the existing conditional policy plus the corrected nonzero WASAPI rule.

- [ ] Compile raw tagged audio/controller fields into the concrete settings from Task 2 only after all required fields for that alternative pass. Never retain `DigitalControlBinding` in runtime `InputSettings`.

- [ ] Convert the derived registry paths and server IP into owned `NesysSettings` during compilation. Keep filesystem creation, driver inspection, registry access, and ASIO probing out of the compiler.

- [ ] Change the temporary `ConfigManager` constructor to strict-parse then compile. Store both the document needed by the old startup repair path and a compiled snapshot. Expose only `const ValidatedConfig& validated() const noexcept` to `DllMain`; feature code must never call it. After `PrepareGameSystemPath` mutates a fallback field, recompile before publishing the replacement snapshot. A compiler failure at either point remains fail-closed.

- [ ] Use CLion MCP formatting/diagnostics, then run the focused contract test and the full Debug graph.

```powershell
cmake --build --preset msvc32-debug --target gc_config_contract_tests
ctest --preset msvc32-debug -R '^ConfigContract$' --output-on-failure
cmake --build --preset msvc32-debug
```

- [ ] Commit.

```powershell
git add -- src/Config/ConfigError.h src/Config/ConfigError.cpp src/Config/ConfigCompiler.h src/Config/ConfigCompiler.cpp src/Config/CMakeLists.txt src/Config/config.h src/Config/config.cpp tests/Config/ConfigContractTests.cpp
git diff --cached --check
git commit -m "Compile config documents into validated settings"
```

---

### Task 4: Make ConfigGUI an invalid-draft editor with complete errors

**Files:**

- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/InputEditorModel.h`
- Modify: `tools/ConfigGUI/InputEditorModel.cpp`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.h`
- Modify: `tools/ConfigGUI/AudioBackendEditorModel.cpp`
- Modify: `tools/ConfigGUI/AudioOperationWorker.h`
- Modify: `tools/ConfigGUI/AudioOperationWorker.cpp`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tests/Config/ConfigContractTests.cpp`

**Interfaces:**

- Consumes: `ConfigDocument`, `ConfigCompiler`, atomic document writer, and existing bounded ASIO probe client.
- Produces: editor startup that accepts semantic-invalid complete documents, complete error rendering, disabled Save on compiler failure, and compile/preflight/write Save order.
- Preserves: ASIO probe timeout as an operational bound only; elapsed time is not evidence of audio validity.

- [ ] Extend the contract test for `FormatConfigErrors`: feed two compiler errors and assert both rendered paths/messages are present in compiler order. Keep the formatter in `ConfigError.cpp` because both ConfigGUI Save failures and loader startup fatals consume it; field association in the live editor still iterates the structured errors directly.

- [ ] Change ConfigGUI load to call only `ParseConfigDocument`. A structurally invalid file still blocks the editor; a semantically invalid document opens and immediately shows errors.

```cpp
auto parsed = gc::config::ParseConfigDocument(text);
if (!parsed) {
    return std::unexpected(parsed.error().message);
}
return LoadedConfig{
    .document = std::move(parsed->document),
    .migrated = parsed->migrations.any(),
};
```

- [ ] Rename GUI model parameters/storage from `InputConfig` to `ConfigDocument`. Keep capture/device discovery operating on raw `DigitalControlBinding` because those are editor concerns.

- [ ] Remove `InputEditorModel::Validate` and its dummy schema/poll/threshold/keyboard values. `AddBinding` and `ReplaceBinding` keep index and capture-identity checks, then update the draft. Shared compilation becomes the only semantic authority.

- [ ] Compile the entire draft each frame after applying model changes, render every error, and gate Save on compilation success.

```cpp
const auto compiled = gc::config::ConfigCompiler::Compile(document);
if (!compiled) {
    ImGui::SeparatorText("Configuration errors");
    for (const auto& error : compiled.error()) {
        const auto path = error.path.Render();
        ImGui::BulletText("%s: %s", path.c_str(), error.message.c_str());
    }
}

ImGui::BeginDisabled(
    !compiled.has_value() || !dirty || audio_worker.busy());
const bool save_requested = ImGui::Button("Save Configuration");
ImGui::EndDisabled();
```

Inline widget restrictions may remain for usability, but they must not substitute for or disagree with compiler acceptance.

- [ ] Change `ValidateAndWriteConfig` to enforce the Save transaction in this order: compile, inspect the `AudioSettings` selection, run bounded ASIO capability preflight only for ASIO, verify the report matches the compiled request, then atomically serialize/write the original `ConfigDocument`.

```cpp
const auto compiled = gc::config::ConfigCompiler::Compile(document);
if (!compiled) {
    return std::unexpected(FormatConfigErrors(compiled.error()));
}
if (const auto* asio = std::get_if<gc::audio::AsioSettings>(
        &compiled->audio().selection())) {
    const gc::audio::AsioProbeRequest request{
        gc::audio::AsioProbeMode::validate,
        asio->driver_name(),
        asio->buffer_frames(),
        asio->output_base_channel(),
    };
    const auto report = asio_probe.Run(
        request,
        gc::audio::kDefaultAsioProbeTimeout);
    if (!report) {
        return std::unexpected(DescribeAsioFailure(report.error()));
    }
}
return gc::config::WriteConfigDocumentAtomically(
           path, document, write_actions)
    .transform_error([](const auto& error) { return error.message; });
```

- [ ] Preserve migration dirtiness: opening a migrated document marks it dirty, but ConfigGUI writes it only after explicit Save.

- [ ] Use CLion MCP formatting/diagnostics for every changed GUI file. Build ConfigGUI and run the contract test.

```powershell
cmake --build --preset msvc32-debug --target ConfigGUI gc_config_contract_tests
ctest --preset msvc32-debug -R '^ConfigContract$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- tools/ConfigGUI/Main.cpp tools/ConfigGUI/InputEditorModel.h tools/ConfigGUI/InputEditorModel.cpp tools/ConfigGUI/AudioBackendEditorModel.h tools/ConfigGUI/AudioBackendEditorModel.cpp tools/ConfigGUI/AudioOperationWorker.h tools/ConfigGUI/AudioOperationWorker.cpp tools/ConfigGUI/CMakeLists.txt tests/Config/ConfigContractTests.cpp
git diff --cached --check
git commit -m "Use shared config compilation in ConfigGUI"
```

---

### Task 5: Build the explicit process startup transaction

**Files:**

- Create: `src/Loader/StartupConfiguration.h`
- Create: `src/Loader/StartupConfiguration.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/Config/ConfigStartupTests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: process role, config path, strict parser, compiler, native-storage probe action, system-root directory actions, and atomic write actions.
- Produces: `PreparedProcessConfiguration` only after final compilation and any required atomic persistence succeed.
- Role guarantee: NESYS takes the same entry point but never calls the game probe, directory preparation, or write callbacks.

- [ ] Add the startup test target using the distributed config as its only full fixture.

```cmake
add_executable(gc_config_startup_tests
        Config/ConfigStartupTests.cpp)
target_link_libraries(gc_config_startup_tests PRIVATE gc_loader_startup)
target_compile_definitions(gc_config_startup_tests PRIVATE
        GC_TEST_CONFIG_PATH="${PROJECT_SOURCE_DIR}/config.toml")
add_test(NAME ConfigStartup COMMAND gc_config_startup_tests)
```

- [ ] Write fake actions that count reads, probes, directory calls, temporary writes, replacements, and removals while retaining destination/temporary text. Add failing tests for these exact cases:

  1. valid current game config, native storage available, no fallback: one read, one probe, zero writes;
  2. recognized audio migration made by replacing the distributed `audio_backend = "directsound"` entry with `enable_wasapi_exclusive_audio = false`: exactly one final write;
  3. registry enabled at the default path, first directory creation fails, fallback succeeds, and native storage is unavailable: both approved repairs appear in the one replaced document and there is exactly one replacement;
  4. a migrated document with `target_fps = 59`: semantic failure and zero writes;
  5. replacement failure: no published result, the fake destination retains its prior contents, and temporary cleanup is requested;
  6. NESYS role with poison probe/directory/write callbacks: one read and none of the poison callbacks invoked.

Run `ConfigStartup` and confirm the target fails to build before the production API exists.

- [ ] Implement explicit actions and output types.

```cpp
namespace gc::loader {

struct ConfigReadActions {
    void* context{};
    std::expected<std::string, std::string> (*read)(
        void*,
        const std::filesystem::path&) noexcept{};
};

struct StartupConfigurationActions {
    ConfigReadActions config_read;
    gc::testmode_storage::NativeStorageProbeResult (*probe_native_storage)(
        void*) noexcept{};
    void* probe_context{};
    gc::system_path::DirectoryActions directories;
    gc::config::AtomicConfigWriteActions config_write;
};

enum class StartupConfigurationStage : std::uint8_t {
    read,
    document,
    semantic,
    system_path,
    persistence,
};

struct StartupConfigurationError {
    StartupConfigurationStage stage{};
    std::string message;
    gc::config::ConfigErrors semantic_errors;
};

enum class StartupConfigChange : std::uint8_t {
    recognized_migration,
    system_path_fallback,
    native_storage_redirect,
};

struct GameProcessConfiguration {
    gc::config::ValidatedConfig settings;
    gc::system_path::RuntimeRoot system_root;
    std::vector<StartupConfigChange> changes;
    bool persisted{};
};

struct NesysProcessConfiguration {
    gc::logging::LoggingSettings logging;
    gc::nesys_service::NesysSettings nesys;
};

using PreparedProcessConfiguration = std::variant<
    GameProcessConfiguration,
    NesysProcessConfiguration>;

[[nodiscard]] StartupConfigurationActions
ProductionStartupConfigurationActions() noexcept;

[[nodiscard]] std::expected<
    PreparedProcessConfiguration,
    StartupConfigurationError>
PrepareProcessConfiguration(
    const std::filesystem::path& config_path,
    gc::nesys_service::ProcessRole role,
    const StartupConfigurationActions& actions =
        ProductionStartupConfigurationActions()) noexcept;

} // namespace gc::loader
```

- [ ] Implement the transaction in this exact order:

  1. validate the read action for both roles, and validate probe/directory/write actions only after selecting the game role;
  2. read the file once;
  3. strict-parse and apply recognized migrations in memory;
  4. compile the parsed document;
  5. if role is NESYS, copy only logging and NESYS settings into `NesysProcessConfiguration`, destroy the full temporary grouping, and return with no game calls or persistence;
  6. if role is game, run the native-storage probe and system-root preparation;
  7. apply only the named fallback field changes to one candidate document and record reasons;
  8. compile the final candidate again;
  9. if migrations or approved repairs changed it, atomically write exactly once;
  10. only after required persistence succeeds, return settings compiled from that same candidate and the prepared runtime root.

Do not create a generic repair registry, test-only repair injection, or a general mutation callback.

- [ ] Ensure a semantic or persistence failure returns no `PreparedProcessConfiguration`. Directory creation may be non-rollbackable, but it still blocks feature publication on failure. Assert the result alternative matches the requested process role; a NESYS result must not contain game settings.

- [ ] Add `gc_loader_startup` after the feature/process subdirectories in `src/CMakeLists.txt`, linking only the libraries required by this orchestration. Keep it separate from `iDmacDrv32` so the startup transaction is testable without loading the DLL.

- [ ] Use CLion MCP formatting/diagnostics, reconfigure CMake, build, and run both config tests.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_config_startup_tests gc_config_contract_tests
ctest --preset msvc32-debug -R '^Config(Contract|Startup)$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- src/Loader/StartupConfiguration.h src/Loader/StartupConfiguration.cpp src/CMakeLists.txt tests/Config/ConfigStartupTests.cpp tests/CMakeLists.txt
git diff --cached --check
git commit -m "Add role-aware configuration startup transaction"
```

---

### Task 6: Move input polling and Switch input to owned settings

**Files:**

- Modify: `src/Input/Polling/InputPollingRuntime.h`
- Modify: `src/Input/Polling/InputPollingRuntime.cpp`
- Modify: `src/Input/Polling/InputMapper.h`
- Modify: `src/Input/Polling/InputMapper.cpp`
- Modify: `src/Input/Win32/ControllerBindingEvaluator.h`
- Modify: `src/Input/Win32/ControllerBindingEvaluator.cpp`
- Modify: `src/Input/Win32/ControllerStateView.h`
- Modify: `src/Input/Switch/SwitchInputPatch.h`
- Modify: `src/Input/Switch/SwitchInputPatch.cpp`
- Modify: `src/Input/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `tests/Config/ConfigContractTests.cpp`

**Interfaces:**

- Consumes: copies of compiled `InputSettings` and `SwitchInputSettings` from the temporary composition-root snapshot.
- Produces: input runtime configuration that exists before lazy `iDmacDrvOpen`, worker-owned settings, and evaluators/mappers that store only concrete controller bindings.
- Preserves: current input transport, foreground clearing, timestamp/judgement publication, and Switch gameplay policy.

- [ ] Add an ownership test that compiles a controller binding, moves/copies `InputSettings` out of the temporary grouping, destroys the document/grouping, creates the production `ControllerBindingEvaluator`, and obtains the expected state from a fake `ControllerStateView`. This protects vector/string/binding lifetime without adding a test-only runtime abstraction.

- [ ] Change the input runtime API and first build to expose every old hidden dependency.

```cpp
[[nodiscard]] std::expected<void, std::string>
ConfigureInputPollingRuntime(InputSettings settings) noexcept;

InputPollingOpenResult OpenInputPollingRuntime();
void CloseInputPollingRuntime() noexcept;
std::uint32_t ReadPublishedInput() noexcept;
```

`ConfigureInputPollingRuntime` succeeds once before the first open, owns its argument in `RuntimeState`, and rejects reconfiguration while starting/open. `OpenInputPollingRuntime` returns a clear failure if configuration was never published.

- [ ] Pass an owned settings copy into each worker thread. Do not capture a reference to `RuntimeState::settings`.

```cpp
auto worker_settings = *state.settings;
state.worker = std::thread(
    [&state,
     stop_event = state.stop_event,
     settings = std::move(worker_settings)]() mutable {
        WorkerMain(state, stop_event, std::move(settings));
    });
```

`NativeInputWorker` receives `InputSettings` by value and initializes its mapper, evaluator, controller identity, logging fields, and publication policy from that owned value. Remove `Config/config.h` and every `ConfigManager` query from the worker.

- [ ] Change `ControllerBindingEvaluator` and `InputMapper` to own `ControllerBinding` alternatives. Add a nonvirtual typed `ControllerStateView::Activation(const ControllerBinding&)` visitor that translates one valid typed alternative into the existing raw device query seam; keep raw `DigitalControlBinding` overloads for GUI discovery/capture only.

```cpp
std::expected<ControllerBindingEvaluator, std::string>
ControllerBindingEvaluator::Create(
    std::span<const ControllerBinding> bindings,
    std::uint32_t press_percent,
    std::uint32_t release_percent);

InputMapper::InputMapper(
    InputMode mode,
    std::span<const KeyboardBinding> keyboard,
    std::span<const ControllerBinding> controller);
```

- [ ] Change Switch initialization to take `SwitchInputSettings` by value. It may reduce the setting to its enum during installation, but no hook may query the config layer later.

```cpp
void SwitchInputPatchInit(SwitchInputSettings settings) noexcept;
```

- [ ] In the temporary `ConfigManager` era, update `DllMain` to copy compiled settings into both APIs before any exported iDmac open can start the worker. Keep the composition-root copy explicit.

- [ ] Remove `gc_config` from `gc_input` link libraries. Input types and runtime must compile without config headers.

- [ ] Use CLion MCP formatting/diagnostics. Build `gc_input`, `iDmacDrv32`, and the contract test; run the ownership/contract test.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_input iDmacDrv32 gc_config_contract_tests
ctest --preset msvc32-debug -R '^ConfigContract$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- src/Input/Polling/InputPollingRuntime.h src/Input/Polling/InputPollingRuntime.cpp src/Input/Polling/InputMapper.h src/Input/Polling/InputMapper.cpp src/Input/Win32/ControllerBindingEvaluator.h src/Input/Win32/ControllerBindingEvaluator.cpp src/Input/Win32/ControllerStateView.h src/Input/Switch/SwitchInputPatch.h src/Input/Switch/SwitchInputPatch.cpp src/Input/CMakeLists.txt src/Loader/DllMain.cpp tests/Config/ConfigContractTests.cpp
git diff --cached --check
git commit -m "Give input runtimes owned compiled settings"
```

---

### Task 7: Move audio initialization and lazy detour state to owned settings

**Files:**

- Modify: `src/Audio/AudioBackendController.h`
- Modify: `src/Audio/AudioBackendController.cpp`
- Modify: `src/Audio/AudioPatch.h`
- Modify: `src/Audio/AudioPatch.cpp`
- Modify: `src/Audio/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `tests/Config/ConfigContractTests.cpp`

**Interfaces:**

- Consumes: `AudioSettings` by value.
- Produces: a process-lifetime audio detour state constructed before hook publication and owning all controller/ASIO strings and values.
- Preserves: backend selection, WASAPI/ASIO startup policy, ASIO focus-loss recovery, exact-clock policy, fatal behavior, and existing hook transaction semantics.

- [ ] Extend the compiler ownership test: compile ASIO settings, copy/move `AudioSettings` out, destroy the source document and `ValidatedConfig`, then assert the driver name, buffer frames, and output channel are still intact. This is the meaningful public ownership seam; do not add a test-only audio singleton accessor.

- [ ] Change the public initialization API and build once to identify all old implicit reads.

```cpp
bool AudioPatchInit(AudioSettings settings) noexcept;
[[nodiscard]] bool IsAudioHookCommitted() noexcept;
```

- [ ] Convert `AudioSettings` to `AudioBackendControllerConfig` inside audio. Remove `Config/AudioConfig.h` from `AudioBackendController.h`, use `gc::audio::AudioBackend`, and make the production controller factory own its controller config by value rather than by reference.

```cpp
class ProductionAudioBackendControllerFactory final
    : public IAudioBackendControllerFactory {
public:
    ProductionAudioBackendControllerFactory(
        AudioBackendControllerConfig config,
        IWasapiOutputBackendFactory& wasapi,
        IAsioOutputBackendFactory& asio,
        IAudioBackendControllerReporter& reporter) noexcept
        : config_(std::move(config)),
          wasapi_(wasapi),
          asio_(asio),
          reporter_(reporter) {}
private:
    AudioBackendControllerConfig config_;
    IWasapiOutputBackendFactory& wasapi_;
    IAsioOutputBackendFactory& asio_;
    IAudioBackendControllerReporter& reporter_;
};
```

- [ ] Construct the process-lifetime `ProductionDetourState` from the passed settings before enabling the DirectSound hook. Its member graph owns the `AudioSettings`, the fully converted controller config, and the exact-clock flag; the factory then takes its own controller-config copy. Delete `production_controller_config()` and every `ConfigManager` query.

```cpp
struct ProductionDetourState {
    explicit ProductionDetourState(AudioSettings settings);

    AudioSettings settings;
    AudioBackendControllerConfig config;
    ProductionDiagnosticContext diagnostics;
    ProductionAudioBackendControllerReporter reporter;
    ProductionWasapiOutputBackendFactory wasapi;
    ProductionAsioOutputBackendFactory asio;
    ProductionAudioBackendControllerFactory factory;
};
```

The state is still deliberately process-lifetime, but it is created from owned startup data. The detour must never lazily sample a stack object or global config manager.

- [ ] Update `DllMain` to pass a settings copy from the temporary compiled snapshot. Preserve ordering: audio hook commitment precedes absolute-judgement capability initialization and framerate audio-capability consumption.

- [ ] Remove `gc_config` from `gc_audio` link libraries.

- [ ] Use CLion MCP formatting/diagnostics. Build audio, DLL, and contract targets; run relevant tests.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_audio iDmacDrv32 gc_config_contract_tests gc_exact_asio_clock_tests
ctest --preset msvc32-debug -R '^(ConfigContract|ExactAsioClock)$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- src/Audio/AudioBackendController.h src/Audio/AudioBackendController.cpp src/Audio/AudioPatch.h src/Audio/AudioPatch.cpp src/Audio/CMakeLists.txt src/Loader/DllMain.cpp tests/Config/ConfigContractTests.cpp
git diff --cached --check
git commit -m "Give audio hooks owned compiled settings"
```

---

### Task 8: Move NESYS, RFID, and storage consumers to validated values

**Files:**

- Modify: `src/Nesys/NesysServicePatch.h`
- Modify: `src/Nesys/NesysServicePatch.cpp`
- Modify: `src/Nesys/Network/ServerAddressOverride.h`
- Modify: `src/Nesys/Network/ServerAddressOverride.cpp`
- Modify: `src/Nesys/Registry/RegistryConfigOverride.h`
- Modify: `src/Nesys/Registry/RegistryConfigOverride.cpp`
- Modify: `src/Nesys/CMakeLists.txt`
- Modify: `src/Rfid/Feature.h`
- Modify: `src/Rfid/Feature.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `tests/Config/ConfigContractTests.cpp`

**Interfaces:**

- Consumes: owned `NesysSettings`, owned `FeatureSettings`, prepared `RuntimeRoot`, and process role.
- Produces: hook objects constructed from already parsed server/registry values and RFID/storage state constructed without config access.
- Preserves: current role feature plan, network/registry hooks, storage fallback policy, system-path routing, and RFID key behavior.

- [ ] Extend `ConfigContract` to assert compiled NESYS settings own parsed IPv4 octets/text and derived registry paths, and RFID settings own the card key/redirect flag after the document is destroyed.

- [ ] Change the APIs and build to expose all remaining config dependencies.

```cpp
bool NesysServicePatchInit(
    HMODULE loader_module,
    ProcessRole role,
    NesysSettings settings) noexcept;

[[nodiscard]] std::expected<void, gc::rfid::FeatureError>
InitializeFeature(
    const gc::system_path::RuntimeRoot& system_root,
    gc::rfid::FeatureSettings settings) noexcept;
```

- [ ] Change server and registry initialization to consume compiled values, not validate raw document structures a second time.

```cpp
bool InitializeServerAddressOverride(ServerAddressState state) noexcept;

bool InitializeRegistryConfigOverride(
    ProcessRole role,
    RegistryOverrideValues values) noexcept;
```

`NesysServicePatchInit` resolves its feature plan from `NesysSettings`, moves the owned server/registry state into process-lifetime hook owners, and never includes `Config/config.h`.

- [ ] Change RFID feature state construction to use `FeatureSettings::card_read_key()` and `testmode_storage_redirect_enabled()`. It still converts the key to a Win32 virtual key at feature initialization and retains current logging/failure behavior.

- [ ] Update `DllMain` to pass settings copies from the temporary compiled snapshot. The NESYS role receives only logging/NESYS settings; game-only RFID/storage initialization remains under the game-role branch.

- [ ] Remove `gc_config` from `gc_nesys` and `gc_rfid_feature` link libraries. `gc_config` may depend on passive headers and the small network parsing target, but runtime feature libraries must not depend back on config.

- [ ] Use CLion MCP formatting/diagnostics. Build NESYS, RFID, DLL, and contract/startup tests.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_nesys gc_rfid_feature iDmacDrv32 gc_config_contract_tests gc_config_startup_tests
ctest --preset msvc32-debug -R '^Config(Contract|Startup)$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- src/Nesys/NesysServicePatch.h src/Nesys/NesysServicePatch.cpp src/Nesys/Network/ServerAddressOverride.h src/Nesys/Network/ServerAddressOverride.cpp src/Nesys/Registry/RegistryConfigOverride.h src/Nesys/Registry/RegistryConfigOverride.cpp src/Nesys/CMakeLists.txt src/Rfid/Feature.h src/Rfid/Feature.cpp src/CMakeLists.txt src/Loader/DllMain.cpp tests/Config/ConfigContractTests.cpp
git diff --cached --check
git commit -m "Inject validated settings into NESYS and RFID"
```

---

### Task 9: Move framerate and judgement patches to explicit settings

**Files:**

- Modify: `src/Patches/Framerate/FrameratePatch.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h`
- Modify: `src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `src/Loader/DllMain.cpp`

**Interfaces:**

- Consumes: `FramerateSettings`, `JudgementSettings`, and the explicit audio-hook capability result.
- Produces: patch initialization with no global config reads.
- Preserves: authored/native frame-unit policy, absolute judgement invariants, exact audio domain selection, timer-freeze policy, diagnostics, grading, and fatal contracts.

- [ ] Change the public APIs first and build to enumerate all compile-time call sites.

```cpp
[[nodiscard]] bool FrameratePatchInit(
    FramerateSettings settings,
    bool authoritative_audio_clock_available);

void InitializeAbsoluteJudgementOrFatal(
    JudgementSettings settings) noexcept;
```

- [ ] In framerate initialization, obtain target FPS and timer-freeze policy only from the owned function argument. Move any values needed beyond initialization into `FramerateRuntime`; do not retain a reference to the argument.

- [ ] In judgement initialization, obtain enablement, diagnostics fields, input rate, and expected exact-clock domain only from `JudgementSettings`. Keep `IsAudioHookCommitted()` as an explicit runtime capability query; it is not configuration.

- [ ] Remove config enum formatting from judgement diagnostics and use `AudioBackendName(gc::audio::AudioBackend)` from `AudioSettings.h`, so the patch does not include config.

- [ ] Update `DllMain` to pass compiled settings by value while preserving order: input transport configured, audio initialized, judgement initialized, then framerate receives audio commitment.

- [ ] Remove `gc_config` from `gc_runtime_patches` link libraries and remove all config includes from patch sources.

- [ ] Use CLion MCP formatting/diagnostics. Build the patch library and DLL, then run the existing focused tests plus both config tests.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target gc_runtime_patches iDmacDrv32 gc_exact_asio_clock_tests gc_config_contract_tests gc_config_startup_tests
ctest --preset msvc32-debug -R '^(ExactAsioClock|ConfigContract|ConfigStartup)$' --output-on-failure
```

- [ ] Commit.

```powershell
git add -- src/Patches/Framerate/FrameratePatch.h src/Patches/Framerate/FrameratePatch.cpp src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.h src/Patches/AbsoluteJudgement/AbsoluteJudgementPatch.cpp src/Patches/CMakeLists.txt src/Loader/DllMain.cpp
git diff --cached --check
git commit -m "Inject settings into timing patches"
```

---

### Task 10: Switch the composition root to the startup transaction and delete compatibility scaffolding

**Files:**

- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Loader/StartupConfiguration.h`
- Modify: `src/Loader/StartupConfiguration.cpp`
- Modify: `src/Config/ConfigDocument.h`
- Modify: `src/Config/ConfigDocument.cpp`
- Modify: `src/Config/CMakeLists.txt`
- Delete: `src/Config/config.h`
- Delete: `src/Config/config.cpp`
- Delete: `src/Config/AudioConfig.h`
- Delete: `src/Config/AudioConfig.cpp`
- Delete: `src/Config/NativeInputConfig.cpp`
- Modify: `src/Config/NativeInputConfig.h` (raw nested document types only)
- Modify: `src/Config/RegistryConfig.h` (raw nested document types plus derived-path declaration only)
- Modify: `src/Config/RegistryConfig.cpp` (derived-path conversion only)
- Modify: `src/CMakeLists.txt`
- Modify: any remaining source/header discovered by exact symbol search

**Interfaces:**

- Consumes: `PrepareProcessConfiguration` and the feature APIs migrated in Tasks 6-9.
- Produces: one startup snapshot, explicit role distribution, and no `ConfigManager`, legacy first-error validator, or `InputConfig` alias.
- Preserves: current startup fatal/UI formatting, game/NESYS initialization order, and fail-closed behavior.

- [ ] Before editing, run exact symbol searches and save the output in the task notes so no consumer is missed.

```powershell
rg -n "\bConfigManager\b|\bInputConfig\b|ValidateInputConfig|ParseAndValidateInputConfig" src tools tests
rg -n "#include \"Config/" src/Audio src/Input src/Nesys src/Patches src/Rfid
```

- [ ] Replace `ConfigManager::instance()` in `DllMain` with one call to `PrepareProcessConfiguration(current_path / "config.toml", role)`. Format `StartupConfigurationError` at this outer boundary and return `FALSE` on failure without letting exceptions escape.

```cpp
auto prepared = gc::loader::PrepareProcessConfiguration(
    std::filesystem::current_path() / "config.toml",
    role);
if (!prepared) {
    PublishConfigurationStartupFatal(prepared.error());
    return FALSE;
}
```

- [ ] Distribute copies/moves from the role-specific result. A service result has no game settings; a game result owns the prepared system root and the full temporary settings grouping.

```cpp
if (!gc::nesys_service::ShouldRunGameOnlyInitialization(role)) {
    auto* service =
        std::get_if<gc::loader::NesysProcessConfiguration>(&*prepared);
    if (service == nullptr) {
        PublishConfigurationRoleMismatchFatal(role);
        return FALSE;
    }
    ApplyConfiguredLogLevel(service->logging);
    if (!gc::nesys_service::NesysServicePatchInit(
            hModule, role, std::move(service->nesys))) {
        return FALSE;
    }
} else {
    auto* game_result =
        std::get_if<gc::loader::GameProcessConfiguration>(&*prepared);
    if (game_result == nullptr) {
        PublishConfigurationRoleMismatchFatal(role);
        return FALSE;
    }
    auto game = std::move(*game_result);
    auto settings = std::move(game.settings);
    ApplyConfiguredLogLevel(settings.logging());

    if (!gc::song_unlock::SongUnlockPatchInit(
            settings.unlock_all_songs_and_difficulties())) {
        return FALSE;
    }
    if (!gc::nesys_service::NesysServicePatchInit(
            hModule, role, settings.nesys())) {
        return FALSE;
    }

    const auto input_configured =
        gc::input::ConfigureInputPollingRuntime(settings.input());
    if (!input_configured) {
        PublishInputConfigurationFatal(input_configured.error());
        return FALSE;
    }
    if (!gc::audio::AudioPatchInit(settings.audio())) {
        return FALSE;
    }
    gc::absolute_judgement::InitializeAbsoluteJudgementOrFatal(
        settings.judgement());
    const auto rfid = gc::rfid::InitializeFeature(
        game.system_root, settings.rfid());
    if (!rfid) {
        return FALSE;
    }
    if (!gc::framerate::FrameratePatchInit(
            settings.framerate(),
            gc::audio::IsAudioHookCommitted())) {
        return FALSE;
    }
    gc::switch_input::SwitchInputPatchInit(settings.switch_input());
}
```

Keep the existing no-setting game initializers (test-mode timing and renderer-device-loss) in their current relative positions. Use the existing fatal-reporting functions rather than replacing their diagnostics with bare `return FALSE`; add `PublishInputConfigurationFatal(std::string_view)` at the outer loader boundary for the input-publication error. After the last call, allow the game settings grouping to die; delayed users must already own their values.

- [ ] Move log-level formatting to the logging domain and pass `LoggingSettings` to `ApplyConfiguredLogLevel`. Pass the song-unlock boolean directly from `ValidatedConfig` during game-only startup.

- [ ] Delete `ConfigManager`, all legacy getters, first-error `ValidateInputConfig`, `ParseAndValidateInputConfig`, `ParseAndValidateInputConfigDocument`, and the temporary `InputConfig` alias. Remove duplicated manual leaf validators superseded by `ConfigCompiler`; retain only genuinely shared parsing/serialization helpers.

- [ ] Remove obsolete config source files from `gc_config` and reduce its public dependencies. Runtime feature libraries must not link `gc_config`; `iDmacDrv32`, ConfigGUI, the startup library, and config tests may link it.

- [ ] Re-run exact symbol searches. Expected result: no `ConfigManager`, `InputConfig`, or legacy validation/parser names; no feature-runtime config includes.

```powershell
rg -n "\bConfigManager\b|\bInputConfig\b|ValidateInputConfig|ParseAndValidateInputConfig" src tools tests
rg -n "#include \"Config/" src/Audio src/Input src/Nesys src/Patches src/Rfid
```

- [ ] Use CLion MCP formatting on every changed C++ file, reload CMake, and inspect diagnostics on `DllMain.cpp`, startup/config sources, and every changed feature entry point.

- [ ] Build the entire Debug graph and run all Debug tests.

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug --output-on-failure
```

- [ ] Commit.

```powershell
git add -A -- src tools tests
git diff --cached --check
git commit -m "Remove the global configuration manager"
```

---

### Task 11: Complete local verification and change accounting

**Files:**

- Modify only if verification exposes a real defect: files already in scope above.
- Do not modify: deployed/runtime files under `H:\gc`.

**Interfaces:**

- Consumes: final source tree and both x86 presets.
- Produces: reproducible static/build evidence, clean diagnostics, and a precise worktree/commit accounting.

- [ ] Run policy-focused searches. Inspect every hit rather than turning these into source-text tests.

```powershell
rg -n "\bConfigManager\b|\bInputConfig\b|ValidateInputConfig|ParseAndValidateInputConfig" src tools tests
rg -n "ConfigDocument|rfl::|Config/" src/Audio src/Input src/Nesys src/Patches src/Rfid
rg -n "WriteConfigDocumentAtomically|PrepareProcessConfiguration" src tools tests
```

Expected architecture:

- `ConfigDocument`/reflect-cpp references are limited to config, ConfigGUI, loader startup orchestration, and config tests;
- atomic writes originate only from ConfigGUI explicit Save and the game-owned startup transaction;
- delayed feature code contains no config-layer dependency.

- [ ] Use CLion MCP `reformat_file` for every changed C/C++ file that has not yet been formatted. Reload CMake and run `get_file_problems` on all changed C/C++ files, filtering INFO noise but resolving every error and justified warning. Do not substitute MSVC diagnostics for CLion diagnostics.

- [ ] Run complete Debug verification.

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug --output-on-failure
```

- [ ] Run complete Release verification.

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release --output-on-failure
```

- [ ] Check formatting/whitespace and account for every changed file.

```powershell
git diff --check
git status --short
git diff --stat
git log --oneline -12
```

- [ ] If verification required fixes, commit each coherent fix separately after its targeted regression test and full affected build pass. If no fixes were required, do not create an empty verification commit.

- [ ] Report exactly what static/build evidence passed and state that game/ConfigGUI interactive acceptance and deployment were not performed.
