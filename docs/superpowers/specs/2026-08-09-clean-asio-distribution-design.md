# Clean ASIO Distribution Design

**Date:** 2026-08-09
**Status:** Approved in conversation

## Context

`GC_DIST_DIR` is the generated, runnable deployment staging directory. Its
established contract is the loader DLL, ConfigGUI, and the operator-editable
configuration/card templates. The initial ASIO implementation incorrectly
also treated this directory as a complete legal-release bundle. It copied
license documents and an optional Steinberg logo during configuration, copied
the corresponding-source archive there when its packaging target ran, and
staged a dedicated probe executable. ConfigGUI also retained Dear ImGui's
default `imgui.ini` persistence in its working directory.

This design changes only distribution ownership and ConfigGUI's validation
host. It does not change ASIO discovery, exact buffer validation, runtime
revalidation, rendering, fallback, or clock behavior.

## Deployable Directory Contract

After a normal complete build, `dist` contains these project-owned primary
artifacts:

```text
dist/
|-- ConfigGUI.exe
|-- config.toml
|-- card.txt
`-- iDmacDrv32.dll
```

The build configuration removes only known retired project artifacts from this
generated directory: `AsioProbe.exe`, the previously copied compatible logo,
`imgui.ini`, the old `licenses` directory, and matching GCLoader
corresponding-source ZIPs. It does not delete arbitrary operator files.

`dist` is a deployment staging directory, not by itself a complete public GPL
release bundle.

## Release and License Artifacts

Project license documents, third-party notices, and `SOURCE-OFFER.md` remain in
the repository and in the corresponding-source package. The
`gc-package-corresponding-source` target continues to verify and publish the
exact archive under the build tree's `source-package` directory, but it no
longer duplicates that archive into `dist`.

Any future externally conveyed binary release must package the applicable GPL
license/notices and provide matching corresponding-source access through an
allowed distribution mechanism. That release concern does not change the
runnable `dist` contract.

## ASIO Logo

The optional ASIO Compatible logo and its ConfigGUI texture-loading code are
removed. CMake no longer requires or copies the artwork, and the ASIO settings
UI uses text only. The SDK's headers and license remain required build inputs;
the logo is neither an ASIO runtime dependency nor a GPL requirement.

## Self-Hosted Isolated Probe

Save-time validation remains out of process because it loads an arbitrary
vendor ASIO DLL. ConfigGUI recognizes one fixed internal command-line mode:

```text
ConfigGUI.exe --asio-probe
```

This mode is selected before ConfigGUI initializes its GUI COM apartment,
loads configuration, or creates Dear ImGui/D3D state. It runs the existing
bounded stdin/stdout probe protocol, creates the hidden driver window, owns its
own COM apartment, and returns the same structured result and exit codes.

The parent launches its own absolute executable path with only the fixed mode
argument. Driver names and numeric selections continue to travel exclusively
through the bounded binary stdin message, never through the command line. The
existing suspended launch, restricted inherited handles, kill-on-close Job
Object, output limit, and five-second timeout remain unchanged.

The dedicated `AsioProbe` target and executable are removed. Runtime ASIO
startup in the game process remains authoritative and unchanged.

## Dear ImGui State

Immediately after creating the Dear ImGui context, ConfigGUI sets
`ImGuiIO::IniFilename` to `nullptr`. The GUI has one fixed operator-facing
layout and does not need a second configuration file for window placement.
Launching and closing ConfigGUI therefore does not create `imgui.ini` in
`dist` or any other working directory.

## Failure Behavior

- A self-probe crash, malformed response, launch failure, or timeout still
  prevents Save and leaves the configuration file byte-for-byte unchanged.
- A missing or renamed ConfigGUI executable is reported as a probe launch
  failure through the existing typed failure path.
- Unknown command-line values keep the existing interpretation as a config
  path; only the exact standalone `--asio-probe` form selects internal mode.
- Removing the logo cannot block ConfigGUI startup or ASIO configuration.

## Verification

Behavioral verification covers:

1. the probe client launches the current executable with the fixed internal
   argument while preserving all isolation controls;
2. the real `ConfigGUI.exe --asio-probe` path exchanges the existing protocol
   without initializing the GUI;
3. ConfigGUI's real ImGui host disables INI persistence;
4. SDK configuration succeeds without the optional logo artwork;
5. corresponding-source packaging publishes only to `source-package`;
6. a complete build leaves the deployable `dist` contract free of the retired
   logo, helper, licenses, source ZIPs, and `imgui.ini`;
7. the full Debug and Release suites pass and the DLL export surface remains
   unchanged.

Gameplay deployment and ASIO latency acceptance remain outside this cleanup.
