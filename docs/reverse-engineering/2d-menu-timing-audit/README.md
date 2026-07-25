# 2D Menu Animation Timing Audit

- Status: static audit complete; runtime probes and gameplay acceptance pending
- Audit date: 2026-07-25
- Scope: read-only completeness review of the existing high-FPS timing patches for all Flash-like 2D menu animation, including nested movie clips and tag-driven transitions.

## Fixed evidence baseline

- Runtime/deploy tree: `H:\gc`
- Source worktree: `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`
- Source branch/commit: `ctune-effect-timing` at `2354d0f`
- XFL/RVB/MTX corpus: `H:\gc\artifacts\2d_boost`
- Executable: `H:\gc\game471.exe`
- Executable SHA-256: `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`
- IDB: `H:\gc\game471.exe.i64`
- IDB SHA-256: `55D119762B0706549AB5AA9C7D5D2DDF3C902AE322462D025D570C8181C50C1F`
- IDA daemon directory: `C:\Users\10614\.ida-cli\daemons`
- Shared daemon target: `H:\gc\game471.exe.i64`
- Daemon policy: agents attach with `AgentSession.connect`; nobody shuts down, mutates, or saves the IDB during the audit.

The deployed DLL currently comes from this worktree, so this audit uses the worktree rather than `main` as its source baseline. The effect-timing branch changes several framerate files; its 2D behavior must therefore be audited as a complete current state, not inferred from the older mainline patch.

## Evidence streams and file ownership

| Stream | Owner | Durable findings file |
|---|---|---|
| XFL tag, label, main-timeline, and nested-timeline inventory | XFL analyst | `xfl-inventory.md` |
| Existing GCLoader 2D hooks, transforms, diagnostics, and tests | Patch analyst | `patch-inventory.md` |
| Binary 2D scheduler/update/transition paths and hook reachability | IDA analyst | `ida-trace.md` |
| Reconciled coverage matrix and final verdict | Root | `coverage-matrix.md` |

Each analyst records evidence after every substantial discovery, not only at task completion. Findings must distinguish:

- observed XFL/RVB facts;
- source-code assumptions;
- IDA-proven binary behavior;
- runtime evidence;
- inference or unresolved uncertainty.

## Historical constraints

- `Anim::DrawTraverse` belongs to the Flash-like 2D/RVB runtime; it must not be treated as a generic 3D animation fix.
- Earlier broad experiments doubled 2D speed and disturbed input. This audit makes no code changes and does not recommend a hook without tracing its clock domain and call reach.
- The previously stable baseline used a MovieClip timing gate plus optional news/notice gates. That is a starting point to audit, not proof of completeness.
- “All 2D menu animations” includes child movie clips, independent playheads, ActionScript-driven `gotoAndPlay`/`gotoAndStop`, label transitions, and assets outside the obvious attract loop.

## Running findings ledger

| Time | Owner | Finding | Evidence status |
|---|---|---|---|
| 2026-07-25 | Root | Runtime source baseline, binary hashes, corpus root, and one reusable IDA daemon were fixed before analysis. | Confirmed |
| 2026-07-25 | Root | Fresh 240 FPS runtime counters show both ordinary MovieClip and Navigator gates executing at the expected approximately 1:3 run/skip ratio; aggregate counters cannot identify assets, nesting, labels, or bypass callsites. | Confirmed but insufficient for completeness |
| 2026-07-25 | Root | Prior evidence identified ordinary main/nested MovieClip traversal at `0x004DF940` and the manual Navigator sink at `0x005B6310`; the current-IDB audit subsequently revalidated both paths. | Revalidated |
| 2026-07-25 | Root | Raw `menu`, `title`, and `news` folders contain static DDS/PNG resources. Static texture files have no timeline, but their native sprite-cell/transform consumers remain audit targets. | Confirmed file classification |
| 2026-07-25 | XFL analyst | Completed the 59-project corpus census: 57/59 projects have reachable multi-frame children, 55/59 have depth-2+ children, and authored scripts contain extensive self, child, root, parent, and grandparent playhead control. | Complete |
| 2026-07-25 | Patch analyst | Confirmed the deployed branch has the same 2D behavior as `main`; its four added CTune effect hooks do not expand menu MovieClip coverage. Existing tests and counters prove installation/arithmetic, not asset or callback-semantic completeness. | Complete |
| 2026-07-25 | IDA analyst | Revalidated ordinary root/nested MovieClip convergence on `0x004DF940`, goto coverage through `0x004DEA30`, and the sole Navigator sink at `0x005B6310`. | Complete |
| 2026-07-25 | IDA analyst | Found a high-risk PreProcessor context collision at the universal MovieClip sink, uncovered Ranking and HitChart per-draw counters, and a partially uncovered mixed clock in UnlockReward. | Complete |
| 2026-07-25 | Root | Reconciled verdict: current 2D menu timing coverage is not complete. News/Notice should remain native; targeted runtime probes are required before patch design. See `coverage-matrix.md`. | Complete |

## Final audit artifacts

- `coverage-matrix.md` — reconciled coverage verdict, ranked findings, search
  boundary, and runtime probe requirements.
- `ida-trace.md` — current-IDB call graph, hook reach, alternate-path sweep, and
  address-level evidence.
- `patch-inventory.md` — deployed source behavior, tests, diagnostics, and
  branch comparison.
- `xfl-inventory.md` — complete 59-project structural and transition inventory.

No production code, tests, executable, deployed DLL, or IDB was changed by this
review. The single shared IDA daemon remains running for the follow-up probes.
