# Third-Party Notices

GCLoader uses the following third-party components. Each component retains its
own copyright and license terms.

## Steinberg ASIO SDK

- Source: https://www.steinberg.net/developers/asiosdk-open/
- Required interface version: ASIO SDK 2.3.4 or newer
- Build input: external `GC_ASIO_SDK_DIR`; the SDK is not vendored or fetched
- License choice for GCLoader's ASIO-enabled combined program: GNU General
  Public License Version 3
- Local license copied into distributions as `ASIO-SDK-LICENSE.txt`
- Exact SDK tree copied into the corresponding-source ZIP shipped with each
  ASIO-enabled binary distribution

ASIO is a registered trademark of Steinberg Media Technologies GmbH. The
official ASIO Compatible logo is used unmodified under the usage guidelines
shipped with the SDK.

The matching-source ZIP also contains the exact configured source trees for
all dependencies below, including SafetyHook's transitive Zydis and embedded
Zycore sources. Its generated manifest records an SHA-256 inventory, requested
revision, resolved Git commit when available, and a deterministic tree hash.

## MinHook

- Source: https://github.com/TsudaKageyu/minhook
- Revision: `c3fcafdc10146beb5919319d0683e44e3c30d537`
- License: BSD-2-Clause

## miniaudio

- Source: https://github.com/mackron/miniaudio
- Revision: `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`
- License choice: Public Domain alternative (an MIT No Attribution alternative
  is also offered upstream)

## toml++

- Source: https://github.com/marzer/tomlplusplus
- Requested revision: `v3.4.0`
- Resolved revision: `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`
- License: MIT

## SafetyHook

- Source: https://github.com/cursey/safetyhook
- Requested revision: `v0.7.0`
- Resolved revision: `19223663fb8a573253ffb2e82da87cc354bf5c16`
- License: Boost Software License 1.0

## Zydis and Zycore

- Source: https://github.com/zyantific/zydis and its Zycore dependency
- Revisions resolved by the pinned SafetyHook tree:
  - Zydis: `569320ad3c4856da13b9dbf1f0d9e20bda63870e`
  - Zycore: `74620eefd233bec20daeb66e78e744ff06e273b7`
- License: MIT

## reflect-cpp

- Source: https://github.com/getml/reflect-cpp
- Revision: `v0.25.0`
- License: MIT

## plog

- Source: https://github.com/SergiusTheBest/plog
- Revision: `1.1.11` (`e5c033e317a01b2703d13aab42288d09b2efdafc`)
- License: MIT

## Dear ImGui

- Source: https://github.com/ocornut/imgui
- Revision: `v1.92.8`
- License: MIT
