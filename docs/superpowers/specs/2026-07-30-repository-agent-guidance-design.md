# GCLoader Repository Agent Guidance Design

Date: 2026-07-30

Status: Approved

## Context

The repository currently stores its project terminology in `CONTEXT.md`, but
Codex discovers repository instructions through `AGENTS.md`. The context file
therefore depends on an agent knowing to find and read a non-standard document.
It also contains only terminology, leaving build commands, repository
boundaries, binary-patch expectations, test policy, and runtime-acceptance
boundaries implicit.

## Decision

Create one root `AGENTS.md` as the canonical repository-wide instruction and
context file. Move the complete durable content of `CONTEXT.md` into it, delete
`CONTEXT.md`, and update the two live references in the architecture
modernization design.

Do not add nested instruction files or provider-specific workflow rules. The
root guidance applies uniformly to `src/`, `tools/`, `tests/`, and `docs/`.

## AGENTS.md Contents

The file will cover:

1. Repository purpose and the boundary between the source repository at
   `H:\gc\artifacts\GCLoader` and the runtime/deployment tree at `H:\gc`.
2. The x86 MSVC, CMake preset, build, and CTest commands used by the project.
3. Architecture and correctness rules for process roles, feature ownership,
   configuration, ABI seams, and guarded executable-image patches.
4. Test policy that favors observable behavior, invariants, boundaries,
   protocol contracts, concurrency, failure handling, and transactional
   rollback.
5. Explicit rejection of low-signal tests that grep source text, duplicate
   production literal tables or byte manifests, exercise only test helpers, or
   exist solely to satisfy a workflow requirement.
6. The complete domain glossary and flagged ambiguities currently stored in
   `CONTEXT.md`.
7. A clear separation between automated build/static evidence and user-performed
   in-game acceptance.

## Evidence and Runtime Rules

Runtime-patch work must use current executable or IDB evidence rather than
guessing from names or stale planning documents. Named RVAs, expected original
bytes, arithmetic and access checks, and transactional rollback remain feature
correctness requirements.

Changes to the source repository do not authorize deployment into `H:\gc`.
Runtime files, logs, and the active IDB may be inspected as evidence, but
deployment or mutation of the runtime tree requires explicit task scope.

## Validation

This is a documentation-only migration. Validation consists of:

- confirming `AGENTS.md` contains every durable rule and term from
  `CONTEXT.md`;
- confirming no live reference still targets the deleted file;
- running the Codex target validator;
- running `git diff --check`;
- reviewing the final diff and repository status.

A C++ rebuild or CTest run is not required because no build, source, test, or
runtime behavior changes.
