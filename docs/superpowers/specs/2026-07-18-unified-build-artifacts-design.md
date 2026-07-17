# Unified Build Artifacts Design

**Date:** 2026-07-18

## Context

The source-layout refactor gives each runtime feature and tool a clear CMake owner, but the primary deployable artifacts still land in different build subdirectories. `iDmacDrv32.dll` is emitted from the runtime target, `ConfigGUI.exe` is emitted from the tool target, and the tracked `config.toml` template is not staged at all. A developer must locate and copy each artifact manually before deployment or release packaging.

Both the loader and ConfigGUI resolve `config.toml` from the process working directory. Placing the DLL, GUI, and template together therefore matches their runtime contract as well as simplifying packaging.

## Goals

- Provide explicit Win32 Debug and release-oriented build entry points.
- Keep Debug and release-oriented outputs in separate top-level build trees.
- Emit the main deployment artifacts into one `dist` directory within each build tree.
- Stage the tracked `config.toml` byte-for-byte as the deployment template.
- Preserve the existing x86 MSVC, Ninja, DLL export, and test contracts.

## Non-Goals

- Building an installer, archive, or CPack package.
- Copying PDBs, import libraries, tests, or intermediate files into `dist`.
- Deploying artifacts into the `H:\gc` runtime tree.
- Preserving edits made to generated `dist/config.toml` files.
- Replacing the existing requirement to initialize an x86 MSVC developer environment.

## Build Entry Points

Add `CMakePresets.json` with a hidden shared Ninja preset and two public configure presets:

| Preset | CMake configuration | Binary directory | Distribution directory |
|---|---|---|---|
| `msvc32-debug` | `Debug` | `build-msvc32-debug` | `build-msvc32-debug/dist` |
| `msvc32-release` | `RelWithDebInfo` | `build-msvc32-release` | `build-msvc32-release/dist` |

Matching build and test presets use the same names. Tests run with failure output enabled. The presets deliberately retain the verified Ninja toolchain and are invoked from the same `vcvars32` environment used by the current build.

The name `msvc32-release` describes the deployment-oriented build tree. It intentionally selects `RelWithDebInfo` so optimized binaries retain diagnostic symbols outside the deployment directory.

## Distribution Contract

Root CMake defines one distribution path derived solely from the active binary tree:

```cmake
set(GC_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
```

The `iDmacDrv32` and `ConfigGUI` targets set their runtime output directories to `GC_DIST_DIR`. No global runtime output variable is used because that would also place test executables in `dist`.

After a successful complete build, each distribution directory contains these primary artifacts:

```text
dist/
├── ConfigGUI.exe
├── config.toml
└── iDmacDrv32.dll
```

Other generator or linker outputs remain in their normal build-tree locations. In particular, PDBs, import libraries, and test executables are not part of this three-file deployment contract.

## Configuration Template Flow

During CMake configuration, the tracked root `config.toml` is copied with `COPYONLY` semantics to `${GC_DIST_DIR}/config.toml`. CMake treats the source template as a configure dependency, so changing it triggers regeneration and refreshes the staged copy.

The distribution directory is generated and disposable. Reconfiguration may overwrite edits made to its `config.toml`; developers who need a customized runtime configuration copy the completed `dist` directory to their deployment location and edit the deployed copy there.

## Failure Behavior

- Configuration fails if the source template cannot be read or the distribution directory cannot be created or written.
- Compilation and linking retain their normal failure behavior.
- The DLL and GUI are written directly to `dist`; no second post-build copy can become stale independently.
- A failed build may leave artifacts from an earlier successful build in the same binary tree. Release automation must rely on a successful build result, not directory existence alone.

## Verification

Verification covers both presets:

1. Configure and fully build `msvc32-debug` and `msvc32-release` from an x86 MSVC developer environment.
2. Assert that each `dist` directory contains `iDmacDrv32.dll`, `ConfigGUI.exe`, and `config.toml`.
3. Compare each staged config SHA-256 with the tracked source template.
4. Compare each preset's CTest inventory with the established 28-test baseline and run the full suite.
5. Compare the release-oriented DLL's 15 exported name/ordinal pairs with the established baseline.
6. Confirm tests, import libraries, and PDBs are not required members of the distribution contract.

These are build/static checks only. Deployment into the runtime tree and gameplay acceptance remain separate manual gates.
