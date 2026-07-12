# WASAPI Audio Opt-In Configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one required, default-off configuration field that controls the future WASAPI exclusive backend and round-trips through ConfigGUI.

**Architecture:** Extend the existing strict reflect-cpp `ExperimentalConfig`; do not add a compatibility default wrapper. Runtime code receives one Boolean getter, while ConfigGUI edits the same model and the distributed TOML explicitly contains the new key.

**Tech Stack:** C++23, reflect-cpp v0.19.0 TOML, ImGui v1.91.9b, CMake/Ninja, CTest, existing `ConfigFeatureTests`.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`; do not modify deployed `H:\gc\config.toml`.
- Add exactly `experimental.enable_wasapi_exclusive_audio`.
- Do not use `rfl::DefaultIfMissing`; missing-key parsing must fail even when the feature would otherwise be disabled.
- The C++ default, ConfigGUI default, and distributed `config.toml` value are all `false`.
- This plan adds no hook and no audio runtime code.

---

## Prerequisites

- Start from the committed design `61908fa` or a descendant.
- Preserve unrelated untracked files, including `docs/superpowers/plans/2026-07-12-registry-config-virtualization.md` if it is still present.

## File Structure

- Modify `config.h`: field and getter.
- Modify `config.toml`: required distributed value.
- Modify `GUI_main.cpp`: experimental checkbox and tooltip.
- Modify `tests/ConfigFeatureTests.cpp`: valid fixtures, defaults, round trip, and missing-key regression.

### Task 1: Strict Schema and Default-Off UI

**Files:**
- Modify: `config.h:54-60,181-187`
- Modify: `config.toml:47-52`
- Modify: `GUI_main.cpp:439-460`
- Modify: `tests/ConfigFeatureTests.cpp:51-105,209-375,405-421`

**Interfaces:**
- Consumes: `ExperimentalConfig`, `InputConfig`, `ConfigManager`, and ConfigGUI's existing dirty/save flow.
- Produces:
  - `rfl::Rename<"enable_wasapi_exclusive_audio", bool> ExperimentalConfig::enable_wasapi_exclusive_audio`
  - `bool ConfigManager::GetEnableWasapiExclusiveAudio() const`
  - ConfigGUI label `WASAPI exclusive low-latency audio`

- [ ] **Step 1: Add failing schema assertions**

In every valid `[experimental]` test fixture, add `enable_wasapi_exclusive_audio`. Use `false` in default fixtures and `true` in `kEnabledExperimentalConfig`.

Add after the existing default assertions:

```cpp
failures += expect_bool(
    upgraded_defaults.experimental().enable_wasapi_exclusive_audio(),
    false,
    "upgraded default enable_wasapi_exclusive_audio");
failures += expect_bool(
    generated_toml.find("enable_wasapi_exclusive_audio = false") !=
        std::string::npos,
    true,
    "ConfigGUI default WASAPI field serialization");
```

Add after the enabled assertions:

```cpp
failures += expect_bool(
    custom.experimental().enable_wasapi_exclusive_audio(),
    true,
    "custom enable_wasapi_exclusive_audio");
const auto serialized_wasapi = rfl::toml::write(custom);
const auto reparsed_wasapi = parse_config(serialized_wasapi);
failures += expect_bool(
    reparsed_wasapi.experimental().enable_wasapi_exclusive_audio(),
    true,
    "WASAPI field TOML round trip");
```

Add an isolated missing-key case whose table contains every older key:

```cpp
failures += expect_parse_failure(
    std::string(kRequiredConfigPrefix) + kDefaultCardReadConfig + R"toml(
[experimental]
enable_120fps_timer_patches = false
enable_testmode_storage_redirect = false
enable_timer_freeze_patches = false
enable_nesys_service_adapter_patch = true
)toml",
    "missing enable_wasapi_exclusive_audio");
```

- [ ] **Step 2: Verify the test is red**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests'
```

Expected: compilation fails because the new member is not defined.

- [ ] **Step 3: Add the exact model and getter**

Append the field without a default wrapper:

```cpp
struct ExperimentalConfig
{
    rfl::Rename<"enable_120fps_timer_patches", bool> enable_120fps_timer_patches = false;
    rfl::Rename<"enable_testmode_storage_redirect", bool> enable_testmode_storage_redirect = false;
    rfl::Rename<"enable_timer_freeze_patches", bool> enable_timer_freeze_patches = false;
    rfl::Rename<"enable_nesys_service_adapter_patch", bool> enable_nesys_service_adapter_patch = true;
    rfl::Rename<"enable_wasapi_exclusive_audio", bool> enable_wasapi_exclusive_audio = false;
};
```

Add beside the existing experimental getters:

```cpp
bool GetEnableWasapiExclusiveAudio() const {
    return config.experimental.value().enable_wasapi_exclusive_audio.value();
}
```

- [ ] **Step 4: Add the distributed key and GUI control**

Append to `[experimental]` in repository `config.toml`:

```toml
enable_wasapi_exclusive_audio = false
```

Add after the NESYS experimental checkbox:

```cpp
bool enable_wasapi_exclusive_audio =
    g_config.experimental().enable_wasapi_exclusive_audio();
if (ImGui::Checkbox(
        "WASAPI exclusive low-latency audio",
        &enable_wasapi_exclusive_audio)) {
    g_config.experimental().enable_wasapi_exclusive_audio =
        enable_wasapi_exclusive_audio;
    g_config_dirty = true;
}
ImGui::SameLine();
ImGui::TextDisabled("(?)");
if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Uses the default console endpoint in exclusive 44.1 kHz PCM16 mode.\n"
        "Disable this option if exclusive endpoint initialization fails.");
}
```

- [ ] **Step 5: Verify build, strict parsing, and serialization**

Run:

```powershell
& $env:ComSpec /c '"C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars32.bat" && cmake --build build-msvc32-latest --target ConfigFeatureTests ConfigGUI && ctest --test-dir build-msvc32-latest -R "^ConfigFeatureTests$" --output-on-failure'
```

Expected: both targets build and `ConfigFeatureTests` passes.

- [ ] **Step 6: Commit**

```powershell
git add -- config.h config.toml GUI_main.cpp tests/ConfigFeatureTests.cpp
git commit -m "feat: add WASAPI exclusive audio opt-in"
```

## Completion Gate

The plan is complete only when removing `enable_wasapi_exclusive_audio` from an otherwise valid TOML fixture fails parsing, and ConfigGUI serialization emits the field as `false` by default.
