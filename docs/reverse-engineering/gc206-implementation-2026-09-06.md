# GC 2.06 implementation and validation record

Date: 2026-09-06. Implementation baseline: `86807302a17677abab9871f6d72b657c94ee67b5`.
Status: implemented inline and compiled; real-process acceptance not performed.

The widescreen counts and mapping below describe the initial implementation.
The [subsequent narrowed widescreen port](gc206-narrowed-widescreen-2026-09-06.md)
supersedes its 83-hook policy with 88 hooks and corrects the title/top-bar
ownership mapping. The operator confirmed that fix works after its dotted-line
follow-up and authorized cleanup/commit. Other feature records below are unchanged.

## Implemented scope

One DLL now selects game 2.06 or 4.71 and independently selects NESYS 2.86.1
or 2.97. Exact known hashes retain their direct profile selection. Unknown
hashes require the existing structural candidate and complete local contract.
The plan-context validator now recognizes both game and both NESYS builds;
the legacy-patched variant remains exclusive to the known 4.71 profile.

The mandatory compatibility, renderer recovery, Test Mode and judgement
lifecycle profiles are present. Song unlock, Switch input, ASIO close,
AutoPlay, high-FPS, countdown and current widescreen profiles are included.
Disabled absolute judgement still contributes its four lifecycle/configuration
operations; ordinary 60 Hz still contributes OuterFrame.

Version-specific native differences remain feature-owned:

- AutoPlay bypasses the old card-save construction branch at RVA `0x1EF52A`.
  The native post-save completion states remain reachable. Its marker, native
  auto-play and IsMute operations form the same five-operation feature.
- Framerate profiles use static arrays exposed as spans: 17 writes, 48 hooks
  and seven targets for 2.06, versus 17/53/10 for 4.71. Authored operand,
  countdown-owner, countdown-asset and tutorial-source registers are explicit.
  Native relocated instructions retain their destination stores.
- The old audio-resync continuation is `0x20CD07`, preserving debug/overlay
  processing. Two remote cadence and three UnlockReward hooks are omitted;
  absent UnlockReward prompt inspection is gated before any object read.
- Countdown uses its own `0x203F80` delta helper and 26 calls in thirteen pairs.
- Widescreen contains 86 byte contracts and nine pointer contracts, sharing
  the current 83 hooks and compositor policy. The effect collection is
  `CTune+0x1D60`; Test Mode uses `0x207F3C/0x207F41`.
- NESYS 2.86.1 uses the existing EAX/ECX ping ABI at RVA `0x8E20`.

The 2.06 effect-timing diagnostic inventory describes its 32 mapped timing
hooks. The more extensive 4.71 registration/duration inventory is not
attributed to the older build.

## Verification

| Evidence | Result |
| --- | --- |
| Final 2.06 source contracts against IDA and executable bytes | 266/266 match |
| 2.06 detour widths, interior branch entries and mutation overlaps | No mismatches, entries or overlaps |
| 4.71 source contracts against its IDB/executable | 276 original contracts match; the four compatibility sites match the known already-installed replacements |
| 4.71 detour widths, interior branch entries and mutation overlaps | No mismatches, entries or overlaps |
| NESYS 2.86.1 source ping contract | Matches IDA and executable, including the 32-byte prefix |
| Complete `msvc32-debug` configure/build | Passed |
| Complete `msvc32-release` configure/build | Passed (`RelWithDebInfo`) |
| CLion diagnostics after opening each changed source file | 26 files, no reported errors or warnings |
| Unit/synthetic native-patch tests | Not added or run, per repository native-work policy |
| Game/NESYS runtime acceptance | Not performed |

Contract counts cover the union of the feature profiles and audio choices;
they are not the installed operation count for a single configuration.
Read-only contracts may intentionally cover code also owned by a detour.
The overlap check above concerns mutations.

The repeatable read-only IDA queries and results are under
`.codex-tmp/gc206-2026-09-06/`:

- `query_implementation_core.py` and `gc206-implementation-core.json`;
- `query_implementation_framerate.py` and `gc206-implementation-framerate.json`;
- `query_implementation_widescreen.py` and `gc206-implementation-widescreen.json`;
- `query_final_source_contracts.py` and the corresponding
  `gc206-`, `gc471-`, and `nesys206-final-source-contracts.json`;
- `clion-diagnostics.json`, configure logs and build logs.

Each IDA batch requires the real backend and disconnects its client afterward.
No IDB mutations or saves, deployments, game launches, process restarts, commits,
or pushes were performed.

## Prepared artifacts and baseline configuration

The built release artifact is
[build-msvc32-release/dist/iDmacDrv32.dll](../../build-msvc32-release/dist/iDmacDrv32.dll).
Its SHA-256 is
`1C11A92E05318890BACFDC458737584B5EA5C92A3ABF438946F5C43EEE570EF0`.

The Debug artifact is
[build-msvc32-debug/dist/iDmacDrv32.dll](../../build-msvc32-debug/dist/iDmacDrv32.dll).
Its SHA-256 is
`B95D06767E948CC85244FF13522628EBFEA13F6FF20264AE6A4DB64C180371D0`.

The complete distributed [config.toml](../../build-msvc32-release/dist/config.toml)
provides the initial operator baseline: keyboard/Arcade input, 1000 Hz polling,
60 FPS, DirectSound, NESYS adapter patch enabled, registry override disabled,
and optional AutoPlay, exact judgement, countdown freeze, song unlock,
storage redirection and widescreen disabled. Machine-specific settings must
reflect the actual operator setup before deployment; no partial TOML replaces
the complete runtime configuration.

## Pending operator observations

All rows are **not performed**. Deployment and exact process actions require
separate authorization. Preserve the existing 4.71 runtime until that decision.

| Configuration | Required observations | Status |
| --- | --- | --- |
| Baseline 2.06 / NESYS 2.86.1 | Boot; selected build names; Test Mode timing rows; input; card/storage; DirectSound; NESYS child startup | Not performed |
| Song unlock and Switch input | Native song availability and pressed/held/diagonal input | Not performed |
| AutoPlay off/on | Authored judgement and hidden-note completion; persistent marker; no result CSV or ordinary card-result save; native completion; protected results unchanged | Not performed |
| Widescreen | Twenty bar pairs, names, all four counter forms, judgement/tutorial placement, network icons, Test Mode and device reset | Not performed |
| High-FPS and timer freeze | Chart/judgement/score timing, menu progression, thirteen countdown pairs; each supported audio-clock backend | Not performed |
| 4.71 / NESYS 2.97 | Regression boot and previously accepted input, audio, AutoPlay and widescreen behavior | Not performed |

Record the executable and deployed DLL identities, complete configuration,
process-specific logs and operator observations with each authorized run.
Compilation and native contract agreement do not establish runtime behavior.

See the [native audit](gc206-new-patches-2026-09-06.md) and
[implementation plan](../superpowers/plans/2026-09-06-gc206-version-support.md).
