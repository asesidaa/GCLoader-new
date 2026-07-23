# Complete CTune Effect Timing Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct every gameplay `CTuneEffect` producer in `game471.exe` so tutorial, hit, judgement, countdown, chart/gimmick, player-position, remote, managed, externally framed, and child effects retain their authored 60 FPS speed at 120, 144, and 240 FPS without regressing native 60 FPS.

**Architecture:** Keep each executable subsystem in its current clock domain and normalize only proven producer boundaries. A checked C++ manifest reconciles all 34 effect registrations, all nine authored-duration queries, direct frame writers, existing effect hooks, and the four missing crossings. An offline Python catalog provides independent asset-side coverage without changing runtime data. The transformed framerate transaction grows from 42 to 46 checked hook contracts; `OuterFrame` remains last, native 60 FPS installs no effect-timing hook, existing optional WASAPI selection is preserved, and any mismatch or conversion failure remains fail-closed.

**Tech Stack:** C++23, Win32/x86, SafetyHook 0.6.9, CMake/Ninja presets, CTest, Python 3 standard library, daemon-backed `ida-cli`, and the `H:\gc\game471.exe.i64` IDA database.

**Design:** [Complete CTuneEffect High-FPS Timing Design](../specs/2026-07-24-complete-ctune-effect-timing-design.md)

**Implementation baseline:** `5f6f0fa` (`docs: record player effect modulo timing boundary`)

## Global Constraints

- Work and commit only in `H:\gc\artifacts\GCLoader`. Treat `H:\gc` as the runtime/deployment and read-only evidence tree.
- Never modify, copy, repack, delete, or commit anything under `H:\gc\data\effect\game`.
- Target only the executable whose input SHA-256 is `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` and whose image base is `0x00400000`.
- Reuse the daemon-backed `H:\gc\game471.exe.i64` session through `AgentSession.start(..., daemon=True)`; do not open a competing raw IDA session.
- Preserve the invariant that `effect + 0x08` is an authored-60 frame when `sub_5F1F70` consumes it.
- Preserve target-frame simulation, `Tune + 0x10`, millisecond timelines, normalized-progress paths, and child inheritance in their proven domains.
- Use only `MapPositiveTargetFrameToAuthored60` and `ScalePositiveDuration` for the new integer conversions. Do not duplicate arithmetic.
- Preserve zero and signed nonpositive sentinels bit-for-bit. Checked arithmetic failure remains fatal.
- Do not add a renderer-wide hook, effect-pointer side table, asset-length hardcoding, global frame-duration replacement, or runtime asset parser.
- Each new hook must have an exact expected-byte contract and belong to the existing preflight-first, all-or-nothing framerate transaction.
- The full checked contract set is exactly 46 hooks. `OuterFrame` is index 45 and remains last. A transformed plan contains 45 hooks when the optional WASAPI resync hook is excluded and 46 when it is committed. Native 60 FPS installs no effect-timing hook; it retains `OuterFrame` and preserves the existing optional WASAPI resync selection.
- Build with the repository's `msvc32-debug` and `msvc32-release` presets. Build/static completion is not gameplay acceptance.
- Do not deploy a DLL or mutate live configuration as part of plan execution unless the user explicitly requests that runtime action.

## File and Responsibility Map

| File | Responsibility |
|---|---|
| `tools/analysis/__init__.py` | Makes the offline analysis helpers importable by standard-library tests. |
| `tools/analysis/ctune_effect_catalog.py` | Strict, read-only parser and deterministic Markdown catalog generator for `data\effect\game`. |
| `tools/analysis/tests/__init__.py` | Test package marker. |
| `tools/analysis/tests/test_ctune_effect_catalog.py` | Synthetic container, definition, PNG, ordering, and malformed-input tests. |
| `docs/reverse-engineering/ctune-effect-asset-catalog.md` | Generated asset-side evidence pinned to the current live directory hashes. |
| `docs/reverse-engineering/ctune-effect-producer-manifest.md` | Human-readable IDA evidence, exact census, exclusions, and code-manifest crosswalk. |
| `src/Patches/Framerate/FramerateEffectTiming.h/.cpp` | Authoritative registration/duration/producer manifest, effect-hook contract view, summaries, and pure register transforms. |
| `src/Patches/Framerate/FrameratePatchPlan.h/.cpp` | Add four stable hook IDs and merge pre-effect, effect, and post-effect contract views into the exact 46-contract order. |
| `src/Patches/Framerate/FrameratePatchTransaction.h` | Raise the checked hook capacity from 42 to 46. |
| `src/Patches/Framerate/FrameratePatch.cpp` | Own four new hooks, callbacks, per-site counters, bindings, failure handling, and statistics publication. |
| `src/Patches/Framerate/FramerateDiagnostics.h/.cpp` | Publish manifest coverage and format aggregated effect runtime statistics. |
| `src/Patches/CMakeLists.txt` | Compile `FramerateEffectTiming.cpp` into `gc_runtime_patches`. |
| `tests/Patches/Framerate/FramerateEffectTimingTests.cpp` | Manifest exhaustiveness, exact hook contracts, summary counts, and pure transform tests. |
| `tests/Patches/Framerate/FrameratePatchPlanTests.cpp` | Exact 46-contract merged plan, native bypass, optional WASAPI count, and outer-hook ordering. |
| `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp` | Failure injection and rollback at all 46 hook positions. |
| `tests/Patches/Framerate/FramerateRuntimeTests.cpp` | Runtime binding coverage for all 46 IDs and context-isolation checks. |
| `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp` | Native/transformed manifest fields and effect-stat formatting. |
| `tests/Patches/CMakeLists.txt` | Register `FramerateEffectTimingTests`. |
| `docs/reverse-engineering/ctune-effect-runtime-acceptance.md` | Operator-owned 60/120/144/240 gameplay matrix; create only when runtime testing begins. |

---

### Task 1: Build the Read-Only Asset Catalog

**Files:**
- Create: `tools/analysis/__init__.py`
- Create: `tools/analysis/ctune_effect_catalog.py`
- Create: `tools/analysis/tests/__init__.py`
- Create: `tools/analysis/tests/test_ctune_effect_catalog.py`
- Create: `docs/reverse-engineering/ctune-effect-asset-catalog.md`

**Interfaces:**

```python
@dataclass(frozen=True)
class OffsetContainer:
    declared_size: int
    record_count: int
    offsets: tuple[int, ...]
    records: tuple[bytes, ...]

@dataclass(frozen=True)
class EffectTrack:
    track_type: int
    texture_slot: int | None
    raw: bytes

@dataclass(frozen=True)
class EffectDefinition:
    index: int
    authored_frames: int
    tracks: tuple[EffectTrack, ...]

@dataclass(frozen=True)
class PngInfo:
    width: int
    height: int
    sha256: str

def read_u16_be(data: bytes, offset: int) -> int: ...
def read_u32_be(data: bytes, offset: int) -> int: ...
def parse_offset_container(data: bytes) -> OffsetContainer: ...
def parse_effect_definition(index: int, record: bytes) -> EffectDefinition: ...
def parse_png(payload: bytes) -> PngInfo: ...
def catalog_effect_directory(root: Path) -> dict[str, object]: ...
def render_catalog_markdown(catalog: Mapping[str, object], root: Path) -> str: ...
```

- [ ] **Step 1: Write failing parser tests from synthetic bytes**

Create package marker files and add tests that construct fixtures without reading `H:\gc`. The core fixture helper must encode the proven container layout:

```python
def make_container(records: list[bytes]) -> bytes:
    header_size = 6 + 4 * (len(records) + 1)
    offsets = [header_size]
    for record in records:
        offsets.append(offsets[-1] + len(record))
    body = b"".join(records)
    declared_size = header_size + len(body)
    return (
        declared_size.to_bytes(4, "big")
        + len(records).to_bytes(2, "big")
        + b"".join(offset.to_bytes(4, "big") for offset in offsets)
        + body
    )

def make_effect_definition(
    authored_frames: int,
    tracks: list[bytes],
) -> bytes:
    table_size = 3 + 2 * len(tracks)
    offsets: list[int] = []
    cursor = table_size
    for track in tracks:
        offsets.append(cursor)
        cursor += len(track)
    return (
        authored_frames.to_bytes(2, "big")
        + bytes([len(tracks)])
        + b"".join(offset.to_bytes(2, "big") for offset in offsets)
        + b"".join(tracks)
    )
```

Required assertions:

```python
container = parse_offset_container(make_container([b"abc", b"de"]))
self.assertEqual(container.declared_size, len(make_container([b"abc", b"de"])))
self.assertEqual(container.record_count, 2)
self.assertEqual(container.records, (b"abc", b"de"))

definition = parse_effect_definition(
    61,
    make_effect_definition(38, [b"\x02\x00\x0d\xaa", b"\x00\xff\xff"]),
)
self.assertEqual(definition.authored_frames, 38)
self.assertEqual(definition.tracks[0].texture_slot, 13)
self.assertIsNone(definition.tracks[1].texture_slot)
```

Also test:

- declared size does not equal actual byte length;
- truncated offset table;
- first record offset differs from `6 + 4 * (count + 1)`;
- non-increasing, out-of-bounds, and final offsets not equal to declared size;
- definition shorter than three bytes;
- track table truncation or an offset inside the table;
- type `0x02` track shorter than three bytes;
- PNG bad signature, missing `IHDR`, truncated dimensions, and zero dimensions;
- deterministic filename sorting and lowercase SHA-256 output;
- the output path is not opened by `catalog_effect_directory`.

- [ ] **Step 2: Run the tests and verify the expected failure**

Run from `H:\gc\artifacts\GCLoader`:

```powershell
python -m unittest tools.analysis.tests.test_ctune_effect_catalog -v
```

Expected: failure because `tools.analysis.ctune_effect_catalog` does not yet define the required parser API. A passing run at this point means the test did not exercise the new module.

- [ ] **Step 3: Implement strict parsing and deterministic rendering**

Implement the interfaces with these exact format rules:

- Container offset `0`: BE32 declared byte size.
- Container offset `4`: BE16 record count.
- Container offset `6`: `count + 1` BE32 offsets.
- `offsets[0] == 6 + 4 * (count + 1)`.
- `offsets[-1] == declared_size == len(data)`.
- Every record is nonempty, strictly ordered, and bounded by adjacent offsets.
- Effect definition offset `0`: BE16 authored duration.
- Effect definition offset `2`: U8 track count.
- Track offsets begin at offset `3`, contain exactly `count` BE16 values, and point after the table.
- Track end is the next track offset or definition length.
- Track byte `0` is the proven type. Only type `0x02` exposes a BE16 texture slot at track offset `1`; all other bytes remain raw and unnamed.
- PNG parsing accepts only the eight-byte PNG signature followed by an `IHDR` chunk with positive BE32 width and height.
- Use `hashlib.sha256`; do not add dependencies.
- CLI syntax is:

```powershell
python tools/analysis/ctune_effect_catalog.py `
  --root 'H:\gc\data\effect\game' `
  --output 'docs\reverse-engineering\ctune-effect-asset-catalog.md'
```

- The CLI resolves both paths, rejects an output located inside the input root, reads the input only, and creates only the output parent/file.
- Markdown ordering is core containers (`efcdata.dat`, `effect.dat`, `uvdata.dat`), definitions by numeric index, then images by natural `(img/img_big, numeric slot, language, suffix)` order.

- [ ] **Step 4: Run the synthetic tests**

```powershell
python -m unittest tools.analysis.tests.test_ctune_effect_catalog -v
```

Expected: all catalog tests pass.

- [ ] **Step 5: Generate and check the live evidence document**

```powershell
python tools/analysis/ctune_effect_catalog.py `
  --root 'H:\gc\data\effect\game' `
  --output 'docs\reverse-engineering\ctune-effect-asset-catalog.md'
git diff --check
```

The generated document must assert these current canaries:

| File | Bytes | SHA-256 | Records |
|---|---:|---|---:|
| `efcdata.dat` | 42748 | `0eee698b17a04611e411b929be685419758568247ae9a5dc094d3dc914dbbc87` | 89 |
| `effect.dat` | 15365 | `8e0ee9f862f8a94853c187adfb2e401d604d8bda5ba86906fc86272e9edbf098` | 43 |
| `uvdata.dat` | 2810 | `d224d52de30c48fe5d1800d7c40c9b2417202882d85f0916b6098667ea791678` | 16 |

It must also record:

- 34 `.bin` image payloads, all with PNG signatures;
- tutorial definitions 61 through 69 with authored durations
  `16, 38, 34, 13, 16, 14, 14, 38, 38`;
- every type-`0x02` track in definitions 61 through 69 references texture slot 13;
- `img13.bin`, `img13_eng.bin`, `img_big13.bin`, and
  `img_big13_eng.bin` exist and are cataloged independently.

Run the generator a second time and verify byte-identical output:

```powershell
$catalogHashBefore = (Get-FileHash -Algorithm SHA256 `
  'docs\reverse-engineering\ctune-effect-asset-catalog.md').Hash
python tools/analysis/ctune_effect_catalog.py `
  --root 'H:\gc\data\effect\game' `
  --output 'docs\reverse-engineering\ctune-effect-asset-catalog.md'
$catalogHashAfter = (Get-FileHash -Algorithm SHA256 `
  'docs\reverse-engineering\ctune-effect-asset-catalog.md').Hash
if ($catalogHashBefore -ne $catalogHashAfter) {
  throw 'CTune effect catalog output is not deterministic'
}
```

Expected: exit code 0 on the second run.

- [ ] **Step 6: Commit the asset catalog**

```powershell
git add -- tools/analysis docs/reverse-engineering/ctune-effect-asset-catalog.md
git commit -m "test: catalog CTune effect assets"
```

---

### Task 2: Add the Exhaustive Effect-Timing Manifest and Contract View

**Files:**
- Create: `src/Patches/Framerate/FramerateEffectTiming.h`
- Create: `src/Patches/Framerate/FramerateEffectTiming.cpp`
- Create: `tests/Patches/Framerate/FramerateEffectTimingTests.cpp`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.h`
- Modify: `src/Patches/Framerate/FrameratePatchPlan.cpp`
- Modify: `src/Patches/CMakeLists.txt`
- Modify: `tests/Patches/CMakeLists.txt`

**Interfaces:**

```cpp
enum class EffectTimingDisposition {
    Hook,
    ManagerGated,
    AlreadyAuthoredNormalized,
    ResetOrConstant,
    ChildInherited,
    NonCtuneOutOfScope,
};

enum class EffectClockDomain {
    TargetFrame,
    Authored60Frame,
    Milliseconds,
    NormalizedProgress,
    ConstantOrSentinel,
    NonCtuneData,
};

struct EffectRegistrationSite {
    std::uintptr_t rva{};
    const char* owner{};
    const char* reaching_frame_path{};
};

struct EffectDurationQuerySite {
    std::uintptr_t rva{};
    const char* owner{};
    const char* consumer_path{};
};

struct EffectTimingSite {
    const char* stable_id{};
    std::uintptr_t boundary_rva{};
    EffectClockDomain source{};
    EffectClockDomain consumer{};
    EffectTimingDisposition disposition{};
    std::optional<FramerateHookId> hook_id{};
    const char* evidence{};
};

struct EffectTimingManifestSummary {
    std::size_t timing_sites{};
    std::size_t registration_sites{};
    std::size_t duration_queries{};
    std::size_t hook_contracts{};
    std::size_t manager_gated{};
    std::size_t already_authored{};
    std::size_t reset_or_constant{};
    std::size_t child_inherited{};
    std::size_t non_ctune_out_of_scope{};
};

[[nodiscard]] std::span<const EffectRegistrationSite>
EffectRegistrationSites() noexcept;
[[nodiscard]] std::span<const EffectDurationQuerySite>
EffectDurationQuerySites() noexcept;
[[nodiscard]] std::span<const EffectTimingSite>
EffectTimingSites() noexcept;
[[nodiscard]] std::span<const FramerateHookContract>
FramerateEffectHookContracts() noexcept;
[[nodiscard]] EffectTimingManifestSummary
SummarizeEffectTimingManifest() noexcept;
```

- [ ] **Step 1: Add failing census and contract tests**

Register `FramerateEffectTimingTests` in `tests/Patches/CMakeLists.txt`, then add tests that compare the registration RVAs to this exact sorted set:

```cpp
constexpr std::array<std::uintptr_t, 34> kExpectedRegistrationRvas{
    0x001F02F5,
    0x00240674, 0x00240941, 0x00240CDE, 0x002412B5,
    0x00244BC0, 0x00244D30, 0x00244E20, 0x00244F10, 0x00245000,
    0x00246517, 0x00246693,
    0x00248F75, 0x002491C9, 0x002498E8, 0x0024999C,
    0x00249A53, 0x00249BEC,
    0x0024B61C, 0x0024BB11, 0x0024BC19, 0x0024BF72,
    0x0024C56C, 0x0024C5CA, 0x0024C607, 0x0024C8DC,
    0x0024CB4D, 0x0024CBC0, 0x0024CBFD,
    0x0024D710, 0x0024D779, 0x0024D7C4,
    0x0024EF82, 0x00250689,
};

constexpr std::array<std::uintptr_t, 9> kExpectedDurationQueryRvas{
    0x00246463, 0x0024647D,
    0x00248EA7, 0x00248EBF, 0x00249104,
    0x0024962C, 0x00249653, 0x00249790,
    0x0024A92F,
};
```

Required test rules:

- both returned views match the exact arrays after sorting;
- registration count is 34 and duration-query count is 9;
- every registration RVA, duration RVA, stable ID, and timing boundary RVA is unique in its own view;
- no owner, frame path, consumer path, stable ID, or evidence string is null or empty;
- every timing site has a final disposition because the enum has no `Unknown`;
- every manifest `hook_id` occurs exactly once;
- every `FramerateEffectHookContracts()` ID occurs in exactly one manifest site and vice versa;
- the four new IDs have the exact contracts below;
- the summary is derived from the arrays, not hardcoded independently.

The four new contract assertions are:

```cpp
struct ExpectedHook {
    FramerateHookId id;
    std::uintptr_t rva;
    BytePattern expected;
};

const std::array expected_new_hooks{
    ExpectedHook{
        FramerateHookId::EffectFlowItemFrame,
        0x001F0310,
        Pattern({0x89, 0x42, 0x08})},
    ExpectedHook{
        FramerateHookId::EffectTutorialElapsed,
        0x00249593,
        Pattern({0x89, 0x95, 0x74, 0xFF, 0xFF, 0xFF})},
    ExpectedHook{
        FramerateHookId::EffectChartPreRollDuration,
        0x0024A934,
        Pattern({0x89, 0x45, 0x9C})},
    ExpectedHook{
        FramerateHookId::EffectPlayerModuloDividend,
        0x0025072E,
        Pattern({0xF7, 0xF9})},
};
```

- [ ] **Step 2: Run the focused test and verify it fails**

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug --target FramerateEffectTimingTests
```

Expected: configure or compile failure because the new manifest module and hook IDs do not exist.

- [ ] **Step 3: Add the four stable hook IDs**

Insert these IDs after `PlayerPositionDenominatorB` and before
`NavigatorAdvance` in `FramerateHookId`:

```cpp
EffectFlowItemFrame,
EffectTutorialElapsed,
EffectChartPreRollDuration,
EffectPlayerModuloDividend,
```

Do not reorder any existing ID.

- [ ] **Step 4: Implement the registration and duration-query views**

Populate the 34 registrations with these reaching paths:

| Registration RVA(s) | Owner / reaching frame path |
|---|---|
| `0x001F02F5` | `sub_5F0220`; target-frame flow value crosses at `0x001F0310`. |
| `0x00240674`, `0x00240941`, `0x00240CDE`, `0x002412B5` | `GC120FPS_GameplayUpdate_120FrameToMs_JudgeMsWindows`; zero stores at `0x0024067C`, `0x0024094C`, `0x00240CE9`, `0x002412C0`. |
| `0x00244BC0`, `0x00244D30`, `0x00244E20`, `0x00244F10`, `0x00245000` | Target-cue owners; `definition_length * normalized_progress` stores at `0x00244BDE`, `0x00244D4E`, `0x00244E3E`, `0x00244F2E`, `0x0024501E`. |
| `0x00246517` | `sub_6463F0`; authored-ms lifetime/frame operands at `0x002464A8` and `0x00246528`, store at `0x00246533`. |
| `0x00246693` | `sub_646650`; zero store at `0x0024669B`. |
| `0x00248F75` | Main effect A; authored-ms operands at `0x00248F00` and `0x00248F8C`, store at `0x00248F97`. |
| `0x002491C9` | Main effect B; authored-ms operands at `0x0024912B` and `0x002491E0`, store at `0x002491F1`. |
| `0x002498E8`, `0x0024999C` | Tutorial pair; shared target-frame value crosses at `0x00249593`, then stores at `0x002498F9` and `0x002499AD`. |
| `0x00249A53` | Gameplay countdown; mapped store at `0x00249A9C`. |
| `0x00249BEC` | Direct effect; authored-ms operand at `0x00249C14`, store at `0x00249C22`. |
| `0x0024B61C` | Normalized chart store at `0x0024B680`. |
| `0x0024BB11` | Normalized chart store at `0x0024BB6A`. |
| `0x0024BC19` | Branches to authored-ms operand `0x0024BC8B` / store `0x0024BC99` or normalized store `0x0024BCC6`. |
| `0x0024BF72` | Normalized chart store at `0x0024BF9C`. |
| `0x0024C56C`, `0x0024C5CA`, `0x0024C607`, `0x0024C8DC` | Shared normalized chart store at `0x0024C935`. |
| `0x0024CB4D`, `0x0024CBC0`, `0x0024CBFD` | Branches to authored-ms operands `0x0024CC8A`/`0x0024CCBE`, zero clamp `0x0024CCE1`, or normalized store `0x0024CD12`. |
| `0x0024D710`, `0x0024D779`, `0x0024D7C4` | Branches to authored-ms operand `0x0024D836` or normalized store `0x0024D871`. |
| `0x0024EF82` | Player-position elapsed mapping at `0x0024EF43`, final store at `0x0024EF93`. |
| `0x00250689` | Player effect loops through mapped dividend `0x0025072E`, `idiv` remainder store at `0x00250736`. |

Populate the nine duration queries with:

| Duration query RVA(s) | Consumer |
|---|---|
| `0x00246463`, `0x0024647D` | GREAT/GOOD authored-length selection and lifetime. |
| `0x00248EA7`, `0x00248EBF` | Main effect A authored-length selection and lifetime. |
| `0x00249104` | Main effect B authored-length selection and lifetime. |
| `0x0024962C`, `0x00249653`, `0x00249790` | Tutorial shared elapsed comparisons after `0x00249593`. |
| `0x0024A92F` | Chart pre-roll target-frame comparison after scaling at `0x0024A934`. |

- [ ] **Step 5: Implement the timing-site table**

The effect hook-contract view is the existing contiguous gameplay-effect
contract block from `GameplayEffectAdvance` through
`PlayerPositionDenominatorB`, followed by the four new contracts. This yields
34 effect contracts. Move those existing contract literals from
`FrameratePatchPlan.cpp` into `FramerateEffectTiming.cpp` without changing any
ID, RVA, expected byte, name, or relative order.

Use a local constexpr byte-pattern constructor in the new `.cpp`; duplicate
only this construction helper, never contract data:

```cpp
template <typename... Values>
constexpr BytePattern EffectPattern(Values... values) noexcept {
    static_assert(sizeof...(Values) <= kMaximumPatternBytes);
    BytePattern result{};
    result.size = static_cast<std::uint8_t>(sizeof...(Values));
    std::size_t index = 0;
    ((result.bytes[index++] =
          static_cast<std::byte>(static_cast<std::uint8_t>(values))), ...);
    return result;
}
```

The 34 hook-backed timing rows are:

| Hook ID | RVA | Expected bytes | Disposition |
|---|---:|---|---|
| `GameplayEffectAdvance` | `0x00264E2D` | `E8 6E BA F8 FF` | `ManagerGated` |
| `EffectCadence6` | `0x0024063B` | `85 D2` | `Hook` |
| `EffectCadence5` | `0x002408D7` | `85 D2` | `Hook` |
| `EffectCadence4` | `0x00240C9C` | `85 D2` | `Hook` |
| `EffectCadence16A` | `0x00241213` | `85 D2` | `Hook` |
| `EffectCadence16B` | `0x0024122F` | `81 E1 0F 00 00 80` | `Hook` |
| `EffectCadence8` | `0x00241268` | `85 C0` | `Hook` |
| `RemoteCadenceA` | `0x002632DB` | `85 D2` | `Hook` |
| `RemoteCadenceB` | `0x00263646` | `85 D2` | `Hook` |
| `GameplayBlink` | `0x0024A1B9` | `D1 F8` | `Hook` |
| `GreatGoodLifetimeOperand` | `0x002464A8` | `D8 48 18` | `Hook` |
| `GreatGoodFrameOperand` | `0x00246528` | `D8 71 18` | `Hook` |
| `EffectLifetimeAOperand` | `0x00248F00` | `D8 49 18` | `Hook` |
| `EffectFrameAOperand` | `0x00248F8C` | `D8 72 18` | `Hook` |
| `EffectLifetimeBOperand` | `0x0024912B` | `D8 49 18` | `Hook` |
| `EffectFrameBOperand` | `0x002491E0` | `D8 72 18` | `Hook` |
| `DirectEffectFrameOperand` | `0x00249C14` | `D8 72 18` | `Hook` |
| `ChartEffectFrameAOperand` | `0x0024BC8B` | `D8 71 18` | `Hook` |
| `ChartEffectFrameBOperand` | `0x0024CC8A` | `D8 71 18` | `Hook` |
| `ChartEffectFrameCOperand` | `0x0024CCBE` | `D8 72 18` | `Hook` |
| `ChartEffectFrameDOperand` | `0x0024D836` | `D8 70 18` | `Hook` |
| `FixedVisualFrameOperand` | `0x00250AD5` | `D8 71 18` | `NonCtuneOutOfScope` |
| `GameplayCountdownAssetFrame` | `0x00249A9C` | `89 48 08` | `Hook` |
| `PlayerPositionInitA` | `0x00263240` | `89 84 91 54 1D 00 00` | `Hook` |
| `PlayerPositionInitB` | `0x002632B2` | `89 84 8A 54 1D 00 00` | `Hook` |
| `PlayerPositionInitC` | `0x0026359B` | `89 84 8A 54 1D 00 00` | `Hook` |
| `PlayerPositionInitD` | `0x00263615` | `89 84 8A 54 1D 00 00` | `Hook` |
| `PlayerPositionAssetFrame` | `0x0024EF43` | `2B 84 8A 54 1D 00 00` | `Hook` |
| `PlayerPositionDenominatorA` | `0x0024F76D` | `DB 80 C4 00 00 00` | `Hook` |
| `PlayerPositionDenominatorB` | `0x0024FD40` | `DB 80 C4 00 00 00` | `Hook` |
| `EffectFlowItemFrame` | `0x001F0310` | `89 42 08` | `Hook` |
| `EffectTutorialElapsed` | `0x00249593` | `89 95 74 FF FF FF` | `Hook` |
| `EffectChartPreRollDuration` | `0x0024A934` | `89 45 9C` | `Hook` |
| `EffectPlayerModuloDividend` | `0x0025072E` | `F7 F9` | `Hook` |

Add these 33 evidence-only rows:

| Stable site(s) | Disposition | Evidence |
|---|---|---|
| `0x001F0D04`, `0x001F1E2A`, `0x001F34B0` | `ResetOrConstant` | Engine reset helpers write frame zero. |
| `0x0024067C`, `0x0024094C`, `0x00240CE9`, `0x002412C0`, `0x0024669B`, `0x0024CCE1` | `ResetOrConstant` | Registration/reset branches or negative-frame clamp write zero. |
| `0x00244BDE`, `0x00244D4E`, `0x00244E3E`, `0x00244F2E`, `0x0024501E` | `AlreadyAuthoredNormalized` | Authored definition length multiplied by normalized progress. |
| `0x0024B680`, `0x0024BB6A`, `0x0024BCC6`, `0x0024BF9C`, `0x0024C935`, `0x0024CD12`, `0x0024D871` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| `0x001F3266` | `ChildInherited` | Child frame derives from the already-authored parent through `sub_5F17A0`. |
| `0x0024A574` | `NonCtuneOutOfScope` | Third dword of a 12-byte vector copied to a call stack. |
| `0x0024C487`, `0x0024C4C5`, `0x0024CA7D`, `0x0024CABB`, `0x0024D3E0`, `0x0024D41E` | `NonCtuneOutOfScope` | Chart 3D vector copies, not effect-frame stores. |
| `0x00250926`, `0x00250A8D`, `0x00250BBB`, `0x00250C8D` | `NonCtuneOutOfScope` | Player-position 3D vector copies, not effect-frame stores. |

The resulting timing-site summary is exact:

```text
timing_sites=67
registration_sites=34
duration_queries=9
hook_contracts=34
manager_gated=1
already_authored=12
reset_or_constant=9
child_inherited=1
non_ctune_out_of_scope=12
```

- [ ] **Step 6: Merge the exact 46-contract plan**

Keep the first ten existing contracts (`MovieClipGoto` through
`AudioResyncPolicy`) in a pre-effect array. Keep `NavigatorAdvance` and
`OuterFrame` in a post-effect array. Build one function-local static
`std::array<FramerateHookContract, 46>` by concatenating:

```text
10 pre-effect contracts
34 FramerateEffectHookContracts()
2 post-effect contracts
```

Use that same array in both `FramerateHookContracts()` and
`BuildFramerateHookPlan()`. Do not maintain two complete 46-entry authorities.
The final ordering must satisfy:

```cpp
static_assert(kMaximumFramerateHooks == 46);
Expect(FramerateHookContracts(true).size() == 46);
Expect(FramerateHookContracts(true)[45].id == FramerateHookId::OuterFrame);
Expect(FramerateHookContracts(false).size() == 1);
Expect(FramerateHookContracts(false)[0].id == FramerateHookId::OuterFrame);
Expect(BuildFramerateHookPlan(false, false).count == 1);
Expect(BuildFramerateHookPlan(false, true).count == 2);
Expect(BuildFramerateHookPlan(true, false).count == 45);
Expect(BuildFramerateHookPlan(true, true).count == 46);
```

Add `FramerateEffectTiming.cpp` to `gc_runtime_patches`.

- [ ] **Step 7: Build and run manifest/plan tests**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateEffectTimingTests FrameratePatchPlanTests
ctest --preset msvc32-debug -R "FramerateEffectTimingTests|FrameratePatchPlanTests"
```

Expected: both tests pass; the existing 17 direct writes are unchanged.

- [ ] **Step 8: Commit the manifest and contract refactor**

```powershell
git add -- `
  src/Patches/Framerate/FramerateEffectTiming.h `
  src/Patches/Framerate/FramerateEffectTiming.cpp `
  src/Patches/Framerate/FrameratePatchPlan.h `
  src/Patches/Framerate/FrameratePatchPlan.cpp `
  src/Patches/CMakeLists.txt `
  tests/Patches/Framerate/FramerateEffectTimingTests.cpp `
  tests/Patches/Framerate/FrameratePatchPlanTests.cpp `
  tests/Patches/CMakeLists.txt
git commit -m "refactor: make CTune effect timing exhaustive"
```

---

### Task 3: Add Pure Effect Register Transforms

**Files:**
- Modify: `src/Patches/Framerate/FramerateEffectTiming.h`
- Modify: `src/Patches/Framerate/FramerateEffectTiming.cpp`
- Modify: `tests/Patches/Framerate/FramerateEffectTimingTests.cpp`

**Interfaces:**

```cpp
enum class EffectTimingTransformError {
    ProfileConversion,
};

[[nodiscard]] std::expected<void, EffectTimingTransformError>
MapEffectFrameEaxToAuthored60(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, EffectTimingTransformError>
MapEffectFrameEdxToAuthored60(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;

[[nodiscard]] std::expected<void, EffectTimingTransformError>
ScaleEffectDurationEaxToTarget(
    safetyhook::Context& context,
    const FramerateProfile& profile) noexcept;
```

- [ ] **Step 1: Write failing transform tests**

Use zero-initialized `safetyhook::Context` values with distinct register,
`eip`, and `eflags` canaries. Required cases:

```cpp
const auto profile240 = FramerateProfile::Create(240).value();
const auto profile144 = FramerateProfile::Create(144).value();

context.eax = 8;
Expect(MapEffectFrameEaxToAuthored60(context, profile240));
Expect(context.eax == 2);

context.eax = 12;
Expect(MapEffectFrameEaxToAuthored60(context, profile144));
Expect(context.eax == 5);

context.edx = 8;
Expect(MapEffectFrameEdxToAuthored60(context, profile240));
Expect(context.edx == 2);

context.eax = 25;
Expect(ScaleEffectDurationEaxToTarget(context, profile240));
Expect(context.eax == 100);

context.eax = 25;
Expect(ScaleEffectDurationEaxToTarget(context, profile144));
Expect(context.eax == 60);
```

For all three functions:

- zero remains zero;
- `std::bit_cast<std::uint32_t>(-1)` and
  `std::bit_cast<std::uint32_t>(INT32_MIN)` remain bit-identical;
- every non-target register, `eip`, and `eflags` remains unchanged;
- native 60 FPS leaves positive inputs unchanged.

For `ScaleEffectDurationEaxToTarget`, use a 500 FPS profile and
`INT32_MAX` to prove that destination overflow returns
`EffectTimingTransformError::ProfileConversion` without partially changing
EAX. The two mapping helpers have no reachable overflow for a valid profile
and a signed-positive 32-bit input; do not add a fake injection seam merely to
force an impossible branch.

The same EAX mapping function must serve both `EffectFlowItemFrame` and
`EffectPlayerModuloDividend`; do not create site-specific arithmetic.

- [ ] **Step 2: Run the test and verify failure**

```powershell
cmake --build --preset msvc32-debug --target FramerateEffectTimingTests
```

Expected: compile failure for the three missing transform functions.

- [ ] **Step 3: Implement the three thin transforms**

The implementation must delegate exactly:

```cpp
const auto mapped =
    MapPositiveTargetFrameToAuthored60(profile, context.eax);
```

or its EDX equivalent, and:

```cpp
const auto scaled = ScalePositiveDuration(profile, context.eax);
```

Assign the register only after the expected value succeeds. Do not touch
`context.eip`: each SafetyHook mid-hook must allow the original store or
`idiv` instruction to execute normally after the callback.

- [ ] **Step 4: Run focused tests**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateEffectTimingTests FramerateAuthoredClockTests
ctest --preset msvc32-debug -R "FramerateEffectTimingTests|FramerateAuthoredClockTests"
```

Expected: both tests pass.

- [ ] **Step 5: Commit the transforms**

```powershell
git add -- `
  src/Patches/Framerate/FramerateEffectTiming.h `
  src/Patches/Framerate/FramerateEffectTiming.cpp `
  tests/Patches/Framerate/FramerateEffectTimingTests.cpp
git commit -m "test: define CTune effect boundary transforms"
```

---

### Task 4: Install the Four Missing Producer Hooks Transactionally

**Files:**
- Modify: `src/Patches/Framerate/FrameratePatchTransaction.h`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `tests/Patches/Framerate/FrameratePatchTransactionTests.cpp`
- Modify: `tests/Patches/Framerate/FramerateRuntimeTests.cpp`
- Modify: `tests/Patches/Framerate/FrameratePatchPlanTests.cpp`

**Runtime storage and counters:**

```cpp
// FramerateHookStorage
safetyhook::MidHook effect_flow_item_frame{};
safetyhook::MidHook effect_tutorial_elapsed{};
safetyhook::MidHook effect_chart_preroll_duration{};
safetyhook::MidHook effect_player_modulo_dividend{};

// FramerateRuntimeCounters
std::atomic_uint64_t effect_flow_item_mappings{0};
std::atomic_uint64_t effect_tutorial_elapsed_mappings{0};
std::atomic_uint64_t effect_chart_preroll_scalings{0};
std::atomic_uint64_t effect_player_modulo_mappings{0};
```

- [ ] **Step 1: Make capacity and runtime-binding tests fail**

Change test expectations to:

```cpp
static_assert(kMaximumFramerateHooks == 46);
```

Keep failure injection over the full
`[0, kMaximumFramerateHooks)` range. Add explicit checks that all four new IDs
return true from `FramerateHookHasRuntimeBinding`.

Run:

```powershell
cmake --build --preset msvc32-debug --target `
  FrameratePatchTransactionTests FramerateRuntimeTests FrameratePatchPlanTests
```

Expected: compile/assertion failure because capacity and bindings still cover
42 hooks.

- [ ] **Step 2: Raise the one authoritative hook capacity**

Change only:

```cpp
inline constexpr std::size_t kMaximumFramerateHooks = 46;
```

Do not change the 17-write capacity or pattern-byte capacity.

- [ ] **Step 3: Add exact storage and callback bindings**

Add four callback declarations and these `AssignHookCallbacks` cases:

```cpp
case FramerateHookId::EffectFlowItemFrame:
    operation.install = &InstallMidHook<
        &FramerateHookStorage::effect_flow_item_frame,
        HookEffectFlowItemFrame,
        0x001F0310>;
    operation.reset = &ResetOwnedHook<
        &FramerateHookStorage::effect_flow_item_frame>;
    break;
case FramerateHookId::EffectTutorialElapsed:
    operation.install = &InstallMidHook<
        &FramerateHookStorage::effect_tutorial_elapsed,
        HookEffectTutorialElapsed,
        0x00249593>;
    operation.reset = &ResetOwnedHook<
        &FramerateHookStorage::effect_tutorial_elapsed>;
    break;
case FramerateHookId::EffectChartPreRollDuration:
    operation.install = &InstallMidHook<
        &FramerateHookStorage::effect_chart_preroll_duration,
        HookEffectChartPreRollDuration,
        0x0024A934>;
    operation.reset = &ResetOwnedHook<
        &FramerateHookStorage::effect_chart_preroll_duration>;
    break;
case FramerateHookId::EffectPlayerModuloDividend:
    operation.install = &InstallMidHook<
        &FramerateHookStorage::effect_player_modulo_dividend,
        HookEffectPlayerModuloDividend,
        0x0025072E>;
    operation.reset = &ResetOwnedHook<
        &FramerateHookStorage::effect_player_modulo_dividend>;
    break;
```

- [ ] **Step 4: Implement the four callbacks**

Each callback delegates to one pure transform, publishes one exact fatal
detail on failure, and increments only its own counter on success:

| Callback | Transform | Fatal detail |
|---|---|---|
| `HookEffectFlowItemFrame` | `MapEffectFrameEaxToAuthored60` | `effect flow-item authored-frame mapping` |
| `HookEffectTutorialElapsed` | `MapEffectFrameEdxToAuthored60` | `effect tutorial elapsed authored-frame mapping` |
| `HookEffectChartPreRollDuration` | `ScaleEffectDurationEaxToTarget` | `effect chart pre-roll duration scaling` |
| `HookEffectPlayerModuloDividend` | `MapEffectFrameEaxToAuthored60` | `effect player modulo-dividend authored-frame mapping` |

Do not advance `eip` in any new callback:

- `0x001F0310` must still execute `mov [edx+8], eax`;
- `0x00249593` must still execute `mov [ebp-0x8C], edx`;
- `0x0024A934` must still execute `mov [ebp-0x64], eax`;
- `0x0025072E` must still execute `idiv ecx`.

At `0x0025072E`, the preceding original `cdq` already produced the correct
sign-extension in EDX. Positive mapped values remain positive, and
nonpositive sentinels are unchanged, so do not rewrite EDX.

- [ ] **Step 5: Run transaction and runtime tests**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateEffectTimingTests `
  FrameratePatchPlanTests `
  FrameratePatchTransactionTests `
  FramerateRuntimeTests
ctest --preset msvc32-debug -R `
  "FramerateEffectTimingTests|FrameratePatchPlanTests|FrameratePatchTransactionTests|FramerateRuntimeTests"
```

Expected:

- every one of 46 hook-install failure positions rolls back all installed hooks
  in reverse order;
- over-capacity 47-hook plans fail before descriptor access;
- all 46 transformed contracts have runtime bindings;
- native plan contains `OuterFrame`, preserves optional WASAPI resync selection,
  and contains no effect-timing hook;
- no existing hook RVA or expected bytes changed.

- [ ] **Step 6: Commit runtime integration**

```powershell
git add -- `
  src/Patches/Framerate/FrameratePatchTransaction.h `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FrameratePatchTransactionTests.cpp `
  tests/Patches/Framerate/FramerateRuntimeTests.cpp `
  tests/Patches/Framerate/FrameratePatchPlanTests.cpp
git commit -m "fix: normalize all CTune effect producers"
```

---

### Task 5: Publish Manifest and Per-Site Diagnostics

**Files:**
- Modify: `src/Patches/Framerate/FramerateDiagnostics.h`
- Modify: `src/Patches/Framerate/FramerateDiagnostics.cpp`
- Modify: `src/Patches/Framerate/FrameratePatch.cpp`
- Modify: `tests/Patches/Framerate/FramerateDiagnosticsTests.cpp`

**Interfaces:**

```cpp
struct FramerateEffectRuntimeStats {
    std::uint64_t flow_item_mappings{};
    std::uint64_t tutorial_elapsed_mappings{};
    std::uint64_t chart_preroll_scalings{};
    std::uint64_t player_modulo_mappings{};
};

[[nodiscard]] std::string FormatFramerateEffectRuntimeStats(
    const FramerateEffectRuntimeStats& stats);
```

Add this field to `FramerateStartupPatchSummary`:

```cpp
EffectTimingManifestSummary effect_timing{};
```

- [ ] **Step 1: Write failing startup and formatter tests**

For a transformed 144 FPS summary, assert the startup line contains:

```text
effect_timing=producer_boundary
effect_manifest_rows=67
effect_registration_sites=34
effect_duration_queries=9
effect_hooks=34
effect_manager_gated=1
effect_already_authored=12
effect_reset_or_constant=9
effect_child_inherited=1
effect_non_ctune_out_of_scope=12
```

For native 60 FPS, assert:

```text
effect_timing=native_bypass
```

The counts may still be printed at 60 FPS as static coverage facts, but no
transformed effect hook may be installed.

Test:

```cpp
Expect(
    FormatFramerateEffectRuntimeStats({1, 2, 3, 4}) ==
        " effect_flow_item=1"
        " effect_tutorial_elapsed=2"
        " effect_chart_preroll=3"
        " effect_player_modulo=4");
```

- [ ] **Step 2: Run and verify failure**

```powershell
cmake --build --preset msvc32-debug --target FramerateDiagnosticsTests
```

Expected: compile/assertion failure for the missing manifest field and
formatter.

- [ ] **Step 3: Implement startup and runtime formatting**

Pass `SummarizeEffectTimingManifest()` when constructing
`FramerateStartupPatchSummary` in `FrameratePatchInit`.

Append the formatter result to the existing five-second `runtime_stats` line
using relaxed loads from the four counters. Do not emit a per-frame or
per-effect-instance log.

Keep all existing startup and runtime fields intact.

- [ ] **Step 4: Run diagnostics and runtime tests**

```powershell
cmake --build --preset msvc32-debug --target `
  FramerateDiagnosticsTests FramerateRuntimeTests
ctest --preset msvc32-debug -R `
  "FramerateDiagnosticsTests|FramerateRuntimeTests"
```

Expected: both tests pass and fatal publication remains one-shot.

- [ ] **Step 5: Commit diagnostics**

```powershell
git add -- `
  src/Patches/Framerate/FramerateDiagnostics.h `
  src/Patches/Framerate/FramerateDiagnostics.cpp `
  src/Patches/Framerate/FrameratePatch.cpp `
  tests/Patches/Framerate/FramerateDiagnosticsTests.cpp `
  tests/Patches/Framerate/FramerateRuntimeTests.cpp
git commit -m "feat: report CTune effect timing coverage"
```

---

### Task 6: Commit the IDA-Backed Producer Proof

**Files:**
- Create: `docs/reverse-engineering/ctune-effect-producer-manifest.md`

- [ ] **Step 1: Revalidate the executable and four new byte signatures**

Run from `H:\IDACLI`:

```powershell
$env:IDA_CLI_DAEMON_DIR='C:\Users\10614\.ida-cli\daemons'
@'
from ida_cli.agent_bridge import AgentSession

expected = {
    0x5F0310: bytes.fromhex("89 42 08"),
    0x649593: bytes.fromhex("89 95 74 FF FF FF"),
    0x64A934: bytes.fromhex("89 45 9C"),
    0x65072E: bytes.fromhex("F7 F9"),
}
with AgentSession.start(
    r"H:\gc\game471.exe.i64",
    daemon=True,
    require_ida=True,
    request_timeout_s=120,
) as ida:
    ida.probe_backend(require_ida=True)
    code = (
        "import ida_bytes, idautils\n"
        f"expected = {[(ea, len(raw)) for ea, raw in expected.items()]!r}\n"
        "__result__ = {\n"
        "  'bytes': {hex(ea): ida_bytes.get_bytes(ea, size).hex() "
        "for ea, size in expected},\n"
        "  'registrations': sorted(hex(x.frm) "
        "for x in idautils.XrefsTo(0x5F07A0)),\n"
        "  'durations': sorted(hex(x.frm) "
        "for x in idautils.XrefsTo(0x5F0450)),\n"
        "}\n"
    )
    result = ida.result(
        code,
        request_id="ctune.effect.final-proof",
        timeout_s=120,
    )
    for ea, raw in expected.items():
        assert bytes.fromhex(result["bytes"][hex(ea)]) == raw
    assert len(result["registrations"]) == 34
    assert len(result["durations"]) == 9
    print(result)
'@ | python -
```

Expected: all four byte assertions pass, registration count is 34, and
duration-query count is 9.

- [ ] **Step 2: Write the durable producer report**

The document must contain:

- database path, image base, executable SHA-256, and analysis date;
- the exact 34 registration RVAs from Task 2;
- the exact nine duration-query RVAs from Task 2;
- the 67 timing-site rows and dispositions from Task 2;
- all 34 effect hook IDs/RVAs/expected bytes;
- the four new data-flow proofs:
  - EAX target frame to authored frame before `0x5F0310`;
  - EDX target elapsed to authored elapsed before `0x649593`;
  - EAX authored duration to target duration before `0x64A934`;
  - EAX target frame to authored frame before `idiv ecx` at `0x65072E`;
- the five reviewed suspicious stores
  `0x64A574`, `0x650926`, `0x650A8D`, `0x650BBB`, `0x650C8D`
  as 12-byte vector copies, not CTune frame writers;
- the six chart vector-copy sites
  `0x64C487`, `0x64C4C5`, `0x64CA7D`, `0x64CABB`,
  `0x64D3E0`, `0x64D41E`;
- proof that `sub_5F1F70` consumes `effect + 0x08`, applies speed, and derives
  child frames from the already-authored parent;
- proof that flag `0x4000` effects bypass the ordinary manager advance;
- a cross-link to the generated asset catalog and the tutorial
  definition-61-through-69 / texture-slot-13 canary.

The report must label static proof as complete and gameplay acceptance as
pending.

- [ ] **Step 3: Check source and report agree**

Run:

```powershell
rg -n "0x001F0310|0x00249593|0x0024A934|0x0025072E" `
  src/Patches/Framerate `
  tests/Patches/Framerate `
  docs/reverse-engineering/ctune-effect-producer-manifest.md
rg -n "T[B]D|TO[D]O|place[h]older|unknown dis[p]osition" `
  docs/reverse-engineering/ctune-effect-producer-manifest.md
git diff --check
```

Expected:

- all four RVAs occur in source, tests, and the report;
- the unresolved-marker scan returns no matches;
- `git diff --check` succeeds.

- [ ] **Step 4: Commit static proof**

```powershell
git add -- docs/reverse-engineering/ctune-effect-producer-manifest.md
git commit -m "docs: prove CTune effect producer coverage"
```

---

### Task 7: Run the Complete Static Verification Gate

**Files:**
- Modify only if a test exposes a defect in files owned by Tasks 1-6.

- [ ] **Step 1: Run Python verification**

```powershell
python -m unittest tools.analysis.tests.test_ctune_effect_catalog -v
python tools/analysis/ctune_effect_catalog.py `
  --root 'H:\gc\data\effect\game' `
  --output 'docs\reverse-engineering\ctune-effect-asset-catalog.md'
git diff --exit-code -- docs/reverse-engineering/ctune-effect-asset-catalog.md
```

Expected: tests pass and regeneration is byte-stable.

- [ ] **Step 2: Configure and build both supported presets**

Run from a shell with the x86 MSVC environment available:

```powershell
cmake --preset msvc32-debug
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug

cmake --preset msvc32-release
cmake --build --preset msvc32-release
ctest --preset msvc32-release
```

Expected: both full builds and both full CTest suites pass.

- [ ] **Step 3: Run structural closeout checks**

```powershell
rg -n "kMaximumFramerateHooks = 46" src tests
rg -n "transformed_hooks.size\\(\\) == 46|OuterFrame.*45" tests/Patches/Framerate
rg -n "EffectFlowItemFrame|EffectTutorialElapsed|EffectChartPreRollDuration|EffectPlayerModuloDividend" `
  src/Patches/Framerate tests/Patches/Framerate
rg -n "T[B]D|TO[D]O|place[h]older|unknown dis[p]osition" `
  docs/superpowers/plans/2026-07-24-complete-ctune-effect-timing-fix.md `
  docs/reverse-engineering/ctune-effect-*.md
git diff --check
git status --short
```

Expected:

- one authoritative capacity value of 46;
- exact plan tests for 46 contracts and `OuterFrame` index 45;
- every new ID appears in manifest, plan, binding, and tests;
- no unfinished markers;
- no uncommitted implementation or evidence changes.

- [ ] **Step 4: Record the static verification result**

If verification fails, return to the owning task, apply and commit the narrow
fix there, then rerun this complete gate. Do not create an empty closeout
commit. Report a clean result as:

```text
implementation and static verification complete; gameplay acceptance pending
```

Never report the effects as fixed in game before Task 8 is completed by the
operator.

---

### Task 8: Perform the Operator Runtime Acceptance Matrix

**Files:**
- Create when testing begins:
  `docs/reverse-engineering/ctune-effect-runtime-acceptance.md`

This is a user/operator checkpoint. It is not authorized by merely executing
Tasks 1-7.

- [ ] **Step 1: Prepare each target safely**

For each target FPS `60`, `120`, `144`, and `240`:

- use the existing live target-FPS control rather than editing
  `H:\gc\data\system.cfg`;
- make the external cap equal the selected target;
- confirm the startup log reports the selected target;
- at 60 FPS confirm `effect_timing=native_bypass`, no effect-timing hooks, and
  one or two total hooks according to optional WASAPI resync inclusion;
- above 60 FPS confirm `effect_timing=producer_boundary`, the expected
  45/46 total transaction count according to WASAPI resync inclusion, and no
  preflight/fatal error.

- [ ] **Step 2: Exercise the complete scenario matrix**

At every target, observe:

| Scenario | Required evidence |
|---|---|
| Tutorial prompts using definitions 61-69 / `img_big13` family | Same apparent duration and progression as 60 FPS; tutorial counter increases above 60 FPS. |
| Ordinary player hit/impact effect from the `0x65072E` modulo path | Same loop speed as 60 FPS; player-modulo counter increases above 60 FPS. |
| GREAT and GOOD judgement effects | No accelerated frame or shortened lifetime. |
| Gameplay countdown effect | No accelerated asset playback or premature disappearance. |
| Main/direct effect paths | Flow/direct counters or authored-operand totals increase; no 2x/4x speed. |
| Chart/gimmick effects, including pre-roll | Same wall-clock lifetime; pre-roll counter increases in a chart that reaches the path. |
| Remote/player-position visuals | No regression, freeze, duplicate, or early removal. |
| Child/nested effects | Children retain the parent's authored cadence and are not slowed by double mapping. |
| Input and chart play | No new input loss, chart desync, audio cursor regression, or stage-render regression. |

Use the five-second aggregate statistics to prove that each intended new site
actually executed. Do not infer coverage only from visual similarity.

- [ ] **Step 3: Record results without overclaiming**

Create the runtime acceptance document with one row per
`target × scenario`, including:

```text
date/time
loader commit
game executable SHA-256
target FPS and measured FPS
scenario
relevant counter delta
visual result
operator notes
```

If any target/scenario fails, leave the result as failed/pending and return to
systematic diagnosis. Do not weaken the static manifest, remove a guard, or
declare partial gameplay success to close the task.

- [ ] **Step 4: Commit accepted runtime evidence**

Only after the user confirms the complete matrix:

```powershell
git add -- docs/reverse-engineering/ctune-effect-runtime-acceptance.md
git commit -m "test: accept CTune effect timing in game"
```

At that point—and only then—the work may be described as fully fixed and
gameplay accepted.
