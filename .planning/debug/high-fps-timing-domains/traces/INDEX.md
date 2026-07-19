# Trace index

| Trace | Entry/root | State being followed | Status |
|---|---|---|---|
| T-001 Master scheduling | Outer frame / scheduler | Render cadence, update cadence, engine delta sources | Static trace complete; runtime frequency capture pending |
| T-002 UI/movie | MovieClip and screen-task updates | Timeline position, transition duration, authored frames | Notice-transition mixed-clock failure proven; start2 card states mapped; card callback/delta runtime capture pending |
| T-003 Menu input | Physical edge to menu consumer | Press lifetime, held state, repeat counters | Static trace complete; exact-byte card-confirm callback cadence probe deployed |
| T-004 Gameplay judgement | Input and tune/audio clock | Note time, current time, hit-window comparisons | Static trace complete: judgement is native target-frame to absolute milliseconds |
| T-005 Gameplay visuals | Tune frame to effect/render state | Effect cadence, lifetime, blink, player position | Static trace complete for current hooks and direct `Tune+0x18` consumers; runtime cadence capture pending |
| T-006 Stage rendering | Stage update/draw paths | Mesh motion, clip-mask selection, interpolation | Static trace complete: absolute-ms transform path separated from authored clip-mask index |
