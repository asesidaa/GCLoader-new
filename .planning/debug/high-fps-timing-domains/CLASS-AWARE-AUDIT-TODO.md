# Class-Aware Input/Judgement Audit Todo

This is the persisted work list for the `game471.exe.i64` reverse-engineering
audit. It is intentionally evidence-first: no production high-FPS patch is to
be changed until the class-qualified input and judgement paths are documented.

## Scope and invariants

- Target database: `H:\gc\game471.exe.i64`.
- Runtime tree: `H:\gc`; source and audit documents: `H:\gc\artifacts\GCLoader`.
- RTTI parser source: `H:\PyClassInformer\pyclassinformer\msvc_rtti.py`.
- Preserve the real one-lane Switch-input model; do not introduce lanes or
  unsupported simultaneous-note semantics.
- Audit note types `0` through `15` and post-descriptor free input explicitly;
  chart `HIDDEN/HIDDEN2` notes must remain distinct from free input.
- Preserve original forgiving judgement rules. `GameTimeOffset` and
  `JudgTimeOffset` are the only variable timing settings; other judgement
  settings are static for this audit.
- Do not modify or deploy production patches during this audit.

## Evidence already complete

- [x] Existing IDA-CLI daemon verified for `H:\gc\game471.exe.i64` with the
  `idalib` backend.
- [x] Full PyClassInformer RTTI dump: 1,096 vtable records; SHA-256
  `930B31D55AB598CB6E2425810025FFD1A30655E80102C61A4FE25204941AB91`.
- [x] Class/method reverse index: 801 unique class names, 1,096 vtables,
  3,852 unique method EAs, and 10,313 vtable-slot references; SHA-256
  `c411345c5010d678294b0a57e5c60ca7230e0f16e1b239a3063bf21ab14e7f71`.
- [x] Initial related-class selection: 220 RTTI class records.
- [x] Initial class-method decompile set: 104 unique functions; SHA-256
  `ac01f8f6fb3c1a2e25560124fefc3214b675c5d11a4bd6f9c1c53a6227333d9f`.
- [x] Ambiguous receiver callsite audit and tune-data/ambiguous-method
  artifacts generated.
- [x] Class ownership confirmed for `CTuneGameManager`, `CGameMainTask`,
  `CDemoPlayTask`, `CBooster`, `CInputDevice`, `CSeqTaskBase`, and the GW
  input-device classes.

## Completed work

### 1. Inventory and provenance

- [x] Preserve the exact artifact paths, parser source hashes, target IDB hash,
  and generation timestamp in the canonical audit.
- [x] Verify that every selected method is linked to a concrete RTTI vtable,
  slot, subobject offset, and class hierarchy path.
- [x] Separate true class members from inherited slots, compiler/runtime
  helpers, `__purecall`, and duplicate vtables caused by multiple inheritance.

### 2. Physical input class chain

- [x] Trace `gw::GWInputDeviceXioFio_BOOST` virtual methods and direct callers,
  including the iDmac/FIO snapshot translation method.
- [x] Trace `gw::GWInputDeviceXio_BOOST` and `gw::GWInputDeviceXio` inherited
  update/query slots and identify which subobject offset is used at each call.
- [x] Trace `gw::GWInputXio::UpdatePollAggregate` and its helper methods,
  recording snapshot, pressed, held, released, and repeat field production.
- [x] Trace the gameplay input-frame entry (`0x659920`) to the concrete
  `CBooster` receiver and record all virtual dispatch edges.
- [x] Trace every `CBooster` slot 0–10 method, including initialization,
  history capture, ring-index conversion, edge/held/release queries, direction
  vector, and consecutive-held counters.
- [x] Record exact control-ID behavior for ordinary IDs `0–9`, composite IDs
  `10–14`, and paired IDs `15–19`, including the four-frame lookback.
- [x] Identify all keyboard-fallback or synthetic-input branches and prove
  whether they are active in the real Switch-input path.

### 3. Gameplay task and tune-manager classes

- [x] Trace `CSeqTaskBase` lifecycle slots and all `CGameMainTask` overrides;
  distinguish generic task scheduling from gameplay ownership.
- [x] Trace `CDemoPlayTask` slot 6 into the tune-manager state machine and
  record whether it is a demo-only path or shared gameplay path.
- [x] Trace `CTuneGameManager` constructor, singleton accessor, destructor,
  state-machine member, judgement-frame member, cleanup, initialization,
  render, and transition helpers.
- [x] Prove the owner and meaning of manager fields at offsets `+147`, `+149`,
  and `+155` using constructor writes, member callers, and collection access.
- [x] Trace all direct and virtual callers of the judgement-frame processor;
  record the exact time/frame conversion operands and offset application.
- [x] Review `CHitChartTask` and related chart-task methods to determine which
  are chart/reboost lifecycle code versus actual judgement dispatch.

### 4. Judgement and note coverage

- [x] Build a single raw/canonical/effective matrix for note types `0–15` and
  post-descriptor free input; E-046 supersedes E-042's initial raw-only table.
- [x] For each type, record the dispatcher entry, handler, input query
  (pressed/held/released/direction/count), start/end semantics, miss path,
  result-scoring path, and audio/effect path.
- [x] Explicitly document scratch, hold, slide-hold, beat, dual-hold, chart
  `HIDDEN/HIDDEN2`, and post-descriptor free-input behavior under the one-lane
  model.
- [x] Distinguish start/end judgement from in-between hold/interval validity;
  do not infer simultaneous-note behavior from generic engine support.
- [x] Trace result-code generation and score mapping to prove which path emits
  GREAT/GOOD/MISS and which paths only render or play effects.
- [x] Record how `GameTimeOffset` and `JudgTimeOffset` enter the comparison;
  treat all other judgement timing values as static constants.

### 5. Virtual dispatch and caller evidence

- [x] For every high-impact callsite, record the receiver source, vtable
  address, slot, subobject offset, and the resolved implementation.
- [x] Mark unresolved indirect calls as unresolved; do not label a generic
  helper as a class method without receiver evidence.
- [x] Export focused decompiles/disassembly for unresolved dispatches and
  resolve them from constructor/vtable/object-flow evidence.
- [x] Keep a list of rejected hypotheses and the evidence that ruled them out.

### 6. IDA annotation ledger

- [x] Add class-qualified names only where RTTI ownership is proven.
- [x] Add concise repeatable comments for the input chain, manager fields,
  dispatcher, and note handlers.
- [x] Record every rename/comment mutation in an annotation ledger with EA,
  old name, new name, reason, and source artifact.
- [x] Save the IDB only after the ledger is complete; verify the saved database
  reopens and all annotations remain present.

### 7. Canonical documentation

- [x] Update `high-fps-timing-domains.md` and the sibling `CONTEXT.md`,
  `FINDINGS.md`, or `RESULTS.md` as appropriate with the class-qualified
  pipeline and note matrix.
- [x] Link each conclusion to an artifact rather than to a guessed symbol.
- [x] State the evidence boundary: static/IDA proof is not gameplay acceptance.

### 8. Native catch-up and loader scope

- [x] Trace native history fill, judgement processing, and authoritative frame
  commit order at `0x6630B0`.
- [x] Record the `0x6401E0` catch-up loop, integer recognition-time projection,
  and 60/144/165/240 FPS step maxima.
- [x] Prove skipped history entries propagate held state without fresh physical
  polling or synthetic pressed edges.
- [x] Trace the loader transition sample and `JudgementInputScope` lifetime
  through core, descriptor, and post-descriptor free-input dispatch.
- [x] Re-evaluate descriptor re-routing/free-input ownership against native
  candidate order; E-045 proves pressed edges are non-consuming and narrows the
  loader risk to note-dependent mutation of one immutable sample.
- [x] Persist the catch-up/selection boundary in E-043/E-045 and the final
  normalization/progression closure in E-046; link all three from the
  canonical indexes.

### 9. Native result and score closure

- [x] Prove the separate per-player judgement-state and score-state manager
  fields, allocations, constructors, and polymorphism boundary.
- [x] Trace timing-grade, duration-grade, component aggregation, resolved-grade
  retrieval, and score-state processing with one recognition timestamp.
- [x] Map grades `0..3` to MISS/GOOD/COOL/GREAT and to the four score-state
  counters.
- [x] Separate note/effect metadata publication from score accounting.
- [x] Prove chart `HIDDEN/HIDDEN2` notes are distinct from post-descriptor free
  input and correct the canonical terminology.
- [x] Persist E-044/E-045, the IDA names/comments, and the final saved IDB hash.

### 10. Native normalization and progression closure

- [x] Prove raw aliases `B→A`, `C/E→9`, and `D→4`; distinguish raw,
  canonical `+0x04`, and effective `+0x00` descriptor types.
- [x] Prove mode `2`, mode `17`, and equal-time suppression rewrite effective
  type only, and effective type `0` is skipped.
- [x] Prove each candidate build inserts only the first incomplete descriptor
  per internal chart row and same-row followers are absent from the current
  recognition step.
- [x] Prove later catch-up steps carry held state without replaying the earlier
  pressed edge.
- [x] Prove free input runs before `0x5D58D0` refreshes row eligibility at
  `+0xCC`, and that neither `0x5D58D0` nor `0x5D2A00` advances a cursor.
- [x] Apply/read back the seven final helper renames, ten repeatable comments,
  and the corrected `0x5D68E0` function comment.

### 11. Final verification gate

- [x] Recompute hashes for the final IDB and closure records. The current final
  IDB is
  `3F911E373D18F4C3F11DACF5759AB7FF08847A4F365E8C0ED17B2896E7C47163`;
  E-046 is
  `E8FC9C48898A8C659EAD292312824DF45B8A281D3F74FD33150B60CBE37B633E`;
  progression pass42 is
  `8AD7B2B4C592AAB1A4797DD1FC62F98ACA73FA34E6B9E33170909676490FFA50`;
  annotation readback pass43 is
  `48AA2536D052881C20C902FEB05D4109D3185E50E88D935B4F3EDE5001A6F274`;
  and the final annotation ledger is
  `BF4A49528029E143AFFD6D7CD1F78E2D35637C6804A4A7DBC6C71570A957D239`.
- [x] Run `git diff --check` in the main checkout after final reconciliation
  and inspect status; untracked audit files also pass explicit trailing-
  whitespace checks. The implementation worktree was checked at the earlier
  source-snapshot checkpoint and was not changed during final native cleanup.
  Preserve the unrelated `src/Nesys/NesysServicePatch.cpp` modification.
- [x] Confirm no production source, deployed DLL, runtime binary/config, or
  high-FPS patch was changed as part of this audit; the intentional IDB
  annotation save is the only runtime-tree change. The prior runtime checkpoint
  hashes, not revalidated by documentation cleanup, were
  `game471.exe=FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522`,
  `iDmacDrv32.dll=A843A295B1D0A4F50625F63C04C684799C5CF9CE8F5476265C33FC15B5A4E223`,
  and `config.toml=1073E34F909F36E7285CE9B9F34FC24DD9ECF4D52AC6FCCC4621F936FA6B4C17`.
- [x] Only after review, write a separate implementation plan for any approved
  patch; do not mix implementation decisions into this audit list.

## Required final outputs

- `game471-pyclassinformer-rtti.json`
- `game471-pyclassinformer-class-method-index-2026-08-17.json`
- class-qualified method/caller decompiles
- virtual-dispatch evidence and annotation ledger
- complete note-type `0–15` plus post-descriptor free-input matrix
- updated canonical high-FPS timing audit documents

## Evidence index (authoritative; do not regenerate)

The following artifacts were produced before this checklist was created. They
are the source of truth for completed work. A later session must read these
artifacts and continue from the unchecked items above rather than rerunning the
same scans.

- Full RTTI dump: `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-pyclassinformer-rtti.json`
  (1,096 vtable records; SHA-256
  `930B31D55AB598CB6E2425810025FFD1A30655E80102C61A4FE25204941AB91`).
- Class/method index: `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-pyclassinformer-class-method-index-2026-08-17.json`
  (801 class names, 1,096 vtables, 3,852 method EAs, 10,313 slot
  references; SHA-256
  `c411345c5010d678294b0a57e5c60ca7230e0f16e1b239a3063bf21ab14e7f71`).
- Initial class-method decompiles:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-class-method-decompiles-2026-08-17.json`
  (104 unique functions; SHA-256
  `ac01f8f6fb3c1a2e25560124fefc3214b675c5d11a4bd6f9c1c53a6227333d9f`).
- Ambiguous receiver audit:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-ambiguous-receiver-callsite-audit-2026-08-17.json`.
- Tune-data/ambiguous-method decompiles:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-tune-data-and-ambiguous-method-decompiles-2026-08-17.json`.
- Physical input entry and device chain:
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\audit-decompile-loop-entry.json`,
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\highfps-input-native-helpers-658fa0-659858.txt`,
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\highfps-input-cbooster-62ce80-62e50f.txt`,
  and `H:\gc\runs\20260815T182438Z-297470b1\artifacts\audit-input-helper-decompile-2026-08-17.txt`.
- Class-qualified trace derived from the existing index (no RTTI scan rerun):
  `H:\gc\runs\20260815T182438Z-297470b1\artifacts\game471-class-qualified-input-gameplay-trace-2026-08-17.json`
  (30 selected RTTI records, 97 unique class-method EAs, 20 explicit targets;
  SHA-256 `1EB911B572D9E76C15B90007F6D3559B02B9D847D1DF1DA7CA4ACBAE0456FDE0`).
- Native catch-up and loader-scope audit:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-catchup-judgement-boundaries-2026-08-17.json`
  (SHA-256
  `710FE451CC0A9C3A9A3B9E9EB65529AD422DAEC8113C324198D2178266D3D110`),
  recorded in E-043.
- Native note-ownership closure:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-note-ownership-closure-pass4.json`
  (SHA-256
  `9C4EFDE702C0E93A9EBE2ED3D161AB0373EA23A95984C618A45E024E142D822E`).
- Native terminology/string evidence:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-note-terminology-strings-pass5.json`
  (SHA-256
  `0311EA16BD9340EE1F9F66227C4B0E6860106EE375E17123594DC26312E27E66`).
- Score-state owner evidence:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-score-state-owner-pass5-2026-08-17.json`
  (SHA-256
  `1DD670D12EDC1D3972C19E6A48D59ED628E31D7649ED7379499CE22846961407`).
- Native result/publication closure:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-result-publication-closure-pass6.json`
  (SHA-256
  `1CAA9694F4C0B98FD6D73D7C0DDEF9BEA81B7A20333B859D0CD593DB65DA8929`),
  recorded in E-044.
- Native component/free-input closure:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\game471-native-component-freeinput-closure-2026-08-17.json`
  (SHA-256
  `E308A5047C2A5DAE91E8BF8F51FB15E622B4FB96F500D2B4D52E41D55174964C`),
  recorded in E-045.
- Raw/canonical/effective normalization:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-raw-canonical-effective-matrix-pass41.json`
  (SHA-256
  `5AD64F549E5DEAFF58A833CF6F18DBF65B67BBC2E6930C17B8E5D6D26C67C09A`).
- Final progression/call-order closure:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-progression-cursor-closure-pass42.json`
  (SHA-256
  `8AD7B2B4C592AAB1A4797DD1FC62F98ACA73FA34E6B9E33170909676490FFA50`).
- Final annotation readback:
  `H:\gc\runs\20260816T210335Z-a3aabe78\artifacts\native-annotation-readback-pass43.json`
  (SHA-256
  `48AA2536D052881C20C902FEB05D4109D3185E50E88D935B4F3EDE5001A6F274`),
  recorded with the final IDB hash in E-046.

### Completed class-qualified facts with evidence

- `gw::GWInputDeviceXioFio_BOOST` snapshot translation: vtable
  `0x6B7880`, slot 2, method `0x4B4500`.
- `gw::GWInputXio` aggregate poll: vtable `0x6AE400`, slot 0, method
  `0x456360`.
- Gameplay input-frame entry: `0x659920` invokes the input-device capture path.
- `CBooster` history capture: vtable `0x6F82A4`, slot 3, method `0x62CFB0`.
- `CBooster` query slots: pressed `0x62DFB0`, held `0x62DF50`, released
  `0x62DD30`, direction `0x62E480`, consecutive-held `0x62DAA0`.
- `CBooster` control semantics: ordinary IDs `0–9`, composite IDs `10–14`,
  and paired IDs `15–19` with the prior-four-frame lookback are documented in
  the CBooster decompile artifact above.
- `CTuneGameManager` class ownership: vtable `0x6FA64C`, locator `0x70F6B0`;
  nonvirtual gameplay/judgement members are listed in the class-method
  decompile and tune-data artifacts above.
