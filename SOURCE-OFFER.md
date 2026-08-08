# GCLoader ASIO Corresponding Source

Every distributed ASIO-enabled GCLoader binary must be accompanied by the
matching archive named:

```text
GCLoader-<full-git-commit>-corresponding-source.zip
```

The archive is the release's complete corresponding-source package. It
contains the exact committed GCLoader source, the complete Steinberg ASIO SDK
tree used for that build, every configured direct and transitive FetchContent
source tree, applicable license files, build metadata, SHA-256 inventory, and
an offline PowerShell configure/build script. The package target accepts only a
clean committed repository so the commit in its name and manifest identifies
the project source used for the binary.

Project-authored files remain dedicated under CC0-1.0 individually. The
distributed ASIO-enabled combined work is conveyed under GPL-3.0-only because
it incorporates the ASIO SDK interface under the SDK's GPLv3 option. ASIO SDK
and other third-party files retain their own notices and terms. See
`LICENSE.md`, `LICENSES/`, and `THIRD_PARTY_NOTICES.md` inside the archive.

To reproduce the Win32 build, extract the archive, enter an x86 MSVC developer
environment (for example by running `vcvars32.bat`), and run:

```powershell
.\configure-offline.ps1
```

The script uses only the copied `third_party/asiosdk` and
`third_party/fetchcontent` trees, enables
`FETCHCONTENT_FULLY_DISCONNECTED`, and builds `iDmacDrv32`, `ConfigGUI`, and
`AsioProbe`. Ordinary development may point `GC_ASIO_SDK_DIR` at a separately
downloaded SDK, but a mutable upstream link is not a substitute for this
release archive.

The proprietary game, game data, local configuration, vendor ASIO driver
binaries, credentials, and build outputs are intentionally not part of the
source archive. This file records the project's approved distribution policy;
it is not legal advice.
