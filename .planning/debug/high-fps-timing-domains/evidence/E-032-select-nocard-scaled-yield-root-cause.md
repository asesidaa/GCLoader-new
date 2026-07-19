# E-032: SELECT_NOCARD scaled one-update yield root cause

## Exact descriptor loop

The active `SELECT_NOCARD` loop in the `CStartTask` descriptor table is:

1. label `SELECT_NOCARD` (opcode `0x29` / decimal 41);
2. opcode `0x27` callback `sub_5A3AC0`, branching to
   `SELECT_NOCARD_TIMEOUT` when the absolute timer expires;
3. set IFBL frame index 1 and invoke `sub_5A4540` for the decision action;
4. set frame index 2 and invoke `sub_5A4540` for one navigation direction;
5. set frame index 3 and invoke `sub_5A4540` for the other navigation
   direction;
6. opcode `0x11` / decimal 17 with integer value 1;
7. unconditional branch back to `SELECT_NOCARD`.

The one-update wait descriptor begins at `0x0081CBE8`; its value 1 is stored
at `0x0081CBEC`. The following branch returns to `SELECT_NOCARD`.

## Loader-created mismatch

The current production `IfblWait` hook intercepts the interpreter store at
`0x006309D4` (RVA `0x002309D4`) and applies `ScalePositiveDuration` to every
opcode-`0x11` integer wait. At 240 FPS it changes this loop's value from 1 to
4.

While that four-update wait is active, neither `sub_5A3AC0` nor
`sub_5A4540` is invoked. On the next loop iteration:

- `sub_5A3AC0` subtracts only the current native-frame global delta, about
  1/240 second, rather than the four frames elapsed since its last call;
- `sub_5A4540` samples the one-native-update input edges only once per four
  updates.

The result is exactly the reported behavior at 240 FPS: the absolute
countdown progresses at approximately one quarter real time and decision
input is difficult to register.

## Required design boundary

This establishes the root cause for the mandatory login page. The production
fix must not scale cooperative one-update polling yields as authored visual
durations. Before changing the global rule, all opcode-`0x11` value-1 sites
must be classified so equivalent mixed timer/input loops are corrected
without changing genuine multi-frame authored waits.
