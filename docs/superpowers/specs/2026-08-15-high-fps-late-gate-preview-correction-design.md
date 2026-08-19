> **ARCHIVED FAILED CORRECTION — NOT AUTHORITATIVE.** It addressed one symptom
> inside an architecture that still failed.

# High-FPS Late-Gate Preview Correction

**Date:** 2026-08-15
**Status:** Approved for inline implementation
**Binary evidence target:** `H:\gc\game471.exe.i64`

> **Runtime supersession:**
> [High-FPS One-Shot Input Lifetime Correction](2026-08-15-high-fps-one-shot-input-lifetime-correction-design.md)
> supersedes this document's handler-result ownership, separate free-tap
> lifetime, and physical-grade-retiming clauses. Its type-aware non-consuming
> late-gate preview and native-order audit remain authoritative.

## Scope and supersession

This document corrects only the late-gate edge-selection portion of
`2026-08-15-high-fps-input-judgement-transactions-design.md`. Its transition
journal, immutable transaction, original-forgiveness, Switch-compatibility,
physical-grade, duration, lifecycle, capacity, and 60 FPS no-op contracts
remain authoritative.

The user approved the selected correction after the 240 FPS runtime log showed
that a press intended for one note could immediately consume the following
note. Implementation remains inline in the current worktree and must be
deployed directly after complete verification, without creating a backup.

## Runtime proof and root cause

The 2026-08-15 21:25 run validated the external cap at 240.021 FPS. Its final
input summary reported 206 captured and drained transitions, 4,795/4,795
completed transactions, no transport/history eviction, no diagnostic
overwrite, and no transaction, callback, or invariant anomaly.

The signed records nevertheless contained four characteristic wrong-note
associations:

| Physical press | Inferred intended grid target | Assigned target | Assigned signed error |
|---:|---:|---:|---:|
| 6,675 ms | 6,653 ms | 6,783 ms | -108 ms |
| 9,315 ms | 9,261 ms | 9,391 ms | -76 ms |
| 13,319 ms | 13,305 ms | 13,435 ms | -116 ms |
| 15,158 ms | 15,131 ms | 15,261 ms | -103 ms |

The inferred targets are the missing midpoint of repeated 130/131 ms chart
spacing. Each press is plausibly late for that midpoint by 14-54 ms but is
recorded against the following target. No judged single-note record reused a
physical sequence, so this is one edge consuming the wrong note, not one edge
being committed twice.

Live decompilation through the existing IDA-CLI daemon establishes the cause:

- NORMAL-family handler `0x5D1D50` calls late gate `0x5D0BE0` at
  `0x5D1E41`, then pressed wrapper `0x659640` at `0x5D1EC0`.
- FLICK `0x5D3320` calls the late gate at `0x5D33F1`, then direction matcher
  `0x5D2E50` at `0x5D3425`/`0x5D3448`.
- HOLD `0x5D41B0` calls the late gate at `0x5D42BA`, then the pressed wrapper
  at `0x5D4325`.
- SLIDE HOLD `0x5D35C0` calls the late gate at `0x5D369F`, then the direction
  matcher at `0x5D36D3`/`0x5D36F6`.
- SCRATCH `0x5D3C60` and BEAT `0x5D3920` are the opposite: their pressed
  queries occur before their late gates at `0x5D3E23` and `0x5D3A5D`.

`JudgementInputTransaction::BeginNote` clears `selected_edge_`.
`selected_edge_` is populated only when a pressed query is accepted or a
direction match completes. Consequently, a pre-input late gate always sees no
edge and returns recognition time. If that recognition time has crossed the
current note's late boundary, native code marks the note late without querying
its input. The immutable pending edge is still unused, so the core dispatcher
can offer it to the following already-early-eligible note in the same
judgement transaction.

The existing unit test masked this defect by calling `ProbePressed` and
`AcceptPressed` before `SelectLateGateTime`, reversing the NORMAL handler's
native call order.

## Considered corrections

### Selected: type-aware non-consuming preview

At `BeginNote`, derive at most one candidate that could satisfy that active
note's input primitive. Store it separately from the accepted edge. A late gate
uses an already-selected edge when native code queried input first; otherwise
it uses the preview. Actual input queries, successful association, consumption,
and physical grading continue to use `selected_edge_` only.

This is the smallest correction that makes the existing architecture match
native ordering without changing chart time, note windows, or handlers.

### Rejected: treat any pending edge as a rescue

An unrelated booster, wrong direction, or incomplete chord could suppress a
native miss. That weakens judgement semantics and violates the locked Switch
and original-forgiveness rules.

### Rejected: detour every complete note handler

Reordering native handler operations would require several larger ABI hooks,
duplicate native side effects, and materially increase crash risk. The
transaction already has the immutable state needed for a narrow preview.

## Corrected transaction contract

### Separate preview and accepted association

The transaction owns two distinct optional edges:

- `late_gate_preview_edge_`: a non-consuming, type-aware prediction available
  before native input calls;
- `selected_edge_`: the edge actually selected by accepted pressed or
  direction queries.

The preview may only affect `SelectLateGateTime`. It must never:

- consume or coalesce an edge;
- populate the transaction commit;
- change `SelectGradeArgument`;
- turn a failed handler into a successful handler result;
- replace a native held/history-only acceptance timestamp.

`SelectLateGateTime` uses `selected_edge_` first, preserving SCRATCH and BEAT's
native query-before-gate order. If no edge has yet been selected, it may use the
preview. Both paths apply only the rounded QPC delta to the already-adjusted
recognition argument.

### Button preview

For lane 0 the requested button is logical input 4; for lane 1 it is input 9.
Arcade mode considers only that button. Switch mode mirrors the existing query
order exactly:

1. real button;
2. same-booster directions 0,1,2,3 for button 4 or 5,6,7,8 for button 9;
3. first eligible source wins.

This preserves the rule that every newly pressed same-booster direction may be
a button edge while retaining the existing deterministic alias priority.

### Direction preview

For a FLICK or SLIDE HOLD head, select the latest pending direction edge on the
active lane's booster. Merge every same-transition cohort component into the
immutable current held snapshot, normalize the resulting direction with the
validated native table, and require it to match one of the descriptor's three
accepted directions. Switch diagonal matching continues to accept either
adjacent cardinal. A held/history match without a new qualifying edge creates
no preview.

### Failure and lifecycle behavior

If the eventual handler rejects the input, no note consumption is committed.
Epoch reset, expiry, overflow, native fallback, callback exception handling,
and fixed-capacity behavior remain unchanged. At target FPS 60 the high-FPS
transaction never activates, so this correction is a behavioral no-op.

## Complete note-type and free-tap review

This matrix records every dispatcher ID independently; shared wrappers are
listed as evidence but are not used as a substitute for row coverage.

| ID | Native path and input order | Corrected late-gate treatment | Grade/lifecycle treatment |
|---:|---|---|---|
| 0 NONE | Dispatcher default; no input or late gate | No preview | Native lifecycle only |
| 1 NORMAL | `0x5D1FA0 -> 0x5D1D50`; late gate before button 4/9 | Button preview | Accepted new edge may physically grade; lifecycle stays recognition-time |
| 2 FLICK | `0x5D3320`; late gate before direction matcher | Direction preview | Accepted completion edge may physically grade; history-only stays recognition-time |
| 3 HOLD | `0x5D41B0`; head late gate before button, body held query | Button preview for head; body has no late-gate call | Duration and release remain recognition-time |
| 4 SCRATCH | `0x5D3C60`; four direction pressed queries before late gate | Use actually selected pre-gate edge; no preview | Scratch duration remains recognition-time |
| 5 BEAT | `0x5D3920`; button pressed query before late gate | Use actually selected pre-gate edge; no preview | Repeat cadence and duration remain recognition-time |
| 6 MERRY GO ROUND | `0x5D5660 -> 0x5D1D50`; late gate before offset button | Button preview | Physical delta composes with native segment adjustment |
| 7 HIDDEN | NORMAL-family handler | Button preview | Same as NORMAL |
| 8 HIDDEN2 | NORMAL-family handler | Button preview | Same as NORMAL |
| 9 CRITICAL | `0x5D1F70 -> 0x5D1D50`; per-lane late gate before button | Per-lane button preview from one immutable snapshot | Paired aggregation remains native; accepted completion edge only |
| 10 SLIDE HOLD | `0x5D35C0`; head late gate before direction matcher, then held continuation | Direction preview for head; continuation has no late-gate call | Duration and continuation remain recognition-time |
| 11 SLIDE COUNTER | Dispatcher default; no independent input query | No preview | Native lifecycle marker |
| 12 TURN | Dispatcher default; no independent input query | No preview | Native lifecycle marker |
| 13 SPIN | Dispatcher default; no independent input query | No preview | Native lifecycle marker |
| 14 FINISH | Dispatcher default; no independent input query | No preview | Native lifecycle marker |
| 15 DUAL HOLD | `0x5D5540 -> 0x5D41B0`; per-lane HOLD head/body | Per-lane button preview inherited only by the head | Paired aggregation and duration remain recognition-time |
| - Free tap | `0x5D2040`; buttons 4/9 after note processing; no late gate | No preview; separate one-shot free-tap view | Native suppression and selected hit sound remain authoritative |

## Diagnostics and verification

Existing bounded note diagnostics gain `late_gate_delta_ms`. It records the
time adjustment used by the active note's late gate and remains zero when no
late gate correction occurs. It does not add a new unbounded log source.

Automated verification must reproduce native ordering and cover:

- late gate before pressed query for IDs 1,3,6,7,8,9,15;
- late gate before direction matching for IDs 2 and 10;
- pressed query before late gate for IDs 4 and 5;
- no preview for IDs 0 and 11-14 and for free tap;
- Arcade real-button isolation;
- Switch real-button-first alias order and same-booster isolation;
- exact and adjacent-cardinal direction acceptance;
- wrong-direction, wrong-lane, history-only, and expired-edge rejection;
- preview not grading or consuming before actual handler acceptance;
- a successfully consumed current-note edge being unavailable to the next
  note in the same immutable transaction;
- CRITICAL and DUAL HOLD lane independence;
- existing duration, free-tap, and 60 FPS no-op contracts.

The focused tests must first fail against the current implementation. The full
x86 Debug and Release preset graphs must then pass before direct deployment.
Runtime acceptance remains a new 240 FPS play test: the corrected log should
show small nonzero `late_gate_delta_ms` on rescued current notes and should no
longer show one-beat-early associations caused by the same failure pattern.
