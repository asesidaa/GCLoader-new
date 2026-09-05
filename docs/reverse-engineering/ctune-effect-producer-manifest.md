# CTune Effect Producer Manifest

Static producer coverage is complete for the analyzed executable. Gameplay
acceptance is pending.

## Binary identity

| Property | Value |
|---|---|
| Analysis date | 2026-07-24 |
| IDA database | `H:\gc\game471.exe.i64` |
| Executable | `H:\gc\game471.exe` |
| Image base | `0x00400000` |
| Executable SHA-256 | `FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522` |
| IDA backend | `idalib`, database open and auto-analysis available |

The final daemon-backed check read these bytes directly from the IDB:

| VA | RVA | Bytes | Boundary |
|---:|---:|---|---|
| `0x005F0310` | `0x001F0310` | `89 42 08` | CTune flow-item frame store |
| `0x00649593` | `0x00249593` | `89 95 74 FF FF FF` | Tutorial shared elapsed store |
| `0x0064A934` | `0x0024A934` | `89 45 9C` | Chart pre-roll duration store |
| `0x0065072E` | `0x0025072E` | `F7 F9` | Player-effect signed modulo |

The same check found exactly 34 code xrefs to registration function
`sub_5F07A0` and exactly nine code xrefs to authored-duration query
`sub_5F0450`.

## Reproduction

Run this from `H:\IDACLI`; `AgentSession.start(..., daemon=True)` reuses the
daemon for the same normalized target path.

```python
from ida_cli.agent_bridge import AgentSession

expected = {
    0x5F0310: bytes.fromhex("89 42 08"),
    0x649593: bytes.fromhex("89 95 74 FF FF FF"),
    0x64A934: bytes.fromhex("89 45 9C"),
    0x65072E: bytes.fromhex("F7 F9"),
}

with AgentSession.start(
    r"H:\gc\game471.exe.i64",
    daemon=True,
    require_ida=True,
    request_timeout_s=120,
) as ida:
    ida.probe_backend(require_ida=True)
    result = ida.result(
        """
import ida_bytes, idautils
expected = [(0x5F0310, 3), (0x649593, 6),
            (0x64A934, 3), (0x65072E, 2)]
__result__ = {
    "bytes": {hex(ea): ida_bytes.get_bytes(ea, size).hex()
              for ea, size in expected},
    "registrations": sorted(hex(x.frm)
                            for x in idautils.XrefsTo(0x5F07A0)),
    "durations": sorted(hex(x.frm)
                        for x in idautils.XrefsTo(0x5F0450)),
}
""",
        request_id="ctune.effect.final-proof",
        timeout_s=120,
    )
    for ea, raw in expected.items():
        assert bytes.fromhex(result["bytes"][hex(ea)]) == raw
    assert len(result["registrations"]) == 34
    assert len(result["durations"]) == 9
```

## Registration census

The exact sorted registration RVA set is:

```text
0x001F02F5
0x00240674  0x00240941  0x00240CDE  0x002412B5
0x00244BC0  0x00244D30  0x00244E20  0x00244F10  0x00245000
0x00246517  0x00246693
0x00248F75  0x002491C9  0x002498E8  0x0024999C
0x00249A53  0x00249BEC
0x0024B61C  0x0024BB11  0x0024BC19  0x0024BF72
0x0024C56C  0x0024C5CA  0x0024C607  0x0024C8DC
0x0024CB4D  0x0024CBC0  0x0024CBFD
0x0024D710  0x0024D779  0x0024D7C4
0x0024EF82  0x00250689
```

The reaching producer families are:

- `0x001F02F5`: `sub_5F0220`, whose target-frame result crosses the
  authored-frame boundary at `0x001F0310`.
- `0x00240674`, `0x00240941`, `0x00240CDE`, `0x002412B5`: gameplay judge
  effect registrations followed by frame-zero stores.
- `0x00244BC0` through `0x00245000`: target-cue owners that store authored
  definition length multiplied by normalized progress.
- `0x00246517`, `0x00248F75`, `0x002491C9`, `0x00249BEC`: millisecond
  producers whose final x87 divisor is the authored `1000/60` operand.
- `0x00246693`: a frame-zero registration.
- `0x002498E8`, `0x0024999C`: the paired tutorial consumers of the shared
  value established at `0x00249593`.
- `0x00249A53`: the mapped gameplay countdown asset frame.
- `0x0024B61C` through `0x0024D7C4`: chart producers split between
  authored-ms and normalized-progress branches.
- `0x0024EF82`: player-position elapsed mapping.
- `0x00250689`: player-effect modulo loop whose dividend crosses at
  `0x0025072E`.

## Authored-duration query census

The exact sorted query RVA set is:

```text
0x00246463  0x0024647D
0x00248EA7  0x00248EBF  0x00249104
0x0024962C  0x00249653  0x00249790
0x0024A92F
```

The first two select GREAT/GOOD authored lengths, the next three select the
two main-effect lifetimes, the next three compare the shared tutorial elapsed
value with authored lengths, and the final query supplies the chart pre-roll
authored duration.

## Complete timing-site manifest

This is the durable 67-row disposition set mirrored by
`FramerateEffectTiming.cpp`. `TargetFrame` is the configured runtime cadence;
`Authored60Frame` is the asset clock.

| # | Stable ID | Boundary RVA | Domain | Disposition | Evidence |
|---:|---|---:|---|---|---|
| 1 | `hook.gameplay-effect-advance` | `0x00264E2D` | `TargetFrame -> Authored60Frame` | `ManagerGated` | Ordinary manager advance runs only on authored-60 boundaries. |
| 2 | `hook.effect-cadence-6` | `0x0024063B` | `TargetFrame -> Authored60Frame` | `Hook` | Period-6 cadence conversion. |
| 3 | `hook.effect-cadence-5` | `0x002408D7` | `TargetFrame -> Authored60Frame` | `Hook` | Period-5 cadence conversion. |
| 4 | `hook.effect-cadence-4` | `0x00240C9C` | `TargetFrame -> Authored60Frame` | `Hook` | Period-4 cadence conversion. |
| 5 | `hook.effect-cadence-16-a` | `0x00241213` | `TargetFrame -> Authored60Frame` | `Hook` | Period-16 cadence conversion A. |
| 6 | `hook.effect-cadence-16-b` | `0x0024122F` | `TargetFrame -> Authored60Frame` | `Hook` | Period-16 cadence conversion B. |
| 7 | `hook.effect-cadence-8` | `0x00241268` | `TargetFrame -> Authored60Frame` | `Hook` | Period-8 cadence conversion. |
| 8 | `hook.remote-cadence-a` | `0x002632DB` | `TargetFrame -> Authored60Frame` | `Hook` | Remote period-4 cadence A feeds CTune visuals. |
| 9 | `hook.remote-cadence-b` | `0x00263646` | `TargetFrame -> Authored60Frame` | `Hook` | Remote period-4 cadence B feeds CTune visuals. |
| 10 | `hook.gameplay-blink` | `0x0024A1B9` | `TargetFrame -> Authored60Frame` | `Hook` | Target frame maps before authored blink arithmetic. |
| 11 | `hook.great-good-lifetime-operand` | `0x002464A8` | `Milliseconds -> Authored60Frame` | `Hook` | Final x87 divisor uses authored frame milliseconds. |
| 12 | `hook.great-good-frame-operand` | `0x00246528` | `Milliseconds -> Authored60Frame` | `Hook` | Final x87 divisor uses authored frame milliseconds. |
| 13 | `hook.effect-lifetime-a-operand` | `0x00248F00` | `Milliseconds -> Authored60Frame` | `Hook` | Main effect A lifetime operand. |
| 14 | `hook.effect-frame-a-operand` | `0x00248F8C` | `Milliseconds -> Authored60Frame` | `Hook` | Main effect A frame operand. |
| 15 | `hook.effect-lifetime-b-operand` | `0x0024912B` | `Milliseconds -> Authored60Frame` | `Hook` | Main effect B lifetime operand. |
| 16 | `hook.effect-frame-b-operand` | `0x002491E0` | `Milliseconds -> Authored60Frame` | `Hook` | Main effect B frame operand. |
| 17 | `hook.direct-effect-frame-operand` | `0x00249C14` | `Milliseconds -> Authored60Frame` | `Hook` | Direct effect frame operand. |
| 18 | `hook.chart-effect-frame-a-operand` | `0x0024BC8B` | `Milliseconds -> Authored60Frame` | `Hook` | Chart effect frame operand A. |
| 19 | `hook.chart-effect-frame-b-operand` | `0x0024CC8A` | `Milliseconds -> Authored60Frame` | `Hook` | Chart effect frame operand B. |
| 20 | `hook.chart-effect-frame-c-operand` | `0x0024CCBE` | `Milliseconds -> Authored60Frame` | `Hook` | Chart effect frame operand C. |
| 21 | `hook.chart-effect-frame-d-operand` | `0x0024D836` | `Milliseconds -> Authored60Frame` | `Hook` | Chart effect frame operand D. |
| 22 | `hook.fixed-visual-frame-operand` | `0x00250AD5` | `Milliseconds -> NonCtuneData` | `NonCtuneOutOfScope` | Existing fixed-visual correction is retained but is not a CTune producer. |
| 23 | `hook.gameplay-countdown-asset-frame` | `0x00249A9C` | `TargetFrame -> Authored60Frame` | `Hook` | Countdown target frame maps before the asset-frame store. |
| 24 | `hook.player-position-init-a` | `0x00263240` | `Authored60Frame -> TargetFrame` | `Hook` | Authored duration A scales to target frames. |
| 25 | `hook.player-position-init-b` | `0x002632B2` | `Authored60Frame -> TargetFrame` | `Hook` | Authored duration B scales to target frames. |
| 26 | `hook.player-position-init-c` | `0x0026359B` | `Authored60Frame -> TargetFrame` | `Hook` | Authored duration C scales to target frames. |
| 27 | `hook.player-position-init-d` | `0x00263615` | `Authored60Frame -> TargetFrame` | `Hook` | Authored duration D scales to target frames. |
| 28 | `hook.player-position-asset-frame` | `0x0024EF43` | `TargetFrame -> Authored60Frame` | `Hook` | Elapsed target frames map to the asset clock. |
| 29 | `hook.player-position-denominator-a` | `0x0024F76D` | `Authored60Frame -> TargetFrame` | `Hook` | Duration denominator A scales for target-frame progress. |
| 30 | `hook.player-position-denominator-b` | `0x0024FD40` | `Authored60Frame -> TargetFrame` | `Hook` | Duration denominator B scales for target-frame progress. |
| 31 | `hook.effect-flow-item-frame` | `0x001F0310` | `TargetFrame -> Authored60Frame` | `Hook` | Runtime-frame division result crosses immediately before the CTune store. |
| 32 | `hook.effect-tutorial-elapsed` | `0x00249593` | `TargetFrame -> Authored60Frame` | `Hook` | One shared value feeds three duration comparisons and two frame stores. |
| 33 | `hook.effect-chart-pre-roll-duration` | `0x0024A934` | `Authored60Frame -> TargetFrame` | `Hook` | Authored length scales before a target-frame distance comparison. |
| 34 | `hook.effect-player-modulo-dividend` | `0x0025072E` | `TargetFrame -> Authored60Frame` | `Hook` | Target frame maps before division by authored definition length. |
| 35 | `site.001F0D04` | `0x001F0D04` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Engine reset helper writes zero. |
| 36 | `site.001F1E2A` | `0x001F1E2A` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Engine reset helper writes zero. |
| 37 | `site.001F34B0` | `0x001F34B0` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Engine reset helper writes zero. |
| 38 | `site.0024067C` | `0x0024067C` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Registration branch writes zero. |
| 39 | `site.0024094C` | `0x0024094C` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Registration branch writes zero. |
| 40 | `site.00240CE9` | `0x00240CE9` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Registration branch writes zero. |
| 41 | `site.002412C0` | `0x002412C0` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Registration branch writes zero. |
| 42 | `site.0024669B` | `0x0024669B` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Registration branch writes zero. |
| 43 | `site.0024CCE1` | `0x0024CCE1` | `ConstantOrSentinel -> Authored60Frame` | `ResetOrConstant` | Negative-frame clamp writes zero. |
| 44 | `site.00244BDE` | `0x00244BDE` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Definition length multiplied by normalized progress. |
| 45 | `site.00244D4E` | `0x00244D4E` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Definition length multiplied by normalized progress. |
| 46 | `site.00244E3E` | `0x00244E3E` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Definition length multiplied by normalized progress. |
| 47 | `site.00244F2E` | `0x00244F2E` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Definition length multiplied by normalized progress. |
| 48 | `site.0024501E` | `0x0024501E` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Definition length multiplied by normalized progress. |
| 49 | `site.0024B680` | `0x0024B680` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 50 | `site.0024BB6A` | `0x0024BB6A` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 51 | `site.0024BCC6` | `0x0024BCC6` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 52 | `site.0024BF9C` | `0x0024BF9C` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 53 | `site.0024C935` | `0x0024C935` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 54 | `site.0024CD12` | `0x0024CD12` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 55 | `site.0024D871` | `0x0024D871` | `NormalizedProgress -> Authored60Frame` | `AlreadyAuthoredNormalized` | Chart definition length multiplied by normalized progress. |
| 56 | `site.001F3266` | `0x001F3266` | `Authored60Frame -> Authored60Frame` | `ChildInherited` | Child frame derives from the authored parent through `sub_5F17A0`. |
| 57 | `site.0024A574` | `0x0024A574` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Third dword of a 12-byte vector copy. |
| 58 | `site.0024C487` | `0x0024C487` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Chart 3D vector copy. |
| 59 | `site.0024C4C5` | `0x0024C4C5` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Chart 3D vector copy. |
| 60 | `site.0024CA7D` | `0x0024CA7D` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Chart 3D vector copy. |
| 61 | `site.0024CABB` | `0x0024CABB` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Chart 3D vector copy. |
| 62 | `site.0024D3E0` | `0x0024D3E0` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Chart 3D vector copy. |
| 63 | `site.0024D41E` | `0x0024D41E` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Chart 3D vector copy. |
| 64 | `site.00250926` | `0x00250926` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Player-position 3D vector copy. |
| 65 | `site.00250A8D` | `0x00250A8D` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Player-position 3D vector copy. |
| 66 | `site.00250BBB` | `0x00250BBB` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Player-position 3D vector copy. |
| 67 | `site.00250C8D` | `0x00250C8D` | `NonCtuneData -> NonCtuneData` | `NonCtuneOutOfScope` | Player-position 3D vector copy. |

Derived totals:

```text
timing_sites=67
registration_sites=34
duration_queries=9
hook_contracts=34
manager_gated=1
already_authored=12
reset_or_constant=9
child_inherited=1
non_ctune_out_of_scope=12
```

## Effect hook contracts

These are the exact 34 effect-only contracts merged into the full
46-contract framerate plan:

| Hook ID | RVA | Expected bytes |
|---|---:|---|
| `GameplayEffectAdvance` | `0x00264E2D` | `E8 6E BA F8 FF` |
| `EffectCadence6` | `0x0024063B` | `85 D2` |
| `EffectCadence5` | `0x002408D7` | `85 D2` |
| `EffectCadence4` | `0x00240C9C` | `85 D2` |
| `EffectCadence16A` | `0x00241213` | `85 D2` |
| `EffectCadence16B` | `0x0024122F` | `81 E1 0F 00 00 80` |
| `EffectCadence8` | `0x00241268` | `85 C0` |
| `RemoteCadenceA` | `0x002632DB` | `85 D2` |
| `RemoteCadenceB` | `0x00263646` | `85 D2` |
| `GameplayBlink` | `0x0024A1B9` | `D1 F8` |
| `GreatGoodLifetimeOperand` | `0x002464A8` | `D8 48 18` |
| `GreatGoodFrameOperand` | `0x00246528` | `D8 71 18` |
| `EffectLifetimeAOperand` | `0x00248F00` | `D8 49 18` |
| `EffectFrameAOperand` | `0x00248F8C` | `D8 72 18` |
| `EffectLifetimeBOperand` | `0x0024912B` | `D8 49 18` |
| `EffectFrameBOperand` | `0x002491E0` | `D8 72 18` |
| `DirectEffectFrameOperand` | `0x00249C14` | `D8 72 18` |
| `ChartEffectFrameAOperand` | `0x0024BC8B` | `D8 71 18` |
| `ChartEffectFrameBOperand` | `0x0024CC8A` | `D8 71 18` |
| `ChartEffectFrameCOperand` | `0x0024CCBE` | `D8 72 18` |
| `ChartEffectFrameDOperand` | `0x0024D836` | `D8 70 18` |
| `FixedVisualFrameOperand` | `0x00250AD5` | `D8 71 18` |
| `GameplayCountdownAssetFrame` | `0x00249A9C` | `89 48 08` |
| `PlayerPositionInitA` | `0x00263240` | `89 84 91 54 1D 00 00` |
| `PlayerPositionInitB` | `0x002632B2` | `89 84 8A 54 1D 00 00` |
| `PlayerPositionInitC` | `0x0026359B` | `89 84 8A 54 1D 00 00` |
| `PlayerPositionInitD` | `0x00263615` | `89 84 8A 54 1D 00 00` |
| `PlayerPositionAssetFrame` | `0x0024EF43` | `2B 84 8A 54 1D 00 00` |
| `PlayerPositionDenominatorA` | `0x0024F76D` | `DB 80 C4 00 00 00` |
| `PlayerPositionDenominatorB` | `0x0024FD40` | `DB 80 C4 00 00 00` |
| `EffectFlowItemFrame` | `0x001F0310` | `89 42 08` |
| `EffectTutorialElapsed` | `0x00249593` | `89 95 74 FF FF FF` |
| `EffectChartPreRollDuration` | `0x0024A934` | `89 45 9C` |
| `EffectPlayerModuloDividend` | `0x0025072E` | `F7 F9` |

## Four completed producer-boundary proofs

### Flow-item EAX mapping

`sub_5F0220` subtracts the flow endpoints, divides by the runtime frame
milliseconds argument, converts the result to EAX, and immediately stores EAX
as the CTune frame:

```text
005F0300  fld     dword ptr [eax+4]
005F0303  fsub    dword ptr [ecx]
005F0305  fdiv    [ebp+arg_0]
005F0308  call    __ftol2_sse
005F030D  mov     edx, [ebp+var_C]
005F0310  mov     [edx+8], eax
```

The `EffectFlowItemFrame` mid-hook maps positive EAX from target frames to
authored-60 frames and then allows the original store to execute.

### Tutorial EDX mapping

The tutorial path constructs target elapsed in a shared local, loads it into
EDX, and stores EDX to `[ebp-0x8C]`:

```text
00649573  mov     edx, [ecx+0B8h]
00649579  sub     edx, 1
00649582  mov     ecx, [eax+10h]
00649585  sub     ecx, edx
00649587  mov     [ebp+var_1D8], ecx
0064958D  mov     edx, [ebp+var_1D8]
00649593  mov     [ebp+var_8C], edx
```

That same local is compared with authored lengths after queries at
`0x0064962C`, `0x00649653`, and `0x00649790`, then stored to both registered
effects at `0x006498F9` and `0x006499AD`. Mapping EDX at `0x00649593`
therefore fixes all five consumers together.

### Chart pre-roll EAX duration scaling

The chart path calls `sub_5F0450`, which returns an authored duration in EAX,
stores it, and compares it with a frame-domain distance:

```text
0064A92F  call    sub_5F0450
0064A934  mov     [ebp+var_64], eax
0064A937  mov     eax, [ebp+var_68]
0064A93A  sub     eax, [ebp+var_48]
0064A93D  cmp     eax, [ebp+var_64]
```

The other side originates in a millisecond value divided by the runtime frame
duration before its store at `0x005EB82F`. `EffectChartPreRollDuration`
scales positive EAX from authored frames to target frames before the original
store and comparison.

### Player-effect EAX modulo mapping

The player path obtains the authored definition length from `sub_5F1990` in
ECX, loads the target tune frame into EAX, sign-extends with the original
`cdq`, divides by ECX, and stores the remainder in EDX:

```text
0065071D  call    sub_5F1990
00650722  mov     ecx, eax
00650724  mov     edx, [ebp+var_384]
0065072A  mov     eax, [edx+10h]
0065072D  cdq
0065072E  idiv    ecx
00650730  mov     eax, [ebp+var_324]
00650736  mov     [eax+8], edx
```

`EffectPlayerModuloDividend` maps EAX before `idiv ecx`. Positive mappings
remain positive and signed nonpositive sentinels remain bit-identical, so the
existing EDX from `cdq` stays valid and the authored modulo wraps correctly.

## Manager bypass and renderer consumption

The ordinary manager loop tests flag `0x4000` before the only call to
`sub_5F1740`:

```text
005F0917  mov     edx, [ecx+0Ch]
005F091A  and     edx, 4000h
005F0920  jz      short loc_5F0924
005F0922  jmp     short loc_5F095E
...
005F093D  call    sub_5F1740
```

Consequently, `0x4000` effects bypass the ordinary manager increment. This is
why manager gating alone could not cover every producer.

At renderer entry, `sub_5F1F70` consumes the authored frame at `effect+0x08`,
multiplies it by the effect speed scalar at `+0x10`, and converts the result:

```text
005F1F8B  fild    dword ptr [eax+8]
005F1F94  fmul    dword ptr [ecx+10h]
005F1F97  call    __ftol2_sse
005F1F9C  mov     [ebp+var_14], eax
```

The nested-child path calls `sub_5F17A0` with the already-authored parent
frame, applies its local offsets, and stores the derived value:

```text
005F3228  call    sub_5F17A0
005F3233  fild    [ebp+var_314]
005F3239  fsub    [ebp+var_160]
005F323F  fadd    [ebp+var_164]
005F3245  call    __ftol2_sse
005F3266  mov     [ecx+8], esi
```

This child store is therefore `ChildInherited`; mapping it again would
double-convert an authored parent frame.

## Reviewed vector-copy stores

The five initially suspicious non-chart stores are:

```text
0x0064A574
0x00650926  0x00650A8D  0x00650BBB  0x00650C8D
```

The six chart stores are:

```text
0x0064C487  0x0064C4C5  0x0064CA7D
0x0064CABB  0x0064D3E0  0x0064D41E
```

Every site has the same bounded 12-byte copy shape: reserve 12 stack bytes,
copy source dwords at offsets 0, 4, and 8, then pass the completed vector to a
renderer. A representative sequence is:

```text
00650914  sub     esp, 0Ch
00650917  mov     ecx, esp
00650919  mov     edx, [eax]
0065091B  mov     [ecx], edx
0065091D  mov     edx, [eax+4]
00650920  mov     [ecx+4], edx
00650923  mov     eax, [eax+8]
00650926  mov     [ecx+8], eax
```

These are third-coordinate copies, not writes to a CTune object at
`effect+0x08`, so all eleven are explicitly out of scope.

## Asset cross-check

The code census cross-links to the generated
[CTune effect asset catalog](ctune-effect-asset-catalog.md). Its tutorial
canary proves that group-0 definitions 61 through 69 have authored lengths
`16, 38, 34, 13, 16, 14, 14, 38, 38`, all reference texture slot 13, and are
rendered by `img13`/`img_big13` plus language variants. The gameplay path maps
those definitions into effect slots `0xB2` through `0xC0`.

The catalog is verification-only and does not constrain customized runtime
assets.

## Widescreen gameplay-feedback placement boundary (2026-09-03)

This section records the static producer/consumer boundary used by the
windowed-widescreen implementation. It does not claim that the built hook has
executed in the game or that its visual placement is accepted.

### Per-entry combo draw window

The ordinary per-entry combo branch is bounded by these guarded sites:

| VA | RVA | Expected bytes | Placement role |
|---:|---:|---|---|
| `0x005E4503` | `0x001E4503` | `E8 A8 D0 FF FF` | Begin before the static CHAIN-label draw; entry is `[ebp-0x14]`. |
| `0x005E4550` | `0x001E4550` | `E8 0B 7B FE FF` | Read-only byte witness for the normal digit draw inside the window. |
| `0x005E4B58` | `0x001E4B58` | `8B 55 E4 8B 45 E0 89 02 E9 D9 F8 FF FF` | Shared post-effect join; restore the centered HUD viewport only after every layer using this entry's combo value has completed. |

The calls after the normal digit draw reuse the same per-entry combo value for
threshold glow and milestone/celebration layers before joining at `0x005E4B58`.
Restoring at the old `0x005E4558` boundary split those layers across two
viewports and produced a centered duplicate number. The complete per-entry
presentation now stays together: entry 0 selects the right 720-pixel viewport,
entry 1 selects the left viewport, and an unexpected entry selects center.

### Player 1 judgement effect ownership

The slots in this section own track-position grade effects. They do not include
the fixed-position text produced by 648D40 at slots 12,15,18,24,27,30 decimal.
The [2026-09-06 follow-up](widescreen-judgment-text-followup-2026-09-06.md)
records both families. The later
[right-side flash correction](widescreen-right-flash-followup-2026-09-06.md)
leaves track-position slots 93–97 unmodified and selects only the fixed HUD text
and tutorials. These corrections supersede the older placement scope below.

`sub_6463F0` accepts judgement owner `nn` only when `nn < 4`. For native
grades 0 through 4, it resolves and registers:

```text
CTune.effect_slots[93 + 5 * nn + grade]
```

The four-value bound belongs to this native judgement-effect owner array. It is
not evidence for a maximum count of online matching or LAN participants.
Current widescreen scope is Player 1 only, so the implementation matches the
first lane's five roots at slots 93 through 97. It neither selects a lane from
participant state, reads another lane's coordinates, nor rewrites
`effect+0x18`.

The former proposed hook at VA `0x006465F3` / RVA `0x002465F3` was rejected
after runtime feedback. Native placement at that boundary is the sum of CTune
vectors at `0x1C8C + 12 * nn` and `0x1CBC + 12 * nn`; deriving a direction
from two entries of only the second array was not valid. The implementation no
longer hooks this site. The existing authored-frame timing hook at RVA
`0x00246517` remains unchanged.

`sub_646650` remains excluded. It owns a different effect family and is not
needed to identify the Player 1 judgement roots.

## Widescreen Player 1 feedback placement boundary (2026-09-03)

`GC120FPS_GameplayRender_Effects_FrameDomainTiming` at `0x00648D40` selects
group-0 tutorial definitions 61 through 69 and resolves them through CTune
runtime effect slots. The note-type paths use slots `0xB2`, `0xB3`, `0xB4`,
`0xB5`, `0xB6`, `0xBA`, `0xBB`, and `0xC0`; paired variants at `0xB3` and
`0xBB` also use companion slot `0xB9`. Their authored base X coordinate is 580,
confirming that the family was placed near the portrait canvas's right edge.

The supported game's effect collection is a native 24-byte collection at
`CTune+0x1D6C`, not a zero-offset three-pointer `std::vector`.
`sub_4128A0` computes its count from the pointers at collection offsets `+0x0C`
and `+0x10`; `sub_43D0C0` indexes the pointer array beginning at `+0x0C`.
Player 1 judgement matching therefore validates both pointers, their ordering
and four-byte alignment, the required element count, and slots 93 through 97
before comparing live effect pointers.

CTune is captured before the player-visual call at:

```text
00662FA8  E8 D3 56 FE FF       call    00648680
00662FAD  8B 4D C4             mov     ecx, [ebp-3Ch]
```

This is earlier than the gameplay-effects call because Player 1 judgement is
drawn by the player-visual path before tutorial and the other orthographic
effects. The later gameplay-effects call remains bracketed by:

```text
00663041  E8 FA 5C FE FF       call    00648D40
00663046  E8 D5 00 DF FF       call    00453120
```

At `0x00663041`, `ecx` is checked against the CTune captured at
`0x00662FA8`, and the pointer is cleared at `0x00663046`. This spans the
player-visual judgement draw and the subsequent tutorial pass without
classifying generic manager draws outside the gameplay task.

`sub_5F1180` calls the complete effect-tree renderer at this bracket:

```text
005F11E5  8B 4D F8             mov     ecx, [ebp-8]
005F11E8  E8 83 0D 00 00       call    sub_5F1F70
005F11ED  8B 4D F8             mov     ecx, [ebp-8]
005F11F0  8B 51 0C             mov     edx, [ecx+0Ch]
005F11F3  81 E2 00 40 00 00    and     edx, 4000h
```

The call-site `ecx` is compared only with Player 1 judgement slots 93 through
97. On a match, the compositor flushes pending batches, captures the complete
physical-3D D3D state, and applies Player 1's right 720-pixel HUD viewport.
`sub_5F1F70` renders child effects before returning, so the complete
judgement tree inherits that placement. The post-call boundary flushes the
tree and restores the exact captured physical state. Other effects and all
manager calls outside the captured CTune scope are unchanged.

Tutorial placement uses the concrete group-6 call made by
`GC120FPS_GameplayRender_Effects_FrameDomainTiming`:

```text
0064A2D5  E8 A6 6E FA FF       call    005F1180
0064A2DA  0F B6 55 08          movzx   edx, byte ptr [ebp+8]
```

The begin boundary selects Player 1's right gameplay-HUD viewport and the end
boundary restores center. This keeps the full note-tutorial family together
without matching its individual CTune roots or inferring another local player
from network state.

## Widescreen Test Mode containment boundary (2026-09-03)

Test Mode form rendering is not owned by the common 2D task vtable. The
`LoopLastTask_RunStateMachine` path invokes `sub_576660`, which forwards the
active Test Mode root window through `sub_4BFEE0` and recursive `sub_4C27B0`
form rendering. Its narrow call bracket is:

```text
0063AA89  E8 D2 BB F3 FF       call    sub_576660
0063AA8E  E8 8D 86 E1 FF       call    sub_453120
```

Because this path calls Direct3D `BeginScene`/`EndScene` directly rather than
the game's normal frame wrapper, the begin hook opens a standalone compositor
frame and requests `Native2D`. The end hook composites and closes that frame
before the direct `EndScene`. This confines the complete Test Mode
`CRootWindow` traversal to the centered 720 x 1280 native canvas while
retaining the configured wide window and real backbuffer.

## Completion boundary

The executable identity, xref census, timing-site dispositions, hook byte
contracts, four new producer data flows, manager bypass, renderer consumer,
child derivation, and vector-copy exclusions are statically complete for the
recorded SHA-256. On 2026-09-03 the operator accepted centered Test Mode and
the final Player 1 judgement/tutorial placement in the deployed configuration.
The broader multi-stage and frame-rate acceptance matrix remains an
operator-run gate.
