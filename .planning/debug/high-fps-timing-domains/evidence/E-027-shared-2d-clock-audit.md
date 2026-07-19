# E-027: shared 2D clock and callback audit

Date: 2026-07-20

IDA backend: existing daemon for `H:\gc\game471.exe.i64`, `idalib`

## MovieClip automatic-advance path

- `Anim::DrawTraverse` has vtable `0x006BB74C` and is constructed once for
  `CMovieManager` by `0x004D0450` through `0x004327D0`.
- Its render entry `0x004CE270` traverses the root instance. The MovieClip
  visitor `0x004CEC70` checks `DrawTraverse+0xFC`; the constructor initializes
  that mode to `1`.
- In mode `1`, each traversal calls MovieClip vslot `+0x134`, the forward
  wrapper `0x004D1580`. That wrapper dispatches to vslot `+0x150`, the
  one-timeline-frame primitive at `0x004DF940`.
- The primitive occurs in five related vtables: `Anim::IInstanceHasChild`, its
  `RefCountImpl`, the Movie `SPtrGroup` adaptor, `Anim::Movie`, and
  `Anim::MovieClipInstance`. These are an inheritance/adaptor chain for the
  same MovieClip contract, not five independently timed menu systems.
- Explicit goto-frame traversal reaches the same primitive while executing
  frame actions. The loader's goto-depth guard is therefore still necessary
  if automatic advancement is gated.

This proves that `0x004DF940` is the shared automatic authored-frame sink. It
does not prove that every high-level owner reaching the shared DrawTraverse is
called at the same runtime cadence. A callee-wide gate therefore still needs
caller/owner runtime coverage before it can be considered harmless to every
2D scene.

## Global elapsed-delta consumer inventory

- `GC120FPS_GetGlobalFrameTimer` is `0x00404580`.
- `GC120FPS_GlobalFrameDeltaSeconds` is `0x006350C0` and returns
  `CAppTimer+0x18`.
- The current IDB has 41 direct function-level consumers of that delta.
- The 2D/sequence subset contains 21 unique IFBL opcode-1 callbacks that
  consume elapsed seconds, including the card callbacks `0x005A5E80` and
  `0x005A5F90` through helper `0x005A37D0`.
- Representative direct consumers are `0x005A6F90`, `0x005A8390`,
  `0x005AEF00`, `0x005B4590`, `0x005B4820`, `0x005BB0E0`, `0x005BB9C0`,
  `0x005C1700`, `0x005C55C0`, `0x005C6650`, `0x00601BE0`, `0x00601FB0`,
  `0x00604F80`, `0x00607860`, `0x006082D0`, `0x00608830`, `0x0060B100`,
  `0x0060B610`, and `0x0060DFA0`.
- Their elapsed-time operations include countdown subtraction and clamp,
  `0.5 * delta` fades, song-selection easing, and transition timers. Several
  callbacks also sample input or call the frame-counted `0x00659110` menu
  repeat helper in the same invocation.
- `CSeqTaskBase` state `3` services these through the common IFBL state update.
  Opcode `1` retries a false callback on the next interpreter update; it does
  not impose an authored-frame delay of its own.

## Safety consequence

The card callback is not an isolated timer mechanism. Scaling the global
delta, throttling opcode `1`, or gating a complete `CSeqTask` would change all
of the elapsed-time callbacks above, including song-selection easing and
other transitions. Conversely, changing only MovieClip automatic advance
cannot repair a native countdown callback's consumed elapsed time.

No production timing change is justified by this static inventory alone. The
deployed card probe must first establish callback calls per second, cumulative
consumed delta per wall second, and the timer decrement. If a shared service
lane proves mismatched, the correction must be located at its clock boundary
and validated against this complete callback set rather than special-casing
the visible card scene.
