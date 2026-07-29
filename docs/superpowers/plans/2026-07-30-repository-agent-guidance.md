# Repository Agent Guidance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make root `AGENTS.md` the single repository-wide source for GCLoader agent instructions and domain context.

**Architecture:** Consolidate the existing domain glossary with repository boundaries, build commands, correctness rules, test policy, and runtime-validation expectations in one Codex-discovered root file. Remove the superseded `CONTEXT.md` and update its two live documentation references.

**Tech Stack:** Markdown, Codex `AGENTS.md` instruction discovery, Git, CMake/MSVC repository conventions

## Global Constraints

- `H:\gc\artifacts\GCLoader` is the source repository; `H:\gc` is the runtime/deployment tree.
- Do not add nested instruction files or provider-specific workflow rules.
- Preserve every durable definition and ambiguity currently documented in `CONTEXT.md`.
- Do not change C++, CMake, tests, configuration, or runtime artifacts.
- Do not claim in-game validation from documentation or automated checks.

---

### Task 1: Consolidate repository instructions and domain context

**Files:**

- Create: `AGENTS.md`
- Delete: `CONTEXT.md`
- Modify: `docs/superpowers/specs/2026-07-17-gcloader-codebase-architecture-modernization-design.md`

**Interfaces:**

- Consumes: Root repository structure, `CMakePresets.json`, `cmake/ProjectOptions.cmake`, and all durable terminology from `CONTEXT.md`
- Produces: One root `AGENTS.md` automatically discovered by Codex

- [x] **Step 1: Record the pre-migration instruction references**

Run:

```powershell
rg -n "CONTEXT\.md|GCLoader Domain Context|AGENTS\.md" --glob "!build-*/**" --glob "!.git/**" .
```

Expected: `CONTEXT.md` plus two references in the architecture modernization design; no existing root `AGENTS.md`.

- [x] **Step 2: Create the canonical root `AGENTS.md`**

Create these sections in this order:

1. `# GCLoader Repository Guide`
2. `## Repository Scope`
3. `## Build and Verification`
4. `## Architecture and Correctness`
5. `## Test Policy`
6. `## Runtime Evidence and Acceptance`
7. `## Domain Terminology`
8. `## Flagged Ambiguities`

The file must state these exact operational requirements:

- Configure, build, and test through `msvc32-debug` and `msvc32-release`.
- The project is Windows x86, C++23, and uses the static MSVC runtime.
- Runtime patches require current binary or IDB evidence, named RVAs,
  expected original bytes, checked access, and transactional rollback.
- Preserve exported iDmac ABI names, ordinals, x86 calling conventions, and
  initialized output behavior.
- Runtime `config.toml` is strict and complete; ConfigGUI defaults do not make
  missing runtime fields optional.
- Tests must prove observable behavior, invariants, boundaries, protocols,
  concurrency, failure handling, or rollback.
- Do not add source-text grep tests, copied production tables or byte
  manifests, tests of test-only helpers, or tests solely to satisfy workflow.
- Separate build/static evidence from user-performed in-game acceptance.
- Do not deploy to or mutate `H:\gc` unless the task explicitly includes it.

Move all definitions and avoid-notes for Game process, NESYS process, iDmac,
FastIO, Booster input, RFID/JVS, Test-mode storage, NESYS, Runtime patch, and
Exclusive audio pipeline from `CONTEXT.md`. Preserve the three flagged
ambiguities about iDmac/DMA, NESYS process/Windows service, and FastIO
labels/booster directions.

- [x] **Step 3: Retire `CONTEXT.md` and update live references**

Delete `CONTEXT.md`.

In
`docs/superpowers/specs/2026-07-17-gcloader-codebase-architecture-modernization-design.md`:

- change the domain-naming reference from root `CONTEXT.md` to root
  `AGENTS.md`;
- change the target repository layout entry from `CONTEXT.md` to `AGENTS.md`.

- [x] **Step 4: Validate the consolidated instructions**

Run:

```powershell
rg -n "CONTEXT\.md|GCLoader Domain Context" --glob "!build-*/**" --glob "!.git/**" .
```

Expected: only historical statements in the new migration design and plan
describing the source file being retired; no active guidance reference.

Run:

```powershell
$env:PYTHONUTF8='1'
python 'C:\Users\10614\.codex\skills\migrate-to-codex\scripts\migrate-to-codex.py' --validate-target .
git diff --check
git status --short
```

Expected: validator passes, diff check is clean, and status contains only the
planned Markdown changes.

- [x] **Step 5: Review and commit the migration**

Review:

```powershell
git diff -- AGENTS.md CONTEXT.md docs/superpowers/specs/2026-07-17-gcloader-codebase-architecture-modernization-design.md
```

Confirm the glossary is complete, commands match `CMakePresets.json`, and no
rule claims automated gameplay proof.

Commit:

```powershell
git add -- AGENTS.md CONTEXT.md docs/superpowers/specs/2026-07-17-gcloader-codebase-architecture-modernization-design.md docs/superpowers/plans/2026-07-30-repository-agent-guidance.md
git commit -m "docs: consolidate repository agent guidance"
```
