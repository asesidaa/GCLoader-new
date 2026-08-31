# Windowed Widescreen Stage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in, fixed-size windowed mode whose upright client, Direct3D 9 backbuffer, and perspective scene use a configured size of at least 720 x 1280, while all approved 2D rendering remains an unscaled, centered 720 x 1280 canvas.

**Architecture:** Compile strict TOML into immutable widescreen settings, derive all geometry through pure policies, and install one guarded game-binary hook transaction. A segmented Direct3D 9 compositor preserves native 2D blending and draw order while render-space-aware hooks widen only verified perspective passes. The existing renderer device-loss module owns the single reset lifecycle seam.

**Tech Stack:** C++23, Win32, Direct3D 9, Microsoft WRL `ComPtr`, SafetyHook, reflect-cpp/TOML, CMake/Ninja MSVC x86, focused CTest executables, IDA Pro/Hex-Rays through the existing IDA-CLI daemon.

**Spec:** `docs/superpowers/specs/2026-08-26-windowed-widescreen-stage-design.md` at approved commit `d3e9394d6207ae5b448cfd11363719f2c17caa92`.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader`. `H:\gc`, including `game471.exe.i64`, `data\system.cfg`, `_clip.dat`, logs, and deployed binaries, is read-only evidence unless a later user request explicitly authorizes deployment.
- Preserve unrelated work on `fix/asio-lifecycle-recovery`. The plan baseline is source commit `ada301b0216d2b02d896f53184ff86d688cab00c`; re-read overlapping files before each edit if the branch advances.
- V1 is an ordinary decorated, movable, minimizable, closable, fixed-size window on an unrotated desktop. Do not add fullscreen, borderless, maximize, live resize, monitor selection, Windows/monitor rotation, transposed targets, final-image rotation, UI scaling, or UI anchoring.
- Keep `data\system.cfg` and every `_clip.dat` file unchanged. Enabled policy overrides the already-parsed native config in memory; disabled policy installs no widescreen hooks, performs no monitor/device probes, and preserves native behavior.
- Keep `NativeCanvasSize` exactly 720 x 1280. Do not widen orthographic chart, HUD, menu, or gameplay-effect passes.
- Do not use `StretchRect`, add a second `BeginScene`/`EndScene`, read pixels back to the CPU, or stretch the final backbuffer through a mismatched client.
- Do not add a generic render callback registry. The reset seam remains renderer-specific and has exactly one optional compositor participant.
- Keep render/getter/copy success paths free of per-call logging and dynamic allocation. Unknown task diagnostics are bounded and deduplicated.
- Keep all IDA/Python analysis in a real `.py` file created with a file edit. Never use `python -c`, shell here-strings, inline JSON, or an escaped script embedded in PowerShell. Invoke the saved file by path only.
- Reuse the existing `H:\gc\game471.exe.i64` daemon. Do not stop, kill, restart, or replace IDA or any other process.
- Automated tests and builds are static evidence. Only the operator-run game matrix in Task 11 can establish visual, device-loss, and performance acceptance.
- Execute build commands from an already-open x86 MSVC Developer PowerShell. Set `GC_ASIO_SDK_DIR` directly in that shell; do not wrap the build in an escaped `cmd /c` string.

---

## Current-Code Baseline

- `ConfigDocument` is the only mutable TOML-shaped model. `ConfigCompiler` constructs immutable feature-owned settings and `ValidatedConfig` owns them by value.
- `PrepareProcessConfiguration` reads and compiles once. `Loader/DllMain.cpp` is the game/NESYS composition root; game-only runtime patches begin after validated settings are available.
- `RendererDeviceLossPatch` currently owns six guarded mid-hooks and ten byte/continuation contracts for native vertex/index-buffer loss recovery. Widescreen must extend this owner with one reset lifecycle seam, not install an unrelated reset path.
- The current focused configuration suites are `ConfigContractTests.cpp` and `ConfigStartupTests.cpp`. The old broad renderer suite was intentionally removed. New tests must use independent geometry/state/rollback oracles, not copied executable byte tables or source-grep assertions.
- Current game startup order is timing settings, renderer device-loss, audio, absolute judgement, RFID, framerate, then Switch input. Widescreen belongs immediately after renderer device-loss and before audio.
- A source-wide overlap search found none of the selected widescreen RVAs in existing patches. This must be repeated after implementation because framerate and renderer work are active areas.

## Final File Map

### New production and analysis files

- `src/Patches/WindowedWidescreen/StageClipPolicy.h`
- `src/Patches/WindowedWidescreen/StageClipPolicy.cpp`
- `src/Patches/WindowedWidescreen/WindowedWidescreenSettings.h`
- `src/Patches/WindowedWidescreen/ResolutionModel.h`
- `src/Patches/WindowedWidescreen/ResolutionModel.cpp`
- `src/Patches/WindowedWidescreen/ProjectionPolicy.h`
- `src/Patches/WindowedWidescreen/ProjectionPolicy.cpp`
- `src/Patches/WindowedWidescreen/NativeWindowPolicy.h`
- `src/Patches/WindowedWidescreen/NativeWindowPolicy.cpp`
- `src/Patches/WindowedWidescreen/RenderSpacePolicy.h`
- `src/Patches/WindowedWidescreen/RenderSpacePolicy.cpp`
- `src/Patches/WindowedWidescreen/PassClassifier.h`
- `src/Patches/WindowedWidescreen/PassClassifier.cpp`
- `src/Patches/WindowedWidescreen/NativeCanvasCompositor.h`
- `src/Patches/WindowedWidescreen/NativeCanvasCompositor.cpp`
- `src/Patches/WindowedWidescreen/D3D9CompositorDevice.h`
- `src/Patches/WindowedWidescreen/D3D9CompositorDevice.cpp`
- `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.h`
- `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp`
- `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h`
- `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.cpp`
- `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.h`
- `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.h`
- `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.cpp`
- `tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp`
- `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`
- `tools/analysis/windowed_widescreen_contract_audit.py`

### Existing files to modify

- `config.toml`
- `src/Config/ConfigDocument.h`
- `src/Config/ConfigCompiler.h`
- `src/Config/ConfigCompiler.cpp`
- `src/Patches/CMakeLists.txt`
- `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- `src/Loader/DllMain.cpp`
- `tools/ConfigGUI/Main.cpp`
- `tests/CMakeLists.txt`
- `tests/Config/ConfigContractTests.cpp`
- `tests/Config/ConfigStartupTests.cpp`

No ASIO, WASAPI, framerate, absolute-judgement, RFID, NESYS, input-polling, deployed runtime, `system.cfg`, or asset file is expected to change.

## Frozen Native ABI Contract

These contracts were freshly read from `H:\gc\game471.exe.i64` through the active IDA-CLI/idalib backend on 2026-09-01. Virtual addresses use preferred image base `0x00400000`; implementation stores RVAs and rejects any other loaded image base.

### Function and mid-hook byte contracts

| Site | RVA | Expected original bytes | Use |
| --- | ---: | --- | --- |
| Config apply | `0x0023C360` | `55 8B EC 83 EC 14 E8 E5 F8 FF FF` | Inline; call original, then apply enabled native config override. |
| Window/device creation | `0x0005B8A0` | `83 EC 64 53 55 56 57 6A 30 33 ED 8D` | Inline; call original, then verify HWND/device/client/backbuffer and create resources before first frame. |
| Frame begin | `0x0005AC70` | `51 53 56 8D 44 24 08 57 50 8B F1 E8` | Inline; bind wide scene/depth before native clear and `BeginScene`. |
| Frame end | `0x0005ACE0` | `8B 41 08 8B 08 8B 91 A8 00 00 00 50` | Inline; close native space and copy scene to real backbuffer before native `EndScene`. |
| Task dispatch | `0x0005C1B0` | `8B 09 8B 01 8B 50 10 FF E2 CC CC CC` | Inline `__thiscall`; classify `*task_node` before invoking vtable slot `+0x10`. |
| Screen width int | `0x00052F20` | `A1 E8 6F 78 00 C3` | Inline getter. |
| Screen height int | `0x00052F30` | `A1 EC 6F 78 00 C3` | Inline getter. |
| Screen width float | `0x00052F40` | `D9 05 F0 6F 78 00 C3` | Inline getter. |
| Screen height float | `0x00052F50` | `D9 05 F4 6F 78 00 C3` | Inline getter. |
| Current-target width int | `0x00052FA0` | `A1 F8 6F 78 00 C3` | Inline getter. |
| Current-target height int | `0x00052FB0` | `A1 FC 6F 78 00 C3` | Inline getter. |
| Current-target width float | `0x00052FC0` | `D9 05 00 70 78 00 C3` | Inline getter. |
| Current-target height float | `0x00052FD0` | `D9 05 04 70 78 00 C3` | Inline getter. |
| Viewport reset | `0x00053140` | `8B 4C 24 04 33 C0 83 EC 20 3B C8 0F` | Inline; explicit viewport arguments pass through, null uses current render-space dimensions. |
| Primary projection | `0x0023F5F0` | `55 8B EC 83 EC 54 56 57 D9 05 D4 BC 6F 00` | Inline `float* __cdecl(float*, int, float)`. |
| Oriented projection | `0x0023F660` | `55 8B EC 81 EC CC 00 00 00 56 57 D9 05 D4 BC` | Inline `float* __cdecl(float*, float*, float)`. |
| Mouse/debug poll | `0x000B06B0` | `55 8B EC 83 EC 08 89 4D F8 8B 45 F8` | Inline `__thiscall`; map returned client coordinates into native canvas. |
| After native pre-reset notification | `0x0005B28B` | `83 BE 94 00 00 00 00` | Mid; release compositor resources before `IDirect3DDevice9::Reset`. |
| After successful reset/native post notification | `0x0005B474` | `83 C4 04 B8 01 00 00 00` | Mid; recreate resources only on successful reset. |
| Gameplay enter native | `0x00262FA0` | `E8 4B 1A FE FF 8B 4D C4` | Mid before call to `0x006449F0`. |
| Gameplay enter physical | `0x00262FA8` | `E8 D3 56 FE FF 8B 4D C4` | Mid before call to `0x00648680`; later physical calls stay in this space. |
| Gameplay return native | `0x00263041` | `E8 FA 5C FE FF E8 D5 00 DF FF` | Mid before call to `0x00648D40`. |
| Clip default flag | `0x002441C6` | `C6 45 DF 00` | Read-only guard proving `var_21 = 0` before the policy gate. |
| Clip policy gate | `0x002441CA` | `8B 95 80 FE FF FF 8B 82 4C 02 00 00 0F B6 88 5C 01 00 00` | Mid; authored continues, live-frustum jumps to the guarded continuation. |
| Clip live continuation | `0x0024422F` | `8B 4D D8 E8 C9 18 DC FF 0F B6` | Read-only continuation target. |
| Native batch flush | `0x001C9B10` | `55 8B EC 83 EC 08 C7 45 FC 00 00 00` | Read-only callable barrier before every actual target switch. |
| Clip renderer owner | `0x00244000` | `55 8B EC 81 EC A0 01 00 00 56 57 89` | Read-only owner contract for the gate. |
| Live-frustum helper | `0x00243BE0` | `55 8B EC 81 EC C0 00 00 00 89 8D 58` | Read-only proof that policy retains geometric culling. |

### Pointer, object, and ordering contracts

- Main config vtable RVA is `0x002AE62C`. Compare relocated pointer values semantically, not as raw little-endian bytes:

  | Slot | Offset | Expected target RVA | Call |
  | ---: | ---: | ---: | --- |
  | 6 | `+0x18` | `0x00059CC0` | width `(this, width, 0)` |
  | 7 | `+0x1C` | `0x00059CE0` | height `(this, height, 0)` |
  | 10 | `+0x28` | `0x00059D20` | resize `(this, false)` |
  | 11 | `+0x2C` | `0x00059D40` | minimize/maximize `(this, true, false)` |
  | 12 | `+0x30` | `0x00059D70` | window mode `(this, 1, 1, 1, 1)` |

- `0x0045B8A0` is `int __thiscall(renderer)`. On success, the Direct3D device is at renderer `+0x08`, HWND at `+0x8C`, and stored style at `+0x98`.
- The fixed decorated style is native base `0x00C80000` plus `WS_MINIMIZEBOX`; clearing resize omits `WS_THICKFRAME`, and clearing maximize omits `WS_MAXIMIZEBOX`.
- `0x0045AC70` and `0x0045ACE0` are `int __thiscall(renderer)` and use the device at `+0x08`.
- The reset function calls native pre-reset notification at RVA `0x0005B283`, then reaches the pre-reset hook, calls `Reset`, and returns early on failure. It calls native post-reset notification at RVA `0x0005B46F` only after success, then reaches the post-reset hook. `ESI` is the renderer owner at both hook sites.
- `CCommon2DTask` vtable RVA is `0x002F9AFC`; render slot 4 targets RVA `0x001F5670`. `CCommon3DTask` vtable RVA is `0x002FB218`; render slot 4 targets RVA `0x001784B0`. Guard each vtable address and relocated slot target.
- The batch helper examines four queues through the pointer at RVA `0x003F24FC`; queue stride is 24 bytes and pending count is at `base + 24 + 24 * index`. Development verification reads those counts only after calling the native helper.
- Mouse output words are `x` at `a2[0]`, `y` at `a2[1]`, and validity at `a2[6]`. Only map when the original marked the sample valid.
- The selected renderer-loss hooks occupy RVAs `0x000E5578` through `0x000E7A84`; no selected widescreen address overlaps that range or a current framerate/absolute-judgement site.

The implementation must regenerate this evidence with the saved audit script before final source verification. If any contract differs, stop at preflight; do not signature-scan for another executable.

---

## Task 1: Add strict, feature-owned configuration and ConfigGUI controls

**Files:**

- Create: `src/Patches/WindowedWidescreen/StageClipPolicy.h`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenSettings.h`
- Modify: `src/Config/ConfigDocument.h`
- Modify: `src/Config/ConfigCompiler.h`
- Modify: `src/Config/ConfigCompiler.cpp`
- Modify: `config.toml`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tests/Config/ConfigContractTests.cpp`
- Modify: `tests/Config/ConfigStartupTests.cpp`

**Interfaces:**

```cpp
namespace gc::windowed_widescreen {

enum class StageClipPolicy : std::uint8_t {
    authored,
    live_frustum,
};

[[nodiscard]] constexpr std::string_view StageClipPolicyName(
    StageClipPolicy policy) noexcept;

class WindowedWidescreenSettings final {
public:
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::uint32_t output_width() const noexcept;
    [[nodiscard]] std::uint32_t output_height() const noexcept;
    [[nodiscard]] StageClipPolicy clip_policy() const noexcept;

private:
    WindowedWidescreenSettings(
        bool enabled,
        std::uint32_t output_width,
        std::uint32_t output_height,
        StageClipPolicy clip_policy) noexcept;
    friend class gc::config::ConfigCompiler;
};

} // namespace gc::windowed_widescreen
```

- [ ] Add RED configuration tests that require all four distributed keys, validate defaults, copy the compiled settings by value, and expect semantic paths `experimental.widescreen_window_width`, `experimental.widescreen_window_height`, and `experimental.widescreen_stage_clip_policy` for invalid values. The initial compile/test must fail because the fields/accessor do not exist.
- [ ] Add these required reflect-cpp fields to `ExperimentalConfig` using 32-bit `unsigned long` raw dimensions: enabled `false`, width `1920`, height `1280`, and policy `live_frustum`.
- [ ] Add the exact same keys to distributed `config.toml`. Do not add a migration or optional-field fallback: runtime configuration is strict and complete.
- [ ] Validate width `>= 720`, height `>= 1280`, both dimensions `<= INT_MAX`, the enum is one of the two supported values, and checked rectangle/area arithmetic fits the compiled model's integer types even when the feature is disabled.
- [ ] Construct and own `WindowedWidescreenSettings` inside `ValidatedConfig`; add only `windowed_widescreen() const noexcept`. Do not expose the mutable document.
- [ ] Extend `ConfigContractTests` with independently meaningful cases: minimum accepted, 1137 x 1280 retained exactly, width/height below minimum, values above signed native range, invalid enum via an explicit out-of-range enum value, and copied settings surviving source-document mutation.
- [ ] Extend `ConfigStartupTests` only to prove a valid game config publishes the compiled settings and the NESYS role still publishes only logging/NESYS settings with no game-side probe, write, or feature initialization.
- [ ] Add a `Windowed widescreen stage` section near the top of `DrawExperimental`: enabled checkbox, U32 width/height inputs, and `Authored`/`Live frustum` combo. Mark `dirty` on every change and rely on the existing global compiler result to gate Save.
- [ ] Add a concise tooltip: fixed-size decorated window, ordinary unrotated desktop, perspective scene uses configured W x H, complete 2D remains centered 720 x 1280, restart required. Do not expose any non-goal field.
- [ ] Run the focused GREEN check:

  ```powershell
  $env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
  cmake --preset msvc32-debug
  cmake --build --preset msvc32-debug --target gc_config_contract_tests gc_config_startup_tests ConfigGUI
  ctest --preset msvc32-debug -R 'ConfigContract|ConfigStartup'
  ```

  Expected: both configuration tests pass and ConfigGUI builds; no runtime patch is linked or reachable yet.

- [ ] Commit only Task 1 files:

  ```powershell
  git add -- config.toml src/Config/ConfigDocument.h src/Config/ConfigCompiler.h src/Config/ConfigCompiler.cpp src/Patches/WindowedWidescreen/StageClipPolicy.h src/Patches/WindowedWidescreen/WindowedWidescreenSettings.h tools/ConfigGUI/Main.cpp tests/Config/ConfigContractTests.cpp tests/Config/ConfigStartupTests.cpp
  git commit -m "Add windowed widescreen configuration"
  ```

---

## Task 2: Implement pure resolution, projection, clip, and monitor-placement policies

**Files:**

- Create: `src/Patches/WindowedWidescreen/ResolutionModel.h`
- Create: `src/Patches/WindowedWidescreen/ResolutionModel.cpp`
- Create: `src/Patches/WindowedWidescreen/ProjectionPolicy.h`
- Create: `src/Patches/WindowedWidescreen/ProjectionPolicy.cpp`
- Create: `src/Patches/WindowedWidescreen/NativeWindowPolicy.h`
- Create: `src/Patches/WindowedWidescreen/NativeWindowPolicy.cpp`
- Create: `src/Patches/WindowedWidescreen/StageClipPolicy.cpp`
- Create: `tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Core value types:**

```cpp
struct OutputSize final {
    std::uint32_t width{};
    std::uint32_t height{};
};

struct NativeRect final {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};

struct NativePoint final {
    std::int32_t x{};
    std::int32_t y{};
};

enum class ResolutionError : std::uint8_t {
    width_below_native,
    height_below_native,
    signed_range,
    arithmetic_overflow,
};

class ResolutionModel final {
public:
    static std::expected<ResolutionModel, ResolutionError> Create(
        std::uint32_t width,
        std::uint32_t height) noexcept;
    [[nodiscard]] OutputSize output_size() const noexcept;
    [[nodiscard]] NativeRect native_rect() const noexcept;
    [[nodiscard]] std::optional<NativePoint> ClientToNative(
        std::int32_t x,
        std::int32_t y) const noexcept;
};
```

- [ ] Write RED model tests for 720 x 1280, 1137 x 1280 with 208 left/209 right, 1920 x 1280 with 600-pixel sides, and a larger height with the odd remainder on the right/bottom. Include all four native-rectangle corners, one-past-edge rejection, negative client coordinates, minimum failures, signed-range failures, and checked area overflow.
- [ ] Implement `ResolutionModel::Create` with `NativeCanvasSize = {720, 1280}` and floor-centering. Keep dimensions integral; never infer them from the HWND after startup.
- [ ] Implement client mapping as half-open rectangle membership. Subtract `left/top` only for points inside `[left,right) x [top,bottom)`; return `nullopt` outside rather than clamping.
- [ ] Implement `ProjectionPolicy` as a pure transform of the native CTune scale:

  ```text
  native_fov_rad = radians(75 * native_scale)
  expanded_fov_rad = 2 * atan(tan(native_fov_rad / 2) * output_height / 1280)
  transformed_scale = degrees(expanded_fov_rad) / 75
  ```

  Width does not change the scale. At height 1280 return the native scale. Reject non-finite/non-positive input and any native or expanded vertical FOV `>= 170` degrees.

- [ ] Derive projection expectations in tests with the trigonometric formula, not implementation constants copied from the result. Cover native, 1137 x 1280, 1920 x 1280, and at least one height-expanded output.
- [ ] Define Win32-independent `MonitorWorkArea`, `WindowOuterSize`, and `WindowPlacement` values. `SelectWindowPlacement` chooses primary when it fits, otherwise the first enumerated fitting work area, centers the adjusted outer rectangle, and returns `no_fitting_work_area` if none fits.
- [ ] Test primary fit, primary miss/secondary fit, deterministic first-fit, exact-edge fit, odd centering, negative monitor origins, and no fit. Do not call Win32 in these tests.
- [ ] Implement clip policy as a pure decision: authored continues at the gate; live-frustum selects the guarded continuation. It never forces visibility and never deletes data.
- [ ] Add `gc_windowed_widescreen_model_tests` to CMake and link it to the owning feature/runtime library without duplicating production constants in the test.
- [ ] Run RED before implementation and GREEN after:

  ```powershell
  cmake --build --preset msvc32-debug --target gc_windowed_widescreen_model_tests
  ctest --preset msvc32-debug -R WindowedWidescreenModel
  ```

  Expected GREEN output: one focused model test executable passes without a game binary or Direct3D device.

- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/ResolutionModel.h src/Patches/WindowedWidescreen/ResolutionModel.cpp src/Patches/WindowedWidescreen/ProjectionPolicy.h src/Patches/WindowedWidescreen/ProjectionPolicy.cpp src/Patches/WindowedWidescreen/NativeWindowPolicy.h src/Patches/WindowedWidescreen/NativeWindowPolicy.cpp src/Patches/WindowedWidescreen/StageClipPolicy.cpp src/Patches/CMakeLists.txt tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp tests/CMakeLists.txt
  git commit -m "Add widescreen geometry policies"
  ```

---

## Task 3: Model render-space ownership and pass classification

**Files:**

- Create: `src/Patches/WindowedWidescreen/RenderSpacePolicy.h`
- Create: `src/Patches/WindowedWidescreen/RenderSpacePolicy.cpp`
- Create: `src/Patches/WindowedWidescreen/PassClassifier.h`
- Create: `src/Patches/WindowedWidescreen/PassClassifier.cpp`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
enum class RenderSpace : std::uint8_t {
    physical_3d,
    native_2d,
    compositor,
};

enum class GameplayPass : std::uint8_t {
    orthographic_background,
    perspective_track,
    orthographic_effects,
};

class PassClassifier final {
public:
    PassClassifier(std::uintptr_t image_base) noexcept;
    [[nodiscard]] RenderSpace ClassifyTask(
        std::uintptr_t task_vtable) noexcept;
    [[nodiscard]] static RenderSpace ClassifyGameplay(
        GameplayPass pass) noexcept;
};
```

- [ ] Add RED tests proving the relocated `CCommon2DTask` vtable selects native, `CCommon3DTask` selects physical, each mixed gameplay boundary selects the approved space, and unknown identities select native.
- [ ] Keep unknown-identity deduplication bounded and allocation-free: a fixed-capacity render-thread table stores only stable vtable identities. Emit at most one development diagnostic per stored identity and one aggregate overflow diagnostic.
- [ ] Implement a render-thread policy that captures `GetCurrentThreadId()` on the first frame begin, starts each frame in `physical_3d`, rejects nested frames, rejects transitions outside a frame, and rejects a different thread. Inject the thread-ID provider in tests.
- [ ] Make integer and float dimension selection a pure policy: physical reports output W/H; native reports 720/1280; compositor is an invariant error and must never call game rendering/getters.
- [ ] Do not perform copies inside `PassClassifier` or `RenderSpacePolicy`. They decide ownership; `NativeCanvasCompositor` performs transitions in Task 4.
- [ ] Run:

  ```powershell
  cmake --build --preset msvc32-debug --target gc_windowed_widescreen_model_tests
  ctest --preset msvc32-debug -R WindowedWidescreenModel
  ```

  Expected: classification, dimensions, and thread/frame invariants pass with no Direct3D dependency.

- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/RenderSpacePolicy.h src/Patches/WindowedWidescreen/RenderSpacePolicy.cpp src/Patches/WindowedWidescreen/PassClassifier.h src/Patches/WindowedWidescreen/PassClassifier.cpp src/Patches/CMakeLists.txt tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp
  git commit -m "Model widescreen render spaces"
  ```

---

## Task 4: Build the compositor transition state machine against injected device actions

**Files:**

- Create: `src/Patches/WindowedWidescreen/NativeCanvasCompositor.h`
- Create: `src/Patches/WindowedWidescreen/NativeCanvasCompositor.cpp`
- Create: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Action seam:**

```cpp
struct CompositorDeviceActions final {
    void* context{};
    bool (*bind_wide_scene)(void*) noexcept{};
    bool (*bind_native_canvas)(void*) noexcept{};
    bool (*bind_real_backbuffer)(void*) noexcept{};
    bool (*capture_game_state)(void*) noexcept{};
    bool (*restore_game_state)(void*) noexcept{};
    bool (*draw_scene_center_to_native)(void*) noexcept{};
    bool (*draw_native_to_scene_center)(void*) noexcept{};
    bool (*draw_scene_to_backbuffer)(void*) noexcept{};
    bool (*set_full_viewport_and_scissor)(void*, RenderSpace) noexcept{};
    bool (*native_depth_state_is_disabled)(void*) noexcept{};
    bool (*flush_native_batches)(void*) noexcept{};
    bool (*native_batches_are_empty)(void*) noexcept{};
    bool (*attempt_restore_after_failure)(void*, RenderSpace) noexcept{};
};
```

- [ ] Write RED fake-action tests that log exact operation order and can fail each action independently.
- [ ] Implement frame begin: validate thread/frame state, bind wide scene and compatible depth, establish `physical_3d`, then allow the hook to call native clear/`BeginScene`.
- [ ] Implement every actual space switch as: flush while the old target remains bound; in development verify all four queue counts are zero; capture state; enter loader-owned `compositor`; bind destination; draw the required one-to-one copy; restore captured game state; explicitly reapply destination viewport/scissor; validate native depth state when entering native; publish the new logical space only after all operations succeed.
- [ ] Implement `physical_3d -> native_2d` as scene `native_rect` to full native canvas. Implement `native_2d -> physical_3d` as full native canvas to scene `native_rect`.
- [ ] Make a same-space request a no-op: it must not flush, capture, or copy. This preserves contiguous task segments.
- [ ] Implement frame end: if native, perform one native-to-scene transition; capture/copy full scene one-to-one into the real backbuffer; leave the backbuffer bound with output viewport/scissor; then allow the hook to call native `EndScene`/system presentation once.
- [ ] On any failure, do not advance logical render space. Attempt restoration to the last published target/state, return a structured operation error, and let the production hook select the one-shot fatal boundary. Never continue with an uncertain target.
- [ ] Test: begin ordering, equal-space no-op, both transition directions, frame-end closure, viewport-after-restore, every injected failure, pending-batch rejection, nested/out-of-frame calls, and wrong-thread calls.
- [ ] Build and run:

  ```powershell
  cmake --build --preset msvc32-debug --target gc_windowed_widescreen_runtime_tests
  ctest --preset msvc32-debug -R WindowedWidescreenRuntime
  ```

  Expected: fake actions prove state publication and rollback without a live device.

- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/NativeCanvasCompositor.h src/Patches/WindowedWidescreen/NativeCanvasCompositor.cpp src/Patches/CMakeLists.txt tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp tests/CMakeLists.txt
  git commit -m "Add widescreen compositor state machine"
  ```

---

## Task 5: Implement Direct3D 9 resources, copy-state isolation, and renderer lifecycle

**Files:**

- Create: `src/Patches/WindowedWidescreen/D3D9CompositorDevice.h`
- Create: `src/Patches/WindowedWidescreen/D3D9CompositorDevice.cpp`
- Create: `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.h`
- Create: `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`

**Lifecycle seam:**

```cpp
namespace gc::renderer_device_loss {

enum class RendererResourceState : std::uint8_t {
    disabled,
    awaiting_device,
    active,
    releasing_for_reset,
    awaiting_reset,
    recreating,
};

struct RendererResourceParticipant final {
    void* context{};
    bool (*create)(void*, std::uintptr_t renderer_owner) noexcept{};
    void (*release)(void*) noexcept{};
};

class RendererResourceLifecycle final {
public:
    std::expected<void, RendererResourceError> Attach(
        RendererResourceParticipant participant) noexcept;
    std::expected<void, RendererResourceError> OnDeviceCreated(
        std::uintptr_t renderer_owner) noexcept;
    std::expected<void, RendererResourceError> BeforeReset() noexcept;
    std::expected<void, RendererResourceError> AfterReset(
        std::uintptr_t renderer_owner) noexcept;
    void Detach() noexcept;
};

} // namespace gc::renderer_device_loss
```

- [ ] Add RED lifecycle tests for disabled/attach/device-create/active, active/release/awaiting-reset, successful recreate, repeated pre-reset after a failed native Reset, invalid ordering, create failure, and exact once-only release/recreate counts.
- [ ] Make release infallible and idempotent. If native Reset fails, the missing post hook leaves state `awaiting_reset`; the next pre hook performs no second release, and the eventual post hook recreates once.
- [ ] Keep one optional participant inside the existing `RendererDeviceLossRuntime`. Add narrow attach/detach/device-created entry points for the widescreen transaction; do not expose a vector or general callback registry.
- [ ] Implement `D3D9CompositorDevice` with WRL `ComPtr` ownership for: real backbuffer surface, output-sized scene render-target texture/surface, 720 x 1280 native render-target texture/surface, compatible output-sized depth surface, and reusable `D3DSBT_ALL` state block.
- [ ] At device-created time, validate exact HWND client W/H, backbuffer W/H/format, `D3DMULTISAMPLE_NONE`, caps `MaxTextureWidth/Height`, render-target texture support for the active backbuffer format, and a compatible depth format/surface. Return a named capability/resource stage on failure.
- [ ] Use the device already owned at renderer `+0x08`; never call `Direct3DCreate9` or create a second device.
- [ ] Implement copy quads with `D3DFVF_XYZRHW | D3DFVF_TEX1`, `DrawPrimitiveUP`, point sampling, clamped addressing, half-pixel-correct coordinates, and source UVs derived from texture dimensions. Disable blend, depth, stencil, fog, scissor, sRGB sample/write, and enable all color channels for the copy.
- [ ] Explicitly save/restore render target and depth surfaces around the state block. Unbind a texture from render-target use before sampling it. After applying the state block, always reapply the intended full viewport/scissor because target changes and state restore can overwrite them.
- [ ] Keep the scene/depth bound during physical rendering and native/no-depth bound during native rendering. Validate that the game's restored native segment has Z enable and Z write disabled before handing control back.
- [ ] Release every default-pool reference, including acquired surfaces and state blocks, before Reset. Recreate them only after successful Reset/native post-notification.
- [ ] Add fake owned-reference tests proving complete release before reset, no double release after failed Reset, recreate once, and no logical `active` publication when any allocation/capability action fails. Do not add a hardware-dependent Direct3D unit test.
- [ ] Build and run:

  ```powershell
  cmake --build --preset msvc32-debug --target gc_windowed_widescreen_runtime_tests
  ctest --preset msvc32-debug -R WindowedWidescreenRuntime
  ```

  Expected: lifecycle and copy-action contracts pass; production D3D code compiles but is not claimed runtime-tested.

- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/D3D9CompositorDevice.h src/Patches/WindowedWidescreen/D3D9CompositorDevice.cpp src/Patches/RendererDeviceLoss/RendererResourceLifecycle.h src/Patches/RendererDeviceLoss/RendererResourceLifecycle.cpp src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp src/Patches/CMakeLists.txt tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp
  git commit -m "Add widescreen renderer resources"
  ```

---

## Task 6: Freeze the supported executable ABI and implement a disabled-first hook transaction

**Files:**

- Create: `tools/analysis/windowed_widescreen_contract_audit.py`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.h`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`

- [ ] Write the audit as a normal Python file. It connects to `H:\gc\game471.exe.i64` with `AgentSession`, requires an IDA backend, emits the function/mid/pointer/calling-convention records in the Frozen Native ABI section, and optionally inspects a supplied built DLL's PE machine and configuration strings. It must not mutate the IDB or manage daemon/process lifetime.
- [ ] Run it only by filename:

  ```powershell
  python tools\analysis\windowed_widescreen_contract_audit.py
  ```

  Expected: backend reports `ida_available=true`, database path is exact, and every frozen contract matches.

- [ ] Define `WidescreenContractSite`, bounded `BytePattern`, byte contracts, relocated pointer contracts, calling-convention/object offsets, and hook kind in `WindowedWidescreenAbi`. Use the exact table above. Store pointer targets as RVAs and compare `actual == image_base + target_rva`.
- [ ] Include read-only contracts for clip default/continuation/owner/live-frustum, batch flush, config setter vtable targets, and task render vtable targets. Preflight them even though they are not hook addresses.
- [ ] Reject any loaded base other than `0x00400000`, address addition overflow, unreadable memory, byte mismatch, relocated-pointer mismatch, null actions, or capacity overflow before creating the first hook.
- [ ] Model hook operations with `create_disabled`, `enable`, and `reset` actions. Production uses `SafetyHook::InlineHook::create` or `MidHook::create` with `StartDisabled`; no hook is enabled until every candidate exists.
- [ ] On create or enable failure, disable/reset all prior candidates in reverse order, detach the renderer-resource participant if attached, clear candidate callback context, and verify original site bytes remain. Record whether rollback was attempted and complete.
- [ ] Publish `g_runtime_owner` only after all hooks enable successfully. A temporary callback context may exist only inside the synchronous enable phase; callbacks seeing installation state must fail closed.
- [ ] Add RED/GREEN synthetic transaction tests. Use invented test bytes and addresses, not production patterns. Prove all reads happen before first create, pointer contracts are preflighted, base/address errors reject early, creation and enabling fail at every index, rollback order is reverse, and no owner is published on failure.
- [ ] Repeat the current source overlap audit and record no collision with renderer, framerate, absolute-judgement, audio, or Switch hooks. Do not edit those features to make the audit pass.
- [ ] Run:

  ```powershell
  cmake --build --preset msvc32-debug --target gc_windowed_widescreen_runtime_tests
  ctest --preset msvc32-debug -R WindowedWidescreenRuntime
  ```

- [ ] Commit:

  ```powershell
  git add -- tools/analysis/windowed_widescreen_contract_audit.py src/Patches/WindowedWidescreen/WindowedWidescreenAbi.h src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.cpp src/Patches/CMakeLists.txt tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp
  git commit -m "Guard widescreen native contracts"
  ```

---

## Task 7: Install fixed-window, device-created, and frame-boundary hooks

**Files:**

- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.h`
- Create: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/NativeWindowPolicy.h`
- Modify: `src/Patches/WindowedWidescreen/NativeWindowPolicy.cpp`
- Modify: `src/Patches/WindowedWidescreen/D3D9CompositorDevice.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`

**Initializer:**

```cpp
[[nodiscard]] std::expected<void, WindowedWidescreenError>
WindowedWidescreenPatchInit(
    WindowedWidescreenSettings settings) noexcept;
```

- [ ] Add a one-attempt/stored-result initializer matching current patch conventions. Disabled settings return success before module/base lookup, monitor enumeration, resource creation, lifecycle attachment, or hook preflight.
- [ ] Store settings/model/compositor/hook candidates in one stable `unique_ptr<WindowedWidescreenRuntime>`. No hook callback may retain a stack address or mutable config reference.
- [ ] Implement the config-apply inline detour with exact `int __cdecl(int main_config_ptr)` ABI. Call original first. When it succeeds, guard the object's vtable and invoke width `(W,0)`, height `(H,0)`, resize `false`, minimize/maximize `(true,false)`, and mode `(1,1,1,1)`. Do not alter native device-loss or multisample config fields.
- [ ] Implement production monitor enumeration/outer-frame adjustment. Use the intended fixed decorated style, choose through the pure policy, and retain the chosen placement in runtime state. Failure to fit is a named startup capability error.
- [ ] Implement the window/device inline detour with exact `int __thiscall(renderer)` ABI. Call original. On native failure return its result unchanged. On success, validate renderer `+0x08`, HWND `+0x8C`, actual style, exact client rect, backbuffer, caps/formats/MSAA, and move the already-created outer window to the selected work area without changing client size.
- [ ] Call the renderer-resource lifecycle's device-created entry once so it creates all compositor resources before the first render. A capability/resource error selects the existing one-shot log/modal/fail-fast boundary; do not return to stretched native presentation.
- [ ] Implement frame-begin detour: require active resources, begin compositor frame/bind scene+depth, then call original native clear/`BeginScene`. Preserve native return value.
- [ ] Implement frame-end detour: finish compositor/copy to real backbuffer, then call original native `EndScene` and system presentation once. Preserve native return value.
- [ ] Add fake wrapper tests proving original-call order and return propagation: config original before setters; window original before capability/resource activation; frame scene binding before original begin; final copy before original end; native original failure does not create resources.
- [ ] Do not wire `DllMain` yet. At this commit the default-disabled production path remains unreachable, preventing an incomplete feature from launching.
- [ ] Build the DLL and focused tests:

  ```powershell
  cmake --build --preset msvc32-debug --target iDmacDrv32 gc_windowed_widescreen_runtime_tests
  ctest --preset msvc32-debug -R WindowedWidescreenRuntime
  ```

- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/WindowedWidescreenPatch.h src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp src/Patches/WindowedWidescreen/NativeWindowPolicy.h src/Patches/WindowedWidescreen/NativeWindowPolicy.cpp src/Patches/WindowedWidescreen/D3D9CompositorDevice.cpp src/Patches/CMakeLists.txt tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp
  git commit -m "Add widescreen window and frame hooks"
  ```

---

## Task 8: Install render-space-aware task, gameplay, dimension, and viewport hooks

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/PassClassifier.cpp`
- Modify: `src/Patches/WindowedWidescreen/RenderSpacePolicy.cpp`
- Modify: `src/Patches/WindowedWidescreen/NativeCanvasCompositor.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`

- [ ] Implement the task-dispatch inline detour as `int __thiscall(task_node)`: read `task = *task_node`, guard its vtable read, request the classified space, then call the original vtable dispatch. Leave the selected space active for the next task so equal-policy tasks remain one segment.
- [ ] Unknown or unreadable task identities default to native. Emit only the bounded development diagnostic; do not classify by guessed class layouts or filename strings.
- [ ] Implement midhooks at gameplay RVAs `0x00262FA0`, `0x00262FA8`, and `0x00263041`. The first requests native, the second physical, and the third native. Calls at `0x0064DA90` and optional `0x00645120` stay physical without redundant hooks.
- [ ] Before every actual target change, call native batch flush RVA `0x001C9B10` while the old target is bound. In development, read the four proven pending counts and reject/log once if any remains nonzero. Do not assume the caller already flushed.
- [ ] Implement all eight getter inline hooks. Physical returns output W/H, native returns 720/1280, and float/int variants agree exactly. Disabled never installs them; compositor-space entry is fatal because loader drawing must not call native getters.
- [ ] Implement viewport-reset inline hook with exact `int __cdecl(int* viewport)` ABI. Non-null passes through unchanged. Null constructs `{0.0F, 0.0F, width, height}` for current space and calls the original with that explicit rectangle, avoiding its direct global-dimension path.
- [ ] After every compositor state restore, explicitly apply matching viewport/scissor independently of the viewport hook.
- [ ] Extend fake integration tests with a sequence containing adjacent native tasks, native -> physical -> physical -> native gameplay subpasses, explicit and null viewport calls, all getter variants, and a pending-batch failure. Assert exact copy counts.
- [ ] Run:

  ```powershell
  cmake --build --preset msvc32-debug --target iDmacDrv32 gc_windowed_widescreen_model_tests gc_windowed_widescreen_runtime_tests
  ctest --preset msvc32-debug -R 'WindowedWidescreenModel|WindowedWidescreenRuntime'
  ```

- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp src/Patches/WindowedWidescreen/PassClassifier.cpp src/Patches/WindowedWidescreen/RenderSpacePolicy.cpp src/Patches/WindowedWidescreen/NativeCanvasCompositor.cpp src/Patches/WindowedWidescreen/WindowedWidescreenAbi.cpp tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp
  git commit -m "Route widescreen render spaces"
  ```

---

## Task 9: Install projection, `_clip.dat` policy, and client-coordinate hooks

**Files:**

- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/ProjectionPolicy.cpp`
- Modify: `src/Patches/WindowedWidescreen/StageClipPolicy.cpp`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`

- [ ] Implement both projection inline detours with their exact cdecl signatures. In physical space, transform only the CTune scale for output height above 1280 and let the hooked target getters supply output aspect. At height 1280 pass the native scale bit-for-bit. In native space call original unchanged.
- [ ] Preserve destination pointer, camera/orientation pointer, unused primary argument, near plane 1.0, far plane 1000.0, and the original projection builders. Do not replace matrices or apply a transposed-axis formula.
- [ ] Treat non-finite/invalid scale or FOV `>= 170` as a fatal rendering invariant; do not clamp or silently fall back mid-frame.
- [ ] Implement the clip midhook. `authored` changes nothing. `live_frustum` sets EIP to `image_base + 0x0024422F` only after preflight proved `var_21 = 0` at `0x002441C6`, the gate bytes, the continuation, owner, and live helper. This selects the game's existing fallback frustum; it does not force all parts visible.
- [ ] Implement mouse/debug inline detour with exact `POINT* __thiscall(owner, DWORD* output)` ABI. Call original, map `output[0:2]` only when `output[6] == 1`, subtract native rect origin inside the canvas, and set validity to zero outside. Preserve every other word and return value.
- [ ] Keep booster/button input unchanged; do not touch polling, FastIO, Switch input, or input timestamps.
- [ ] Add wrapper tests for projection argument forwarding, 1280-height identity, height expansion, authored no-op, live EIP redirect, mouse inside/boundary/outside behavior, invalid-original sample preservation, and unchanged non-coordinate output words.
- [ ] Run both focused suites and build the DLL as in Task 8.
- [ ] Commit:

  ```powershell
  git add -- src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp src/Patches/WindowedWidescreen/ProjectionPolicy.cpp src/Patches/WindowedWidescreen/StageClipPolicy.cpp tests/Patches/WindowedWidescreen/WindowedWidescreenModelTests.cpp tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp
  git commit -m "Add widescreen projection clip and input policies"
  ```

---

## Task 10: Complete reset-hook ownership, guarded installation, and game-only startup wiring

**Files:**

- Modify: `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererResourceLifecycle.cpp`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h`
- Modify: `src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp`
- Modify: `src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.cpp`
- Modify: `src/Loader/DllMain.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp`

- [ ] Give `RendererDeviceLossRuntime` ownership of the pre/post reset `MidHook` objects and one `RendererResourceLifecycle`. Expose one compound, idempotently resettable install operation to the widescreen transaction; no other module owns hooks at those addresses.
- [ ] The pre-reset callback runs after native notification at RVA `0x0005B283`, calls lifecycle `BeforeReset`, and leaves resources released before native `Reset`. The post callback runs only after successful Reset and native post notification at RVA `0x0005B46F`, then calls `AfterReset(ESI)`.
- [ ] Include both reset byte contracts in the widescreen all-sites preflight before the compound operation creates either hook. If the second reset hook or any later widescreen hook fails, reset both and detach the participant during reverse rollback.
- [ ] Complete the hook plan with every contract in the frozen table. Create all candidates disabled, attach candidate context, enable them, and publish the runtime owner only on total success. Emit one structured success record with output size, native rect, clip policy, and hook count.
- [ ] Add `PublishWindowedWidescreenInitializationFatal` in `Loader/DllMain.cpp` using the existing `gc::system_path::PublishStartupFatal` actions and a one-shot atomic. Format stage/site/rollback/capability details; use a feature-specific title and exit code.
- [ ] Immediately after successful `RendererDeviceLossPatchInit`, copy `settings.windowed_widescreen()` into `WindowedWidescreenPatchInit`. On error publish fatal and return `FALSE`. This code remains inside the existing game-only role branch, before audio. NESYS must never invoke it.
- [ ] For device-created and mid-session compositor errors occurring after `DllMain`, use the same existing startup-fatal/log/modal/fail-fast boundary from the feature runtime. Rendering must not continue on the native backbuffer after enabled setup failed.
- [ ] Verify disabled initialization returns before `GetModuleHandleW`, lifecycle attach, or any action. Add a fake-action test that observes zero calls and native behavior preservation.
- [ ] Verify full enabled transaction failure at every hook index, including the compound reset pair, leaves the already-installed baseline `RendererDeviceLossPatch` intact but removes all widescreen hooks/participant/context.
- [ ] Run focused Debug tests plus a complete Debug build/test:

  ```powershell
  cmake --build --preset msvc32-debug
  ctest --preset msvc32-debug -R 'ConfigContract|ConfigStartup|WindowedWidescreenModel|WindowedWidescreenRuntime'
  ctest --preset msvc32-debug -j 4
  ```

  Expected: all tests pass; this is static startup/transaction evidence only.

- [ ] Commit:

  ```powershell
  git add -- src/Patches/RendererDeviceLoss/RendererResourceLifecycle.h src/Patches/RendererDeviceLoss/RendererResourceLifecycle.cpp src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.h src/Patches/RendererDeviceLoss/RendererDeviceLossPatch.cpp src/Patches/WindowedWidescreen/WindowedWidescreenPatch.cpp src/Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.cpp src/Patches/CMakeLists.txt src/Loader/DllMain.cpp tests/Patches/WindowedWidescreen/WindowedWidescreenRuntimeTests.cpp
  git commit -m "Integrate windowed widescreen stage"
  ```

---

## Task 11: Run static verification and hand off the operator acceptance matrix

**Files:**

- Inspect all changed files.
- Inspect unchanged ownership boundaries listed under Final File Map.
- Do not deploy or edit `H:\gc`.

- [ ] Re-run the saved IDA audit by filename. Compare every emitted RVA, expected byte sequence, continuation, pointer target, calling convention, task identity, object offset, and batch-queue contract with `WindowedWidescreenAbi.cpp`. A mismatch is a stop condition, not a reason to scan.
- [ ] Run a source coexistence audit for every widescreen RVA against current renderer, framerate, absolute-judgement, audio, and Switch hook plans. Inspect any nearby hit semantically; do not rely only on address-range comparison.
- [ ] Run focused Debug and Release builds/tests:

  ```powershell
  $env:GC_ASIO_SDK_DIR = 'H:\gc\artifacts\ASIOSDK'
  cmake --preset msvc32-debug
  cmake --build --preset msvc32-debug --target gc_config_contract_tests gc_config_startup_tests gc_windowed_widescreen_model_tests gc_windowed_widescreen_runtime_tests iDmacDrv32 ConfigGUI
  ctest --preset msvc32-debug -R 'ConfigContract|ConfigStartup|WindowedWidescreenModel|WindowedWidescreenRuntime'

  cmake --preset msvc32-release
  cmake --build --preset msvc32-release --target gc_config_contract_tests gc_config_startup_tests gc_windowed_widescreen_model_tests gc_windowed_widescreen_runtime_tests iDmacDrv32 ConfigGUI
  ctest --preset msvc32-release -R 'ConfigContract|ConfigStartup|WindowedWidescreenModel|WindowedWidescreenRuntime'
  ```

- [ ] Run both complete preset graphs:

  ```powershell
  cmake --build --preset msvc32-debug
  ctest --preset msvc32-debug -j 4
  cmake --build --preset msvc32-release
  ctest --preset msvc32-release -j 4
  ```

- [ ] Inspect `build-msvc32-release\dist\iDmacDrv32.dll` with the saved audit script's `--artifact` option. Require PE machine `0x014C`, the four config key strings, and an exact SHA-256 identity. Inspect exports to ensure the existing iDmac ABI is unchanged.
- [ ] Confirm the disabled path has no hook/capability/monitor/resource action in focused tests, the game-only DllMain branch is the sole caller, and NESYS startup tests still show no game-side operations.
- [ ] Confirm `config.toml` and ConfigGUI expose exactly the approved four fields and no rotation/fullscreen/borderless/monitor/resize/UI-scale field.
- [ ] Confirm no production path uses `StretchRect`, a second scene pair, CPU readback, client/backbuffer mismatch, `_clip.dat` deletion, or `system.cfg` mutation.
- [ ] Run repository checks:

  ```powershell
  git diff --check
  git status --short
  git log --oneline -12
  ```

  Expected: no whitespace errors; every changed file is accounted for; no build output, temporary probe, runtime asset, or unrelated ASIO file is staged.

- [ ] Record static results without claiming gameplay acceptance: commit hashes, Debug/Release focused and full CTest totals, IDA audit result, Release DLL SHA-256, machine/exports, and exact remaining operator checks.
- [ ] Hand off this operator-run matrix; do not execute or mark it accepted without the user's deliberate deployment and observations:

  - disabled native 720 x 1280 baseline;
  - enabled 720 x 1280 with pixel-identical 2D placement;
  - enabled 1137 x 1280 and 1920 x 1280 on an unrotated desktop;
  - centered 720 x 1280 UI measurements at every output;
  - actual perspective geometry, not stretched pixels, in added horizontal regions;
  - authored versus live-frustum visibility difference with `_clip.dat` still present;
  - one-player and two-player menu, song-select, gameplay, result, and attract layouts;
  - at least three consecutive tunes over several stage themes without stale/one-frame mistargeting;
  - minimize/restore and repeated device-loss recovery without leaks, blank frames, or flashes;
  - unavailable edge resize and maximize;
  - 120/240-FPS GPU timing, copy count, and video-memory comparison; and
  - one startup/lifecycle summary with no render-loop log spam.

## Requirement Coverage

| Approved requirement | Implemented by | Proven by |
| --- | --- | --- |
| Fixed ordinary window, exact client/backbuffer, no rotation | Tasks 1, 2, 7, 10 | Config/model tests, startup/capability checks, operator matrix |
| Centered unscaled 720 x 1280 2D | Tasks 2, 4, 5, 8 | Geometry/action tests, operator pixel measurements |
| Full configured perspective scene | Tasks 4, 5, 8, 9 | Transition/projection tests, operator geometry observation |
| Hor+ at height 1280; focal-length preservation above | Tasks 2 and 9 | Independently derived trigonometric tests |
| `_clip.dat` retained; authored/live-frustum policy | Tasks 1, 2, 6, 9 | Config/policy/contract tests and operator comparison |
| Native order/blending across mixed gameplay | Tasks 3, 4, 8 | Fake action/copy-count tests and multi-screen runtime matrix |
| Device loss/reset integration | Tasks 5 and 10 | Lifecycle tests and operator repeated reset |
| Guarded all-or-nothing installation | Tasks 6 and 10 | Synthetic preflight/rollback tests and fresh IDA audit |
| Disabled native behavior | Tasks 1, 6, 10 | Zero-action disabled test and native baseline |
| Static proof separated from acceptance | Task 11 | Static report plus explicitly pending operator matrix |

The implementation is complete only when Tasks 1-10 and all static checks in Task 11 pass. The feature is accepted only after the separate operator matrix is observed in the real game.
