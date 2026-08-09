# Clean ASIO Distribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore `dist` to its runnable four-file contract while preserving GPL source packaging and isolated ASIO Save validation without a separate helper executable.

**Architecture:** Corresponding source remains a clean-revision artifact under `source-package`; CMake no longer treats deployable staging as release packaging. ConfigGUI self-spawns with an exact internal `--asio-probe` mode before GUI initialization, retaining the bounded pipe and Job Object boundary. The optional logo path is removed and the real ImGui host disables INI persistence.

**Tech Stack:** CMake 3.31, C++23, Win32 process/COM APIs, Dear ImGui, D3D11, CTest, PowerShell 5.1, MSVC x86.

## Global Constraints

- Work only in `H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend`; do not deploy to or mutate runtime `H:\gc`.
- Build with `GC_ASIO_SDK_DIR=H:\gc\artifacts\ASIOSDK` in the Visual Studio x86 environment.
- Keep `dist` runtime-only: `ConfigGUI.exe`, `config.toml`, `card.txt`, and `iDmacDrv32.dll` are its project-owned primary artifacts.
- Retain the GPL-3.0-only combined-distribution policy and exact corresponding-source target, but publish its ZIP only under `source-package`.
- Preserve out-of-process vendor-driver isolation, the binary stdin/stdout protocol, the five-second timeout, restricted inherited handles, and kill-on-close Job Object.
- Do not pass driver-controlled text on a command line.
- Runtime ASIO startup, rendering, clocks, fallback, and gameplay behavior are out of scope.
- Execute inline; multi-agent dispatch is disabled for this task.

---

### Task 1: Separate corresponding source from deployable staging

**Files:**
- Modify: `tests/CMake/CorrespondingSourcePackageTests.cmake`
- Modify: `cmake/PackageCorrespondingSource.cmake`
- Modify: `CMakeLists.txt`
- Modify: `SOURCE-OFFER.md`

**Interfaces:**
- Consumes: `GC_PACKAGE_BUILD_DIR`, exact Git archive/input metadata, external SDK path, PowerShell ZIP helper.
- Produces: `${GC_PACKAGE_BUILD_DIR}/source-package/GCLoader-<commit>-corresponding-source.zip`; no `GC_PACKAGE_DIST_DIR` input or `dist` side effect.

- [ ] **Step 1: Write the failing packaging contract**

Remove `GC_PACKAGE_DIST_DIR` from the test invocation and success outputs. Keep the source archive assertion and replace the source-vs-dist hash assertion with:

```cmake
if(EXISTS "${case_build}/dist")
    message(FATAL_ERROR
        "Corresponding-source packaging created deployable dist output")
endif()
```

Keep the portable-entry, Unicode, manifest, drift, corruption, and atomic-publication checks unchanged.

- [ ] **Step 2: Run the focused test and verify RED**

Run from the x86 MSVC environment:

```powershell
$env:GC_ASIO_SDK_DIR='H:\gc\artifacts\ASIOSDK'
ctest --test-dir build-msvc32-debug -R '^CorrespondingSourcePackageTests$' --output-on-failure
```

Expected: FAIL because the current packager requires `GC_PACKAGE_DIST_DIR` or creates a dist copy.

- [ ] **Step 3: Remove the dist publication path**

In `PackageCorrespondingSource.cmake`, remove `GC_PACKAGE_DIST_DIR` validation/normalization, `dist_temporary_zip`, `dist_final_zip`, copy/hash/rename logic, and stale-dist pruning. Preserve atomic rename into `source-package` and stale cleanup within that directory.

In the root target, stop passing `-DGC_PACKAGE_DIST_DIR`. Update `SOURCE-OFFER.md` to describe the source archive as a separate release artifact rather than a file adjacent to deployable binaries.

- [ ] **Step 4: Run focused verification and verify GREEN**

```powershell
ctest --test-dir build-msvc32-debug -R '^CorrespondingSourcePackageTests$' --output-on-failure
```

Expected: PASS with no fixture `dist` directory.

- [ ] **Step 5: Commit**

```powershell
git add -- CMakeLists.txt cmake/PackageCorrespondingSource.cmake SOURCE-OFFER.md tests/CMake/CorrespondingSourcePackageTests.cmake
git commit -m "Separate corresponding source from deployable dist"
```

### Task 2: Preserve probe isolation through ConfigGUI self-hosting

**Files:**
- Create: `tools/ConfigGUI/AsioProbeMode.h`
- Create: `tools/ConfigGUI/AsioProbeMode.cpp`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tools/CMakeLists.txt`
- Delete: `tools/AsioProbe/CMakeLists.txt`
- Delete: `tools/AsioProbe/Main.cpp`
- Modify: `src/Audio/Asio/AsioProbeClient.h`
- Modify: `src/Audio/Asio/AsioProbeClient.cpp`
- Modify: `tests/Audio/AsioProbeClientTests.cpp`
- Modify: `tests/Audio/CMakeLists.txt`
- Modify: `cmake/PackageCorrespondingSource.cmake`
- Modify: `tests/CMake/CorrespondingSourcePackageTests.cmake`

**Interfaces:**
- Consumes: `AsioProbeRequest`, the existing framed stdin/stdout protocol, `ProductionAsioRegistrySource`, and `ProductionAsioDriverFactory`.
- Produces: `inline constexpr std::wstring_view kAsioProbeModeArgument{L"--asio-probe"};` and `int RunAsioProbeMode() noexcept`; `AsioProbeProcessRequest::fixed_argument` carries only that constant.

- [ ] **Step 1: Change the process-contract tests first**

Extend the fake process action to capture `request.fixed_argument`. Change the successful contract to require:

```cpp
observed->executable ==
    std::filesystem::path{L"C:\\Arcade\\ConfigGUI.exe"} &&
observed->fixed_argument == gc::audio::kAsioProbeModeArgument
```

Keep the Unicode driver name assertion on decoded stdin. Change the integration compile definition from `$<TARGET_FILE:AsioProbe>` to `$<TARGET_FILE:ConfigGUI>` so the existing real-process protocol test exercises the self-hosted mode.

- [ ] **Step 2: Build/run the test and verify RED**

```powershell
cmake --build --preset msvc32-debug --target AsioProbeClientTests
ctest --test-dir build-msvc32-debug -R '^AsioProbeClientTests$' --output-on-failure
```

Expected: compile or assertion failure because the request has no fixed argument and the client still resolves sibling `AsioProbe.exe`.

- [ ] **Step 3: Add the fixed self-launch contract**

Add the fixed argument and owned request field:

```cpp
inline constexpr std::wstring_view kAsioProbeModeArgument{L"--asio-probe"};

struct AsioProbeProcessRequest {
    std::filesystem::path executable_path;
    std::wstring fixed_argument;
    // existing stdin, timeout, output and isolation fields
};
```

`AsioProbeClient::Run` uses `CurrentExecutablePath()` directly and sets the fixed argument. `ProductionAsioProbeProcessActions::Run` validates it and constructs only:

```text
"<absolute ConfigGUI path>" --asio-probe
```

No request payload value is appended to the command line.

- [ ] **Step 4: Move the helper entrypoint behind ConfigGUI's early mode**

Move the protocol I/O, COM apartment, hidden window, capability call, response encoding, and exit-code mapping from `tools/AsioProbe/Main.cpp` into `RunAsioProbeMode() noexcept` without changing behavior. At the first line of `main` behavior, before `GuiComApartment`, config loading, or D3D/ImGui initialization, dispatch only:

```cpp
if (argc == 2 && std::string_view{argv[1]} == "--asio-probe") {
    return RunAsioProbeMode();
}
```

Remove the standalone subdirectory/target and the ConfigGUI dependency on it. Link ConfigGUI's new source to the already shared `gc_asio` core.

- [ ] **Step 5: Update offline corresponding-source build metadata**

The offline script builds `iDmacDrv32` and `ConfigGUI`; remove `AsioProbe` from its target list and from the package test's expected fragments.

- [ ] **Step 6: Build/run focused tests and verify GREEN**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ConfigGUI AsioProbeClientTests
ctest --test-dir build-msvc32-debug -R '^(AsioProbeClientTests|AsioProbeProtocolTests|CorrespondingSourcePackageTests)$' --output-on-failure
```

Expected: all pass; the real integration child is `ConfigGUI.exe --asio-probe` and returns a structured result.

- [ ] **Step 7: Commit**

```powershell
git add -- src/Audio/Asio tools/ConfigGUI tools/CMakeLists.txt tools/AsioProbe tests/Audio cmake/PackageCorrespondingSource.cmake tests/CMake/CorrespondingSourcePackageTests.cmake
git commit -m "Self-host isolated ASIO validation"
```

### Task 3: Remove optional branding and persistent GUI/build artifacts

**Files:**
- Create: `tests/Config/ConfigGuiHostTests.cpp`
- Create: `tests/CMake/DistributionArtifactTests.cmake`
- Modify: `tests/Config/CMakeLists.txt`
- Modify: `tests/CMake/CMakeLists.txt`
- Modify: `tests/CMake/AsioSdkConfigureTests.cmake`
- Modify: `CMakeLists.txt`
- Modify: `cmake/AsioSdk.cmake`
- Modify: `tools/ConfigGUI/CMakeLists.txt`
- Modify: `tools/ConfigGUI/Main.cpp`
- Modify: `tools/ConfigGUI/Win32D3D11Host.cpp`
- Delete: `tools/ConfigGUI/AsioLogoTexture.h`
- Delete: `tools/ConfigGUI/AsioLogoTexture.cpp`
- Modify: `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Consumes: existing `Win32D3D11Host::Open/Close`, generated `GC_DIST_DIR`, SDK headers/license/version files.
- Produces: a text-only ASIO settings UI; `ImGui::GetIO().IniFilename == nullptr` while the host is open; no logo requirement or retired generated files in `dist`.

- [ ] **Step 1: Add the real host persistence regression**

Create a small executable using the repository's `Expect` convention. It opens the real `Win32D3D11Host`, asserts the ImGui context exists and:

```cpp
ImGui::GetIO().IniFilename == nullptr
```

then closes the host. Link it to `gc_config_gui_host` and register `ConfigGuiHostTests`.

- [ ] **Step 2: Add SDK and dist-contract regressions**

Change `write_fake_sdk` so its normal valid fixture has no logo directory/file; `cache_wins` must still configure successfully.

Register `DistributionArtifactTests.cmake` with `GC_TEST_DIST_DIR=${GC_DIST_DIR}`. Require the four primary files and reject:

```cmake
AsioProbe.exe
ASIO-compatible-logo-Steinberg-TM-BW.png
imgui.ini
licenses
GCLoader-*-corresponding-source.zip
```

- [ ] **Step 3: Configure/build/run and verify RED**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ConfigGuiHostTests ConfigGUI iDmacDrv32
ctest --test-dir build-msvc32-debug -R '^(ConfigGuiHostTests|AsioSdkConfigureTests|DistributionArtifactTests)$' --output-on-failure
```

Expected: host persistence is non-null, SDK fixture is rejected for the missing logo, and dist contains retired files.

- [ ] **Step 4: Implement minimal cleanup**

Immediately after `ImGui::CreateContext()` set:

```cpp
ImGuiIO& io = ImGui::GetIO();
io.IniFilename = nullptr;
```

Remove `AsioLogoTexture`, all logo drawing/loading/error text, its CMake source/definition, `GC_ASIO_COMPATIBLE_LOGO`, and the logo from the required SDK input list.

Remove unconditional license copying from root configuration. During configuration remove only known retired generated paths and matching corresponding-source ZIPs from `GC_DIST_DIR`; do not recurse over or delete unrelated files.

Update the third-party notice so it no longer claims the SDK license is copied into every deployable distribution.

- [ ] **Step 5: Reconfigure/build/run and verify GREEN**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target ConfigGuiHostTests ConfigGUI iDmacDrv32
ctest --test-dir build-msvc32-debug -R '^(ConfigGuiHostTests|AsioSdkConfigureTests|DistributionArtifactTests)$' --output-on-failure
```

Expected: all pass and `dist` contains no known retired ASIO artifacts.

- [ ] **Step 6: Commit**

```powershell
git add -- CMakeLists.txt cmake/AsioSdk.cmake tools/ConfigGUI tests/Config tests/CMake THIRD_PARTY_NOTICES.md
git commit -m "Keep deployable dist runtime-only"
```

### Task 4: Final package, binary, and branch verification

**Files:**
- Modify if needed: `docs/superpowers/specs/2026-08-09-clean-asio-distribution-design.md`
- Modify if needed: `docs/reverse-engineering/asio-runtime-validation.md`

**Interfaces:**
- Consumes: clean committed branch, external SDK, both CMake presets.
- Produces: clean x86 Debug/Release builds, matching source archive outside `dist`, unchanged DLL exports, and evidence-bound handoff.

- [ ] **Step 1: Run source/reference checks**

```powershell
rg -n "AsioProbe\.exe|GC_ASIO_COMPATIBLE_LOGO|AsioLogoTexture|ASIO-compatible-logo" CMakeLists.txt cmake src tools tests docs SOURCE-OFFER.md THIRD_PARTY_NOTICES.md
git diff --check main...HEAD
```

Expected: only historical design/plan or explicit absence assertions remain; diff check is clean.

- [ ] **Step 2: Build and test both complete graphs**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4 --output-on-failure

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4 --output-on-failure
```

Expected: both complete suites pass.

- [ ] **Step 3: Verify exact deployable contents and PE contract**

Inspect both `dist` directories. Require only the four primary project artifacts, no source ZIP/logo/licenses/helper/INI, x86 machine `0x014c` for both binaries, and the loader's existing 15-export set.

- [ ] **Step 4: Commit and package exact corresponding source**

Commit any final documentation correction, ensure the worktree is clean, then run:

```powershell
cmake --build --preset msvc32-release --target gc-package-corresponding-source
```

Expected: one current archive under `build-msvc32-release/source-package`, none under `dist`; the archive contains the exact commit and offline build script.

- [ ] **Step 5: Re-run final focused package/dist tests and inspect status**

```powershell
ctest --test-dir build-msvc32-release -R '^(CorrespondingSourcePackageTests|DistributionArtifactTests)$' --output-on-failure
git status --short --branch
git diff --check main...HEAD
```

Expected: tests pass, branch is clean, and gameplay/deployment remains explicitly untested.
