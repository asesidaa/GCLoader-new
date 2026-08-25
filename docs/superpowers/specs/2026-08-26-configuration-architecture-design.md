# Configuration Architecture Design

## Context

GCLoader currently uses `InputConfig` as four different things:

- the reflect-cpp representation of `config.toml`;
- ConfigGUI's mutable editor state;
- the source for semantic validation;
- the runtime configuration reached through the global `ConfigManager`.

Validation has consequently accumulated as hand-written, first-error checks
spread across the central config module and feature-specific helpers. Some leaf
rules are duplicated, cross-field rules are mixed with parsing, and runtime
features repeatedly query a global object whose storage also participates in
startup migration and persistence.

The pinned reflect-cpp version already provides declarative validators for
leaf constraints. It cannot, however, be placed directly around the editable
document: ConfigGUI must be able to hold a temporarily invalid draft, while
the runtime configuration must be valid and immutable for the whole process
launch.

This design separates those lifetimes and responsibilities without changing
the user-facing TOML layout.

## Goals

- Keep `config.toml` strict and complete, with required fields and no ignored
  extra fields.
- Let ConfigGUI hold and display temporarily invalid semantic values.
- Report every independently detectable semantic error in one pass.
- Use reflect-cpp validators for declarative leaf rules while retaining
  explicit project-owned cross-field rules.
- Compile raw document values into feature-owned runtime settings that cannot
  contain irrelevant conditional state.
- Read runtime configuration once per process launch and never hot-reload it.
- Restrict runtime persistence to recognized schema migrations and the two
  explicitly approved startup fallback repairs.
- Remove hidden runtime dependencies on the global `ConfigManager`.
- Preserve explicit game-process and NESYS-process ownership.

## Non-goals

- Redesigning the TOML tables, field names, or ConfigGUI layout.
- Adding defaults for missing current-schema fields.
- Adding runtime configuration watching or hot reload.
- Building a generic configuration framework or repair plug-in system.
- Changing ASIO, input, judgement, storage, NESYS, or runtime-patch policy
  beyond how those features receive validated settings.
- Changing the approved system-path or test-mode-storage fallback behavior.
- Deploying artifacts or claiming in-game acceptance as part of this local
  architecture refactor.

## Architectural split

```text
TOML text
    |
    v
ConfigDocument
    raw, mutable, reflect-cpp serializable
    |
    v
ConfigCompiler
    declarative leaf rules + project semantic rules
    |                         \
    |                          -> ordered ConfigErrors[]
    v
ValidatedConfig
    temporary grouping of feature-owned settings
    |
    v
startup composition root
    |
    +-> InputSettings owned by input runtime
    +-> AudioSettings owned by audio runtime
    +-> FramerateSettings owned by framerate patches
    +-> JudgementSettings owned by judgement runtime
    +-> NesysSettings owned by NESYS initialization
    +-> other feature-specific settings
```

### ConfigDocument

`ConfigDocument` replaces the serialized/editor role currently carried by
`InputConfig`. It contains the reflect-cpp field names, raw primitives, and
tagged optional fields necessary to round-trip the existing TOML format.

It deliberately does not promise semantic validity. ConfigGUI edits this type
directly, and a structurally valid document may remain semantically invalid
for any number of frames while the user edits it.

Document construction remains strict:

- TOML syntax must parse;
- the schema version and obsolete syntax policy must be satisfied;
- recognized migrations are applied explicitly;
- required fields must exist;
- field types must match;
- `rfl::NoExtraFields` rejects unknown fields.

There is no `DefaultIfMissing` compatibility path. ConfigGUI defaults are only
widget conveniences and never weaken document loading.

### ConfigCompiler

`ConfigCompiler` is a pure transformation from a complete `ConfigDocument` to
either a `ValidatedConfig` or a collection of semantic errors. It performs no
filesystem, registry, COM, ASIO, device, or process-role work.

Leaf constraints use reflect-cpp validator aliases, for example:

```cpp
using TargetFpsValue =
    rfl::Validator<int, rfl::Minimum<60>, rfl::Maximum<500>>;
```

The compiler uses the exception-contained result API rather than allowing a
validator exception to cross a runtime boundary. Reflect-cpp validator and
rename types remain implementation details of the configuration layer; they
are not exposed through feature APIs.

The reflect-cpp wrapper is not itself the runtime immutability boundary: the
pinned version exposes mutable access to its underlying value. Runtime
invariants instead come from compiler-controlled construction of project-owned
settings types and the absence of mutating runtime APIs.

Appropriate declarative leaf rules include:

- target FPS bounds;
- supported input polling rates;
- individual threshold bounds;
- registry numeric bounds;
- WASAPI and ASIO numeric bounds;
- physical-key validity through a custom rule.

Project semantic rules remain explicit when they depend on more than one
value, on a selected variant, or on project/operating-system semantics that a
generic leaf validator cannot express, including:

- release threshold being below press threshold;
- absolute-time judgement requiring an eligible audio backend;
- absolute-time judgement requiring 1000 Hz input polling;
- ASIO-only required fields;
- controller backend and binding compatibility;
- tagged button, axis, and hat requirements;
- UTF-8 path decoding and derived NESYS path validity.

Leaf rules run before dependent cross-field rules. A cross-field rule is
evaluated only when all participating leaves are valid, preventing cascaded
and misleading diagnostics.

### Runtime settings

Runtime settings types live beside their consuming feature. Feature modules do
not include `ConfigDocument` or depend on reflect-cpp serialization wrappers.
The dependency direction is:

```text
ConfigCompiler -> feature settings types
feature runtime -X-> ConfigDocument
```

Settings expose read-only runtime access and are created only through their
compiler-facing construction seam. They are not public raw aggregates that an
arbitrary runtime caller can assemble with unchecked values.

Runtime types compile conditional raw fields into concrete alternatives. Audio
is represented conceptually as:

```cpp
using AudioSettings = std::variant<
    DirectSoundSettings,
    WasapiExclusiveSettings,
    AsioSettings>;
```

An `AsioSettings` value therefore always contains its required driver, buffer,
and channel data. DirectSound cannot accidentally carry meaningful stale ASIO
state. Controller bindings similarly compile raw tagged fields into concrete
button, axis, or hat alternatives.

The temporary `ValidatedConfig` groups feature-owned settings for startup
sequencing. It is not a replacement global service locator and is not exposed
to hooks or callbacks.

## Error model

Syntax and structural failures occur before a trustworthy document exists and
remain document-load errors. Semantic compilation returns the full ordered
error collection:

```cpp
struct ConfigError {
    ConfigPath path;
    ConfigErrorCode code;
    std::string message;
    std::vector<ConfigPath> related_paths;
};
```

`ConfigPath` contains typed field and index segments and renders in familiar
form such as `controller.bindings[3].direction`. `ConfigErrorCode` is stable
for tests and GUI association; the message remains human-readable detail.
`related_paths` associates a cross-field failure with its other participating
controls without producing duplicate errors.

Errors follow deterministic document declaration and collection-index order.
Each failed invariant produces one primary error. The acceptance path has no
warning severity: semantic compilation either succeeds or returns errors.
Non-fatal UI advice, if ever needed, is a separate concept.

The boundaries retain distinct result types:

- document-load errors for I/O, syntax, schema, and strict shape;
- `ConfigErrors` for complete semantic failures;
- startup-preparation errors for filesystem and capability work;
- persistence errors for atomic replacement;
- GUI ASIO-preflight errors for the selected device environment.

Formatting those failures for a modal, log, or startup fatal happens only at
the outer UI or loader boundary. No exception may cross `DllMain`, an exported
iDmac function, a hook, COM, or an audio callback.

## ConfigGUI lifecycle

```text
read file
  -> parse syntax and apply recognized migrations
  -> strict ConfigDocument construction
  -> open editor
  -> compile continuously and display all semantic errors
  -> explicit Save
       -> compile successfully
       -> run ASIO capability preflight when ASIO is selected
       -> atomically write ConfigDocument
```

A syntactically and structurally valid but semantically invalid document opens
in ConfigGUI so the user can repair it. A missing required field, invalid type,
extra field, unsupported schema, or unrecognized obsolete form still blocks
opening because no complete editable document exists.

Save remains disabled while compilation fails. GUI widget restrictions may
prevent obvious bad input, but they are only a usability layer; the shared
compiler remains authoritative.

ASIO preflight stays outside semantic compilation because driver availability
and capabilities are environmental. Its bounded worker lifetime prevents the
GUI from hanging; elapsed time is not evidence of audio validity.

Recognized migrations make the draft dirty. ConfigGUI persists them only when
the user explicitly saves.

## Runtime startup lifecycle

Each process reads the file once and receives a fixed launch snapshot. Later
external edits affect only a future launch.

```text
read file
  -> parse syntax and apply recognized migrations in memory
  -> strict ConfigDocument construction
  -> semantic compilation
  -> game-only capability probes and fallback preparation
  -> build repaired candidate document when an approved repair applies
  -> compile the final candidate again
  -> game process atomically persists migrations/repairs once when needed
  -> freeze and distribute feature-owned runtime settings
```

The NESYS process applies recognized migrations in memory so it can initialize
from the same logical schema, but it does not race the game process to write
the shared file. The game-process startup owns migration and repair
persistence.

Ordinary runtime code cannot write configuration. Startup persistence is an
explicit exception restricted to:

1. recognized schema migrations;
2. system-path fallback correction;
3. forcing test-mode-storage redirect when native storage is unavailable.

These are named policies, not a general mutation or plug-in interface. Each
repair identifies the exact document fields it may change and carries a reason
for diagnostics. If several apply, their changes are combined into one
candidate and one atomic write.

The repaired candidate is compiled before it is written. A failed compilation
therefore cannot persist an invalid document. Required persistence failure is
fail-closed and prevents runtime settings from being published. The same
candidate that passed compilation and persistence becomes the immutable launch
snapshot.

Filesystem preparation may have non-rollbackable effects such as creating a
directory, but failure still prevents feature initialization. The existing
atomic temporary-file replacement continues to protect the prior config file.

## Runtime ownership and process roles

The global `ConfigManager` service-locator role is removed. Loading,
compilation, startup repair, persistence, and runtime ownership become separate
responsibilities.

After startup preparation, the composition root moves each settings value into
the feature that owns it:

```cpp
InitializeInput(InputSettings settings);
InitializeAudio(AudioSettings settings);
InstallFrameratePatches(FramerateSettings settings);
InitializeNesys(NesysSettings settings);
```

Long-lived feature objects, worker threads, hooks, and callbacks own the
strings, vectors, bindings, and other values they use. They must not store a
reference into `ValidatedConfig`, `PreparedProcessConfig`, or a stack-local
startup object. Once all settings have been transferred, the process-level
grouping may be destroyed.

Runtime results shared across features remain explicit dependencies rather
than hidden config queries. For example, framerate initialization may consume
an audio-hook capability result, but it does not query global audio settings.

The game process receives the complete set of game feature settings. The NESYS
process receives only logging and NESYS-process settings and must not initialize
game-only input, audio, RFID/storage, or gameplay patches.

## Component boundaries

The intended responsibilities are:

- `ConfigDocument`: TOML-shaped data, strict parsing, explicit migrations, and
  serialization.
- `ConfigCompiler`: pure leaf and semantic validation plus conversion to
  runtime settings.
- startup configuration orchestration: process-role loading, approved game
  probes, named repairs, final compilation, and publication.
- atomic document writer: the shared safe persistence primitive used by
  ConfigGUI explicit Save and approved game-startup persistence.
- feature settings: consumer-owned, reflect-cpp-free runtime values.

The startup repair code stays concrete. It does not introduce a generalized
repair registry solely to support tests.

## Verification strategy

Every test must protect a plausible behavioral or ownership regression.

### Parsing and compilation

- Parse the distributed `config.toml` through the production parser and
  compiler.
- Derive boundary cases by mutating that valid document rather than copying the
  full configuration into a second fixture.
- Verify that strict document loading succeeds for a semantically invalid
  document and that compilation then returns errors. This protects the
  ConfigGUI draft boundary from collapsing back into parse-and-first-error
  validation.
- Construct one document with several independent semantic violations and
  assert the complete ordered set of paths and stable codes. This catches a
  regression to first-error validation.
- Verify that dependent cross-field rules do not emit cascaded errors when a
  participating leaf is already invalid.
- Verify that selected audio and controller forms compile to the correct
  concrete variants and incompatible tagged fields are rejected.

### Startup repair transaction

Using the existing production transaction seam with injected filesystem and
write actions, verify that:

- no migration or approved repair produces no write;
- a recognized migration produces one write;
- simultaneous approved repairs produce one combined write;
- an invalid candidate is never written;
- persistence failure prevents configuration publication and preserves the
  prior file;
- the NESYS process does not persist or run game-only probes.

### Ownership

Where a production feature exposes an observable operation, initialize it with
moved settings, destroy the temporary process grouping, and exercise that
operation afterward. Such a test protects against borrowed string, vector, or
binding lifetimes. Do not create a test-only runtime abstraction merely to
claim this coverage.

### Exclusions

Do not add:

- source-text or regex tests asserting that a class or getter disappeared;
- one test per validator without a distinct failure mode;
- copied production constant/default tables maintained in lockstep;
- a test-local parser or compiler that mirrors production;
- an enormous duplicate of the distributed config file;
- deployment or gameplay claims based on unit tests.

Focused affected targets run during implementation. Completion requires the
complete affected x86 Debug and Release preset graphs, full CTest in both
configurations, CLion diagnostics after CMake reload when needed,
`git diff --check`, and a clean accounting of worktree changes. These are
local/static guarantees only; in-game behavior remains a separate acceptance
boundary.

## Implementation transition

The refactor should preserve behavior while moving one responsibility at a
time:

1. Introduce document, error, compiler, and feature settings types alongside
   the existing API.
2. Make ConfigGUI and focused config tests use the shared compiler and complete
   error collection.
3. Move feature call sites from global getters to owned settings in coherent
   feature batches.
4. Replace startup loading and the existing preparation transaction with the
   explicit migration/repair pipeline.
5. Remove `ConfigManager` only after all production consumers have explicit
   ownership.

At each stage, the existing strict document-shape behavior, atomic writer,
process-role policy, and approved fallback semantics remain authoritative.
Compatibility shims are temporary migration scaffolding and must not survive
the final implementation.
