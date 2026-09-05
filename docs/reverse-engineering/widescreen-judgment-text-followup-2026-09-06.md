# Widescreen judgment text follow-up

The first fine-placement build omitted the fixed-position judgment text roots.
The user reports that most other placement is correct, but judgment text remains
misplaced. The correction below is implemented and built. The operator subsequently
confirmed GREAT placement; the [right-side flash follow-up](widescreen-right-flash-followup-2026-09-06.md)
removes the separate track-effect override retained in this historical build.

## Runtime evidence

The 03:15:48 launch used the first corrected Release DLL, SHA-256
6428009A70F608C193C1BB481C5583324EAF6BDBC64EC2C74880F251B89F7524.
It matched the build artifact and logged selected_draws with 91 installed hooks.
The 1,510,655-byte log is preserved at
.codex-tmp/widescreen-judgment-20260906/loader-log.txt.

| Owner | Sampled submissions | Viewport at submission | Meaning |
| --- | ---: | --- | --- |
| Slot 18 | 8 | 778,0,720,1280 | Current primary GREAT text still centered |
| Slot 30 | 8 | 778,0,720,1280 | Previous primary GREAT text still centered |
| Slot 93 | 1 | 1556,0,720,1280 | Track-position MISS effect already selected |

Slots 18 and 30 are bank 0/group 6, with root position (720,380,0), scale
(2.25,2.25,1), and an orthographic projection. The first matched packet pairs
are 23 at song_ms=6616 and 26 at song_ms=6933. Both allocation and submission
were centered. The slot-93 sample was at song_ms=4; it is not sufficient to
claim that every user-observed MISS presentation is visually correct.

## Native ownership

Fresh IDA-CLI analysis used H:/gc/game471.exe.i64, input SHA-256
FEAD3BD4D0E0985F101965EDC417DD2B96522F8716FF789D84618FEB0D7A2522.

- 5CE680 stores a successful resolved grade in score state +136, shifting the
  previous grade to +140. MISS clears both success-history entries. 5CD9E0
  retrieves one of those two entries.
- 648D40 reads the displayed score entries and their two history entries. For
  a nonzero grade it selects slot 24 * display_entry + 12 * history + 9 +
  3 * grade. The variant read is explicitly overridden to zero in this binary.
- For the primary display entry (0), grades 1/2/3 therefore use slots
  12/15/18, with previous-history slots 24/27/30. These are GOOD/COOL/GREAT.
  Their bank-0 definitions are 9/10/11; 660D20 constructs this separate family.
  The special grade-3 asset replacement retains the same root identity.
- 6463F0 instead selects track-position effects at slots 93 + 5 * lane + grade,
  using bank-1 definitions 29..33 for lane 0. These include the MISS effect at
  slot 93 and are not a complete selection of the fixed-position text.

This separates the fixed-position text, track-position grade effects, CHAIN
counter, tutorials, and finish announcements. A shared group number is not
placement ownership.

## Implemented correction

Added kPlayerOneJudgementTextSlots = {12,15,18,24,27,30}. Root matching includes
these six exact pointers in the existing judgment packet-ownership path.
Their queued sprites receive the right viewport only while 5F0600 submits them,
then restore the centered viewport. No hook sites or protected spans changed.
The other display entry, unused variants, result slots 2..6, and all other
group-6 roots retain their existing placement.

Automatic trace labels now distinguish player1_judgement_text from
player1_judgement_track_effect. The next run should show the text's allocation
at center, submission at right with draw_scope=4/judgement_scope=1, and restoration
at center. The group-6 markers themselves must remain centered.

## Verification and artifact

Both full Debug and Release builds passed without compiler/linker warnings or
errors. CLion completed inspections of the three changed source files without
errors. IDA rechecked the existing 94 byte contracts and their hook boundaries;
no mismatches or overlaps were found. git diff --check passed.

Release DLL: build-msvc32-release/dist/iDmacDrv32.dll.
SHA-256: CD4AF32C591B4BEC401035221B0BFBE7F77DBABEA61334D29CC5853EDC8A02A8.
Matching PDB: build-msvc32-release/src/iDmacDrv32.pdb.
The operator subsequently deployed this artifact and confirmed GREAT text placement.
The later flash correction has its own acceptance boundary.
