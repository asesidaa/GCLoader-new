# Configuration Reflection and Enum Mapping Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make reflect-cpp authoritative for declared configuration-enum membership, remove repeated hand-maintained value lists, and make every remaining bidirectional enum/token mapping come from one table.

**Architecture:** `gc_config` owns a small reflect-cpp-backed declared-enum helper used at the parser/compiler boundary. Domain, protocol, presentation, diagnostic, vendor, and hot-path labels remain explicit in their owning modules. An explicit mapping that needs both directions uses one constexpr row table, never two independently maintained switches.

**Tech Stack:** C++23, reflect-cpp v0.25.0, toml++, `std::array`, `std::span`, CMake/Ninja/MSVC, production config parser/compiler tests.

**Spec:** `docs/superpowers/specs/2026-09-05-loader-codebase-cleanup-design.md`

## Global Constraints

- Complete Plans 01 through 06h first.
- Only `gc_config` may depend directly on reflect-cpp. Do not add reflect-cpp
  includes or link dependencies to Logging, Audio, Input, Widescreen, GUI, or
  protocol targets merely to remove a local switch.
- `rfl::string_to_enum` success is not proof of declared membership in
  reflect-cpp v0.25.0: numeric strings are converted to the underlying enum
  value. Every accepted config value must also appear in
  `rfl::get_enumerator_array<Enum>()`.
- Keep explicit mappings when the external token is not exactly the C++
  enumerator name or when the mapping defines a stable external contract.
- Preserve strict config parsing, field paths, error codes, error ordering,
  complete error collection, serialization, migration, and ConfigGUI draft
  behavior.
- Test through production parser/compiler/codec interfaces. Do not add source-
  grep tests or a test-local copy of the enum lists.

---

## Task 1: Classify every enum conversion and membership check

**Files:**

- Modify: `docs/architecture/loader-cleanup-baseline.md`
- Read: `src/Config/ConfigCompiler.cpp`
- Read: `src/Config/ConfigDocument.*`
- Read: `src/Config/InputRflParsers.h`
- Read: `src/Input/Types/PhysicalKey.*`
- Read: `src/Audio/Asio/AsioProbeProtocol.*`
- Read: `src/Logging/LoggingSettings.h`
- Read: `src/Audio/AudioSettings.h`
- Read: `src/Patches/WindowedWidescreen/WindowedWidescreenSettings.h`
- Read: `tools/ConfigGUI/**`

- [ ] **Step 1: Inventory both directions and validators**

Search enum declarations, `switch`-based names, string comparisons returning
enums, `static_cast` decoding, reflected config fields, and manual membership
checks. For each row record enum, owner, direction, external contract, caller,
allocation constraint, and disposition.

```powershell
rg -n 'enum class|enum_to_string|string_to_enum|get_enumerator_array|FromString|FromName|ToString|Parse[A-Za-z]*|Name\(' src tools
rg -n 'static_cast<[^>]+>\(|case [A-Za-z_:]+:.*return "' src tools
```

- [ ] **Step 2: Require these three reflected membership replacements**

The current compiler has hand-maintained declared-value chains for:

```text
logging.level                         LoaderLogLevel
experimental.widescreen_hud_placement GameplayHudPlacement
experimental.audio_backend            AudioBackend
```

Mark all three `replace_with_reflection`. Do not leave Widescreen as a third
special case.

- [ ] **Step 3: Preserve semantic subsets as semantic subsets**

Keep checks such as XInput button/axis/trigger ranges, cardinal versus axis
directions, binding-field compatibility, ASIO SDK sample-type values, wire
protocol enum ranges, stable diagnostic stage names, GUI presentation labels,
and Win32/vendor names explicit. They do not mean "is this a declared C++
enumerator?" and reflection would erase their domain meaning.

- [ ] **Step 4: Identify actual bidirectional token pairs**

Require the physical-key prefix codec (`sc`, `e0`, `e1` versus
`ScanCodePrefix`) to use one explicit table. Add any other pair only if the
inventory proves both production directions currently exist. Do not create a
generic mapping framework for one-way label functions.

---

## Task 2: Add the declared-enum helper inside `gc_config`

**Files:**

- Create: `src/Config/DeclaredEnum.h`
- Modify: `src/Config/ConfigCompiler.cpp`
- Modify: `src/Config/CMakeLists.txt`

**Interface:**

```cpp
namespace gc::config {

template <class Enum>
[[nodiscard]] constexpr auto DeclaredEnumValues() noexcept
{
    return rfl::get_enumerator_array<Enum>();
}

template <class Enum>
[[nodiscard]] constexpr bool IsDeclaredEnumValue(Enum value) noexcept
{
    for (const auto& [name, candidate] :
         DeclaredEnumValues<Enum>())
    {
        static_cast<void>(name);
        if (candidate == value)
        {
            return true;
        }
    }
    return false;
}

} // namespace gc::config
```

- [ ] **Step 1: Make membership derive only from reflected rows**

`IsDeclaredEnumValue` must enumerate the compile-time `(name, value)` array.
It must not assume contiguous underlying values, min/max ranges, zero origin,
or that the last enumerator is a sentinel.

- [ ] **Step 2: Validate parser results instead of adding an unused parser**

toml++/reflect-cpp continues to perform the document's syntax conversion.
`ConfigCompiler` must validate every resulting schema enum with
`IsDeclaredEnumValue`, so values produced from numeric strings such as `"1"`
or `"99"` cannot cross the semantic boundary unless that underlying value is
actually declared. If a future config-owned exact-name parser is needed, it
must additionally compare the supplied token to the reflected row name; do
not add that API before a production caller exists.

- [ ] **Step 3: Do not use `rfl::enum_to_string` on invalid values**

Where config-owned code needs an exact enumerator name, first require
`IsDeclaredEnumValue`, then use `rfl::enum_to_string`. Do not allow its numeric
fallback for an undeclared value to become a serialized external token.

---

## Task 3: Replace the three compiler lists

**Files:**

- Modify: `src/Config/ConfigCompiler.cpp`

- [ ] **Step 1: Replace loader-log membership**

Replace the `Info`/`Debug`/`Verbose` chain with
`IsDeclaredEnumValue(log_level)`. Preserve path `logging.level`, error code
`unsupported_value`, message `unsupported loader log level`, and its current
position in the aggregate error list.

- [ ] **Step 2: Replace Widescreen placement membership**

Replace the `left`/`center`/`right` chain with
`IsDeclaredEnumValue(widescreen_hud_placement)`. Preserve the field path,
error code, message, ordering, and the later width/height/dependency behavior.

- [ ] **Step 3: Replace audio-backend membership**

Set `audio_backend_valid = IsDeclaredEnumValue(audio_backend)`. Preserve the
field path, code, message, conditional WASAPI/ASIO leaf checks, absolute-
judgement dependency checks, and construction of the selected concrete audio
settings.

- [ ] **Step 4: Leave domain subset predicates explicit**

Do not convert `IsXInputButton`, `IsXInputAxis`, `IsXInputTrigger`,
`IsAxisDirection`, `IsCardinalDirection`, or `IsXInputType` to declared-enum
checks. Their subsets are behavioral contracts.

---

## Task 4: Lock down the numeric-string edge case through production config

**Files:**

- Modify: `tests/Config/ConfigContractTests.cpp`

- [ ] **Step 1: Add a small distributed-config mutation helper**

Read the real distributed `config.toml` and replace exactly one quoted enum
token per case. Do not copy the full config fixture. Assert that each mutated
document parses through `ParseConfigDocument`; this captures reflect-cpp
v0.25.0 accepting numeric strings at the syntax layer.

- [ ] **Step 2: Require semantic rejection of undeclared numeric values**

Exercise at least:

```text
level = '99'                       -> logging.level / unsupported_value
widescreen_hud_placement = '99'    -> experimental.widescreen_hud_placement / unsupported_value
audio_backend = '99'               -> experimental.audio_backend / unsupported_value
```

Call `ConfigCompiler::Compile` and assert the real structured path/code. Do
not assert implementation helper names or the full human-readable message.

- [ ] **Step 3: Cover declared values without copying their list**

In the test, iterate `gc::config::DeclaredEnumValues<Enum>()` for the three
schema enums and prove every reflected value passes the production compiler
after providing any required dependent fields (for example, a nonempty ASIO
driver name). The test does not include reflect-cpp directly. Its independent
invariant is agreement between config-owned reflection metadata and compiler
acceptance, not a duplicated enum table.

---

## Task 5: Consolidate the physical-key prefix codec

**Files:**

- Modify: `src/Input/Types/PhysicalKey.cpp`
- Modify: `tests/Config/ConfigContractTests.cpp`

**Internal row:**

```cpp
struct ScanCodePrefixToken final {
    ScanCodePrefix value;
    std::string_view token;
};

inline constexpr std::array<ScanCodePrefixToken, 3> kPrefixTokens{{
    {ScanCodePrefix::None, "sc"},
    {ScanCodePrefix::E0, "e0"},
    {ScanCodePrefix::E1, "e1"},
}};
```

- [ ] **Step 1: Drive parsing and formatting from one table**

`ParsePhysicalKey` looks up the first two characters in `kPrefixTokens`;
`FormatPhysicalKey` looks up the enum in the same rows. Preserve lowercase
tokens, exact seven-character shape, four hexadecimal digits, nonzero make
code, and existing error text.

- [ ] **Step 2: Preserve the invalid-format fallback explicitly**

The current formatter treats an unknown `ScanCodePrefix` as `sc`. Keep that
observable behavior during this cleanup, but express it as a named fallback
after a failed table lookup rather than accidental switch initialization.
Config parsing itself remains strict and never creates that value.

- [ ] **Step 3: Test the production codec round trip**

For `sc`, `e0`, and `e1`, parse a nonzero key, format it, and require exact
round-trip text. Retain invalid prefix, malformed hex, and zero make-code
rejection through `ParsePhysicalKey`.

---

## Task 6: Remove only genuinely redundant mapping code

**Files:**

- Modify: files marked `one_table` in
  `docs/architecture/loader-cleanup-baseline.md`
- Modify: `docs/architecture/loader-cleanup-baseline.md`

- [ ] **Step 1: Consolidate any additional proven pair**

For each inventory row with both production directions and a non-reflected
external token, create one constexpr table in the owning module and make both
lookups consume it. Preserve case sensitivity, aliases, unknown handling,
wire values, allocation behavior, and public signatures.

- [ ] **Step 2: Record retained explicit mappings**

Document why `LoaderLogLevelName`, `AudioBackendName`,
`GameplayHudPlacementName`, hook/install stage names, ASIO protocol numeric
validation, GUI labels, and hardware/vendor strings remain explicit. These
are stable diagnostic/presentation/domain contracts or allocation-sensitive
paths, not duplicate config serializers.

- [ ] **Step 3: Do not invent a shared enum utility target**

If the audit finds no additional bidirectional pair, record that result and
make no speculative abstraction. One-way functions stay beside their owner.

---

## Task 7: Verify reflection ownership and commit

- [ ] **Step 1: Run focused production tests**

```powershell
cmake --build --preset msvc32-debug --target gc_config_contract_tests
ctest --preset msvc32-debug -R ConfigContract --output-on-failure
cmake --build --preset msvc32-release --target gc_config_contract_tests
ctest --preset msvc32-release -R ConfigContract --output-on-failure
```

- [ ] **Step 2: Run the full static suite**

```powershell
cmake --build --preset msvc32-debug
ctest --preset msvc32-debug -j 4
cmake --build --preset msvc32-release
ctest --preset msvc32-release -j 4
git diff --check
git status --short --branch
```

- [ ] **Step 3: Audit dependency ownership**

```powershell
rg -n 'reflectcpp_SOURCE_DIR|target_link_libraries\([^\)]*reflectcpp|<rfl' src tools -g CMakeLists.txt -g '*.h' -g '*.cpp'
rg -n 'log_level !=|widescreen_hud_placement !=|audio_backend ==.*\|\|' src\Config\ConfigCompiler.cpp
```

Expected: direct reflect-cpp ownership remains in `gc_config`; reflected
types may appear through its public schema, but unrelated targets do not link
reflect-cpp directly. The three manual membership chains are gone.

- [ ] **Step 4: Commit**

```powershell
git add -- src\Config src\Input\Types\PhysicalKey.cpp tests\Config\ConfigContractTests.cpp docs\architecture\loader-cleanup-baseline.md
git commit -m "Use reflection for configuration enum membership"
```
