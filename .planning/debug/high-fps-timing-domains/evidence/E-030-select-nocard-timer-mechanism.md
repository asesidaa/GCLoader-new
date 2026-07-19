# E-030: SELECT_NOCARD timer and callback mechanism

## Descriptor opcode and input callback

The mandatory login decision belongs to the `SELECT_NOCARD*` descriptor
family initialized by `sub_69D700`. These descriptors use IFBL opcode
`0x27` (decimal 39), not opcode 1. The opcode-1 diagnostic therefore could
never observe this page.

`SELECT_NOCARD_INIT` binds `sub_5A4540`. This callback reads the current IFBL
frame index and handles only decision input:

- frame 1 tests action 14 and selects the decision/cancel outcome;
- frame 2 tests action 10 and moves the selection when it is not already set;
- frame 3 tests action 11 and moves the selection when it is set.

The action dispatch reaches the input object's virtual method at `+0x18` via
`sub_6597F0`. `sub_5A4540` does not own or decrement the visible countdown.

## Countdown ownership

The countdown callback in the same `CStartTask` login/read-card sequence is
`sub_5A3AC0`. Each invocation:

1. obtains the shared timer object at `unk_82FECC`;
2. calls `sub_5A37D0`, which reads the current global `CAppTimer` delta and
   passes it to `sub_5A35E0`;
3. subtracts that elapsed delta from the remaining seconds, clamps at zero,
   and updates the visible timer widget;
4. returns `sub_5A3530(timer)`, which is true when the timer has expired.

`sub_5A4B60` initializes this login/read-card flow with
`sub_5A36D0(timer, 30.9, -1)` and resets the IFBL frame index to zero. The
countdown is therefore authored as an absolute 30.9-second duration. It is
not a 30.9-frame or integer-tick wait.

## IFBL control flow

In `sub_6304B0`, opcode `0x27` invokes its callback directly. A false result
advances to the next 88-byte descriptor; a true result resolves the matching
label through `sub_630060`. This is conditional label branching, distinct
from opcode 1 callback-retry behavior.

`CStartTask::Update` at `sub_5A40A0` initializes the sequence when needed and
then calls `CSeqTaskBase_UpdateStateMachine` on every invocation. The next
required trace is therefore the scheduler cadence at the call boundary for
`CStartTask::Update`, not the global opcode-1 dispatcher.

## Proven boundary and remaining question

The timer consumes the native global elapsed delta only when this CStartTask
sequence callback is serviced. The reported approximately 4x slowdown at
240 FPS is consistent with a 60-services-per-second / 240-Hz-delta mismatch,
but that cadence mismatch remains a hypothesis until the `CStartTask` task
scheduler path is statically traced. No global IFBL or global-delta correction
is justified by this evidence.
