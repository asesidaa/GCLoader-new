# XFL Timeline and Transition Inventory

- Status: complete
- Audit date: 2026-07-25
- Analyst scope: read-only inventory of `H:\gc\artifacts\2d_boost`; this file is the only audit artifact owned by this analyst.

## Scope and evidence baseline

- Corpus: `H:\gc\artifacts\2d_boost`
- Source/audit worktree: `H:\gc\artifacts\GCLoader\.worktrees\ctune-effect-timing`
- Fixed source baseline from the master ledger: commit `2354d0f`
- Evidence class in this document: generated XFL XML (`DOMDocument.xml` and `LIBRARY/*.xml`) plus corpus layout. No production source or executable claims are made here.
- Corpus policy: the XFL converter documentation describes `MOVC` definitions as movie clips with their own `TIME`/`FRAM` timelines, `ASRC` as ActionScript source, and the document `TIME` as the main scene timeline (`H:\gc\artifacts\2d_boost\CLAUDE.md`, sections “RVB Format” and “Tree hierarchy”).

## Method

1. Discover every `DOMDocument.xml` recursively rather than relying on a hand-selected screen list.
2. Parse each document and every referenced or present `LIBRARY/*.xml` with namespace-aware XML handling.
3. Count document frame rate, main-timeline span, symbol/movie-clip timelines, multi-frame nested timelines, labels, scripts, instance/export names, and transition primitives.
4. Preserve exact project-relative XML paths, frame indices, labels, and script text for correlation.
5. Separate the document main playhead from symbol movie-clip playheads; treat script-addressed child/root/parent transitions as independent-domain evidence, not proof of runtime scheduler behavior.

## Baseline observations

- The corpus root contains generated XFL projects for both language/RTM variants and many menu families, alongside raw `.rvb`/`.mtx` inputs and non-XFL asset directories.
- This inventory will use the complete recursive `DOMDocument.xml` set as the project denominator. Quantitative results and exceptions are pending.

## Corpus inventory checkpoint

### Denominator and completeness

- Recursive discovery found **59 XFL projects**, **57 top-level `.rvb` files**, and **57 top-level `.mtx` files**. Every one of the 57 same-named RVBs has a same-named `_xfl` project.
- The two additional XFL projects are `navi_001_yume_xfl` and `RECOVER_demo2`. The former is the decoded standalone navigator project discussed by E-036; the latter is a second demo2 reconstruction. They remain separate rows rather than being silently deduplicated.
- The full XFL set is:
  - common/custom: `common_xfl`, `common_eng_xfl`, `custom2_xfl`, `custom2_eng_xfl`
  - attract/start: `demo2_xfl`, `demo2_eng_xfl`, `demo2_eng_rtm_xfl`, `RECOVER_demo2`, `start2_xfl`, `start2_eng_xfl`, `start2_eng_rtm_xfl`
  - selection: `selectmode_eng_xfl`, `selectmode2_xfl`, `selectmode2_eng_xfl`, `selectgame2_xfl`, `selectgame2_eng_xfl`, `selectmusic2_xfl`, `selectmusic2_eng_xfl`, `selectmusic_g_xfl`, `selectcourse_xfl`, `selectcourse_eng_xfl`
  - network/information: `matching_xfl`, `matching_eng_xfl`, `net_xfl`, `net_eng_xfl`, `ranking_xfl`, `ranking_eng_xfl`, `hitchart_xfl`, `hitchart_eng_xfl`
  - reward/result: `event_reward_xfl`, `event_reward_eng_xfl`, `reward2_xfl`, `reward2_eng_xfl`, `result_xfl`, `result_eng_xfl`, `result_ehs_xfl`, `result_ehs_eng_xfl`, `result_eve_xfl`, `result_eve_eng_xfl`, `result_evesolo_xfl`, `result_evesolo_eng_xfl`, `result_local_xfl`, `result_local_eng_xfl`, `result_w_xfl`
  - other screens: `gameover_xfl`, `gameover_eng_xfl`, `signature_xfl`, `signature_eng_xfl`, `stamp_xfl`, `stamp_eng_xfl`, `trophy_xfl`, `trophy_eng_xfl`, `tutorial_xfl`, `tutorial_eng_xfl`, `unlock_xfl`, `unlock_eng_xfl`, `unlock_reward_xfl`, `unlock_reward_eng_xfl`, `navi_001_yume_xfl`
- `avatar`, `cutin`, `game`, `item`, `menu`, `message`, `news`, `se`, `skin`, and `title` contain texture/config assets but no `DOMDocument.xml`, `LIBRARY/*.xml`, or `.rvb` in this corpus snapshot. In particular, `menu` has 24 `.dds` files and `title` has 5,697 `.dds` files; they are not additional Flash timeline projects.

### Metadata caveat and generated controller

- Every discovered document has `frameRate="30"` (for example `demo2_xfl\DOMDocument.xml:1` and `navi_001_yume_xfl\DOMDocument.xml:1`). This is **document metadata only**. It does not establish the baseline game cadence or the runtime scheduler/update rate.
- Every document also contains a red `DOMLayer name="Controller"` whose script builds a keyboard/mouse label-jump panel and calls `MovieClip(root).gotoAndPlay(e.currentTarget.name)` (for example `demo2_xfl\DOMDocument.xml:47-128`). That panel is corpus tooling, not an RVB-authored transition. All ActionScript totals below exclude scripts under this `Controller` layer; timeline spans and authored labels are unaffected.
- E-036’s standalone navigator cross-check agrees with the fresh parse on the asset facts: 9 movie clips and explicit nested `lf_loop`/`lf_stay` mouth/body timelines in `navi_001_yume_xfl`. E-036’s separate runtime-target rejection is retained as historical context only; this XFL inventory makes no runtime identity or scheduler claim.

## Quantitative coverage table

Definitions:

- `Main` is the document timeline span, computed as the maximum zero-based frame `index + duration`.
- `MC` counts `DOMSymbolItem` definitions that are not `symbolType="graphic"`.
- `MF all/r/d` means all multi-frame movie-clip definitions / definitions reachable from a document-timeline instance / reachable definitions at graph depth 2 or deeper.
- `Max MF` is the longest nested movie-clip span.
- `Labels M/N` counts label placements on the main timeline / movie-clip timelines. It counts placements, not distinct strings.
- `GP/GS/P/S` counts authored `gotoAndPlay` / `gotoAndStop` / bare `play()` / bare `stop()` calls after excluding the generated Controller.
- `X-call all/nested` counts explicit cross-playhead `gotoAnd*` lines / the subset whose frame script itself lives in a library movie clip.
- Rows are grouped only when all displayed quantitative fields match. Every named project remains in the 59-project denominator.

| Project(s) | FPS | Main | MC | MF all/r/d | Max MF | Labels M/N | GP/GS/P/S | X-call all/nested |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `common_eng_xfl`, `common_xfl` | 30 | 4 | 79 | 48/48/36 | 240 | 4/222 | 35/7/70/126 | 12/12 |
| `custom2_eng_xfl` | 30 | 29 | 64 | 36/23/18 | 241 | 11/132 | 19/0/53/94 | 4/1 |
| `custom2_xfl` | 30 | 29 | 63 | 36/23/17 | 241 | 11/132 | 19/0/53/94 | 4/1 |
| `demo2_eng_rtm_xfl`, `demo2_eng_xfl`, `demo2_xfl`, `RECOVER_demo2` | 30 | 65 | 10 | 3/3/1 | 226 | 17/25 | 2/0/21/24 | 2/0 |
| `event_reward_eng_xfl`, `event_reward_xfl` | 30 | 19 | 11 | 8/8/5 | 92 | 5/47 | 2/0/10/34 | 0/0 |
| `gameover_eng_xfl`, `gameover_xfl` | 30 | 33 | 3 | 2/2/1 | 161 | 6/11 | 3/0/5/6 | 1/0 |
| `hitchart_eng_xfl`, `hitchart_xfl` | 30 | 18 | 12 | 9/9/4 | 129 | 18/76 | 43/3/15/46 | 45/0 |
| `matching_eng_xfl`, `matching_xfl` | 30 | 21 | 34 | 18/18/9 | 104 | 9/89 | 7/1/23/61 | 3/0 |
| `navi_001_yume_xfl` | 30 | 26 | 9 | 9/9/8 | 17 | 9/25 | 16/6/12/24 | 16/16 |
| `net_eng_xfl`, `net_xfl` | 30 | 29 | 2 | 2/2/1 | 300 | 7/13 | 2/0/9/9 | 0/0 |
| `ranking_eng_xfl`, `ranking_xfl` | 30 | 31 | 20 | 6/6/2 | 156 | 6/58 | 3/0/14/28 | 2/1 |
| `result_ehs_eng_xfl`, `result_ehs_xfl` | 30 | 16 | 39 | 22/22/18 | 132 | 7/72 | 18/2/16/48 | 11/2 |
| `result_eng_xfl`, `result_xfl` | 30 | 21 | 34 | 21/21/15 | 184 | 7/81 | 16/0/34/48 | 3/0 |
| `result_eve_eng_xfl`, `result_eve_xfl` | 30 | 32 | 27 | 17/17/13 | 152 | 17/120 | 4/8/59/87 | 12/8 |
| `result_evesolo_eng_xfl`, `result_evesolo_xfl` | 30 | 19 | 58 | 40/40/34 | 188 | 6/174 | 19/3/45/118 | 6/0 |
| `result_local_eng_xfl` | 30 | 35 | 88 | 51/51/42 | 180 | 12/254 | 17/5/56/197 | 6/2 |
| `result_local_xfl` | 30 | 35 | 104 | 53/53/43 | 190 | 12/273 | 17/5/58/210 | 6/2 |
| `result_w_xfl` | 30 | 29 | 47 | 18/18/13 | 240 | 12/62 | 16/0/30/40 | 5/1 |
| `reward2_eng_xfl` | 30 | 14 | 28 | 19/19/11 | 46 | 6/77 | 3/0/20/66 | 3/0 |
| `reward2_xfl` | 30 | 14 | 28 | 19/19/11 | 46 | 6/79 | 3/0/20/66 | 3/0 |
| `selectcourse_eng_xfl`, `selectcourse_xfl` | 30 | 25 | 13 | 9/9/6 | 61 | 5/25 | 3/9/11/23 | 10/9 |
| `selectgame2_eng_xfl` | 30 | 14 | 51 | 38/38/34 | 147 | 7/111 | 26/0/27/93 | 19/16 |
| `selectgame2_xfl` | 30 | 14 | 52 | 39/39/35 | 147 | 7/115 | 26/0/27/97 | 19/16 |
| `selectmode2_eng_xfl`, `selectmode2_xfl` | 30 | 81 | 129 | 81/63/59 | 190 | 10/256 | 59/0/72/189 | 17/14 |
| `selectmode_eng_xfl` | 30 | 81 | 102 | 70/58/54 | 180 | 8/196 | 56/0/67/135 | 17/14 |
| `selectmusic2_eng_xfl`, `selectmusic2_xfl` | 30 | 22 | 74 | 53/49/44 | 180 | 8/198 | 21/1/39/165 | 7/2 |
| `selectmusic_g_xfl` | 30 | 45 | 31 | 17/17/13 | 128 | 7/47 | 10/0/15/41 | 2/0 |
| `signature_eng_xfl`, `signature_xfl` | 30 | 181 | 1 | 0/0/0 | 0 | 15/0 | 0/0/6/7 | 0/0 |
| `stamp_eng_xfl`, `stamp_xfl` | 30 | 60 | 14 | 10/10/8 | 122 | 15/103 | 1/0/11/101 | 0/0 |
| `start2_eng_rtm_xfl`, `start2_eng_xfl`, `start2_xfl` | 30 | 28 | 48 | 25/23/19 | 300 | 12/143 | 45/0/81/83 | 28/23 |
| `trophy_eng_xfl`, `trophy_xfl` | 30 | 55 | 6 | 3/3/0 | 60 | 8/5 | 1/0/3/7 | 0/0 |
| `tutorial_eng_xfl`, `tutorial_xfl` | 30 | 9 | 13 | 11/11/2 | 240 | 9/29 | 3/0/20/27 | 0/0 |
| `unlock_eng_xfl` | 30 | 23 | 48 | 22/21/14 | 1000 | 8/62 | 14/0/23/53 | 4/1 |
| `unlock_reward_eng_xfl`, `unlock_reward_xfl` | 30 | 22 | 58 | 25/25/15 | 120 | 7/114 | 33/0/9/72 | 18/4 |
| `unlock_xfl` | 30 | 23 | 56 | 22/21/14 | 1000 | 8/62 | 14/0/23/53 | 4/1 |

Across the uncollapsed 59 rows this is 2,209 movie-clip definitions, 1,306 multi-frame definitions, 1,216 reachable multi-frame definitions, 932 reachable multi-frame definitions at depth 2+, 566 main-label placements, and 5,542 nested-label placements. These are corpus counts with language/reconstruction variants retained, not counts of unique original logical assets.

## Exact main-timeline labels

Frame indices are zero-based. Each bullet groups projects whose main-label name/index sequence is identical; the cited representative XML contains the full sequence.

- `common_eng_xfl`, `common_xfl`: `jf_com_ini`@0, `jf_com_title`@1, `jf_com_ranking`@2, `jf_com_all`@3 (`common_eng_xfl\DOMDocument.xml:327-351`).
- `custom2_eng_xfl`, `custom2_xfl`: `jf_ini`@0, `jf_bst_start`@1, `se_SE_WIN_OPEN`@2, `lf_bst_end`@4, `tg_bst_end`@8, `jf_custom_start`@9, `se_SE_INFO`@10, `tg_custom_start`@18, `jf_custom_end`@19, `se_SE_MENU_OUT`@20, `tg_custom_end`@28 (`custom2_eng_xfl\DOMDocument.xml:330-415`).
- `demo2_eng_rtm_xfl`, `demo2_eng_xfl`, `demo2_xfl`, `RECOVER_demo2`: `jf_ini`@0, `jf_title_start`@1, `tg_title_start`@19, `jf_title_end`@20, `tg_title_end`@39, `jf_tutorial_start`@40, `tg_tutorial_start`@41, `jf_tutorial_end`@42, `tg_tutorial_end`@43, `jf_list_start`@44, `jf_vs_start`@46, `jf_vs_end`@48, `tg_vs_end`@60, `jf_catch_start`@61, `tg_catch_start`@62, `jf_catch_end`@63, `tg_catch_end`@64 (`demo2_eng_rtm_xfl\DOMDocument.xml:135-290`).
- `event_reward_eng_xfl`, `event_reward_xfl`: `jf_ini`@0, `jf_start`@1, `tg_start`@8, `jf_end`@9, `tg_end`@18 (`event_reward_eng_xfl\DOMDocument.xml:164-202`).
- `gameover_eng_xfl`, `gameover_xfl`: `jf_ini`@0, `jf_gameover_start`@1, `tg_gameover_start`@2, `jf_gameover_end`@3, `tg_news_end`@4, `tg_gameover_end`@32 (`gameover_eng_xfl\DOMDocument.xml:122-161`).
- `hitchart_eng_xfl`, `hitchart_xfl`: `jf_30_init`@0, `jf_30_open`@1, `lf_30wait`@2, `jf_30_close`@3, `lf_30_end`@4, `jf_20_init`@5, `jf_20_open`@6, `lf_20wait`@7, `jf_20_close`@8, `lf_20_end`@9, `jf_10_init`@10, `jf_10_open`@11, `lf_10wait`@12, `jf_03_open`@13, `jf_02_open`@14, `jf_01_open`@15, `jf_all_close`@16, `lf_10_end`@17 (`hitchart_eng_xfl\DOMDocument.xml:136-317`).
- `matching_eng_xfl`, `matching_xfl`: `jf_ini`@0, `jf_match_start`@1, `se_SE_INFO`@2, `tg_pl_start`@3, `se_SE_WIN_OPEN`@4, `tg_match_start`@10, `jf_match_end`@11, `se_SE_VS_INFO`@12, `tg_match_end`@20 (`matching_eng_xfl\DOMDocument.xml:247-300`).
- `navi_001_yume_xfl`: `jf_ini`@0, `jf_ope_start`@1, `tg_ope_start`@11, `jf_ope_b_fi`@12, `tg_ope_b_fi`@13, `jf_ope_b_fo`@14, `tg_ope_b_fo`@15, `jf_ope_end`@16, `tg_ope_end`@25 (`navi_001_yume_xfl\DOMDocument.xml:134-204`).
- `net_eng_xfl`, `net_xfl`: `jf_ini`@0, `jf_save_start`@1, `se_SE_WIN_OPEN`@2, `tg_save_start`@20, `jf_save_end`@21, `SE_WIN_CLOSE`@22, `tg_save_end`@28 (`net_eng_xfl\DOMDocument.xml:119-163`).
- `ranking_eng_xfl`, `ranking_xfl`: `jf_rank_ini`@0, `jf_rank_start`@1, `se_SE_INFO`@2, `tg_rank_start`@15, `jf_rank_end`@16, `tg_rank_end`@30 (`ranking_eng_xfl\DOMDocument.xml:162-204`).
- `result_ehs_eng_xfl`, `result_ehs_xfl`: `jf_res_ini`@0, `jf_res_open`@1, `lf_res_record`@2, `jf_res_fullopen`@3, `lf_res_open2`@4, `jf_res_close`@5, `tg_res_close`@15 (`result_ehs_eng_xfl\DOMDocument.xml:225-285`).
- `result_eng_xfl`, `result_xfl`: `jf_ini`@0, `jf_playresult_start`@1, `se_SE_INFO`@2, `tg_playresult_start`@10, `jf_playresult_end`@11, `se_SE_STAGE_REWARD`@12, `tg_playresult_end`@20 (`result_eng_xfl\DOMDocument.xml:253-300`).
- `result_eve_eng_xfl`, `result_eve_xfl`: `jf_ini`@0, `jf_result_clear_start`@1, `se_SE_CLEAR`@2, `tg_result_clear_start`@3, `jf_result_fail_start`@4, `se_SE_FAIL`@5, `tg_result_fail_start`@6, `jf_everesult_load`@7, `tg_everesult_load`@8, `jf_everesult_start`@9, `se_SE_INFO`@10, `tg_everesult_start`@18, `jf_epbar_start`@19, `tg_epbar_start`@20, `jf_everesult_skip`@21, `jf_everesult_end`@22, `tg_everesult_end`@31 (`result_eve_eng_xfl\DOMDocument.xml:227-350`).
- `result_evesolo_eng_xfl`, `result_evesolo_xfl`: `jf_ini`@0, `jf_soloev_start`@1, `se_SE_INFO`@2, `jf_soloev_end`@9, `se_SE_STAGE_REWARD`@10, `tg_soloev_end`@18 (`result_evesolo_eng_xfl\DOMDocument.xml:362-412`).
- `result_local_eng_xfl`, `result_local_xfl`: `jf_ini`@0, `tg_ini`@1, `jf_totalresult_start`@2, `tg_playrank_1`@3, `se_SE_MENU_OUT`@4, `tg_playrank_2`@7, `tg_playrank_3`@11, `tg_playrank_4`@15, `tg_totalresult_start`@24, `jf_totalresult_end`@25, `se_SE_STAGE_REWARD`@26, `tg_totalresult_end`@34 (`result_local_eng_xfl\DOMDocument.xml:496-573`).
- `result_w_xfl`: `jf_ini`@0, `jf_result_clear_start`@1, `se_SE_CLEAR`@2, `tg_result_clear_start`@3, `jf_result_fail_start`@4, `se_SE_FAIL`@5, `tg_result_fail_start`@6, `jf_playresult_start`@7, `se_SE_WIN_OPEN`@8, `tg_playresult_start`@18, `jf_playresult_end`@19, `tg_playresult_end`@28 (`result_w_xfl\DOMDocument.xml:265-348`).
- `reward2_eng_xfl`, `reward2_xfl`: `jf_ini`@0, `jf_reward_start`@1, `se_SE_INFO`@2, `tg_reward_start`@3, `jf_reward_end`@4, `tg_reward_end`@13 (`reward2_eng_xfl\DOMDocument.xml:203-244`).
- `selectcourse_eng_xfl`, `selectcourse_xfl`: `jf_ini`@0, `jf_course_start`@1, `tg_course_start`@14, `jf_course_end`@15, `tg_course_end`@24 (`selectcourse_eng_xfl\DOMDocument.xml:213-252`).
- `selectgame2_eng_xfl`, `selectgame2_xfl`: `jf_ini`@0, `jf_slgame_start`@1, `se_SE_INFO`@2, `tg_slgame_start`@3, `jf_slgame_end`@4, `se_SE_MENU_OUT`@5, `tg_slgame_end`@13 (`selectgame2_eng_xfl\DOMDocument.xml:293-337`).
- `selectmode2_eng_xfl`, `selectmode2_xfl`: `jf_ini`@0, `jf_slmode_start`@1, `se_SE_INFO`@2, `se_VC_SELECT_MODE`@3, `tg_slmode_start`@10, `jf_slmode_end`@11, `se_SE_LETSGRV`@21, `se_VC_Lets_Groove`@22, `se_SE_STAGE_REWARD`@71, `tg_slmode_end`@80 (`selectmode2_eng_xfl\DOMDocument.xml:649-715`).
- `selectmode_eng_xfl`: `jf_ini`@0, `jf_slmode_start`@1, `se_SE_INFO`@2, `tg_slmode_start`@10, `jf_slmode_end`@11, `se_SE_LETSGRV`@21, `se_SE_STAGE_REWARD`@71, `tg_slmode_end`@80 (`selectmode_eng_xfl\DOMDocument.xml:545-605`).
- `selectmusic2_eng_xfl`, `selectmusic2_xfl`: `jf_ini`@0, `jf_slmusic_start`@1, `se_SE_INFO`@2, `se_VC_SELECT_MUSIC`@3, `tg_slmusic_start`@11, `jf_slmusic_end`@12, `se_SE_VS_INFO`@13, `tg_slmusic_end`@21 (`selectmusic2_eng_xfl\DOMDocument.xml:404-456`).
- `selectmusic_g_xfl`: `jf_g_ini`@0, `jf_g_start`@1, `se_SE_WIN_OPEN`@2, `tg_g_start`@19, `jf_g_dec`@20, `jf_g_end`@30, `tg_g_end`@44 (`selectmusic_g_xfl\DOMDocument.xml:229-291`).
- `signature_eng_xfl`, `signature_xfl`: `jf_ini`@0, `jf_notice_start`@1, `tg_notice_start`@30, `jf_notice_end`@31, `tg_notice_end`@60, `jf_tto_start`@61, `tg_VC2_009b`@62, `tg_tto_start`@90, `jf_tto_end`@91, `tg_tto_end`@120, `jf_zntt_start`@121, `tg_VC2_010`@122, `tg_zntt_start`@150, `jf_zntt_end`@151, `tg_zntt_end`@180 (`signature_eng_xfl\DOMDocument.xml:105-225`).
- `stamp_eng_xfl`, `stamp_xfl`: `jf_ini`@0, `jf_scard_start`@1, `se_SE_WIN_OPEN`@2, `tg_scard_start`@19, `jf_window_end`@20, `se_SE_WIN_CLOSE`@21, `tg_window_end`@29, `jf_change`@30, `se_SE_WIN_CLOSE`@31, `tg_change_reset`@39, `se_SE_WIN_OPEN`@40, `tg_change`@49, `jf_scard_end`@50, `se_SE_WIN_CLOSE`@51, `tg_scard_end`@59 (`stamp_eng_xfl\DOMDocument.xml:298-395`).
- `start2_eng_rtm_xfl`, `start2_eng_xfl`, `start2_xfl`: `jf_ini`@0, `jf_card_01`@1, `tg_card_01`@10, `jf_card_02`@11, `tg_card_02`@20, `jf_card_02a`@21, `jf_startup_end`@22, `tg_startup_end`@23, `jf_card_03_start`@24, `tg_card_03_start`@25, `jf_card_03_end`@26, `tg_card_03_end`@27 (`start2_eng_rtm_xfl\DOMDocument.xml:312-411`).
- `trophy_eng_xfl`, `trophy_xfl`: `jf_trophy_ini`@0, `jf_trophy_start`@1, `se_SE_EXTRA_OPEN`@2, `tg_trophy_dds`@25, `tg_trophy_start`@34, `jf_trophy_end`@35, `se_SE_WIN_CLOSE`@36, `tg_trophy_end`@54 (`trophy_eng_xfl\DOMDocument.xml:120-170`).
- `tutorial_eng_xfl`, `tutorial_xfl`: `jf_ini`@0, `jf_00_tutorial_start`@1, `jf_hit_start`@2, `jf_hit2_start`@3, `jf_hold_start`@4, `jf_critical_start`@5, `jf_slide_start`@6, `jf_11_other_start`@7, `jf_gage_start`@8 (`tutorial_eng_xfl\DOMDocument.xml:157-221`).
- `unlock_eng_xfl`, `unlock_xfl`: `jf_ini`@0, `jf_unlock_start`@1, `lf_unlock_start`@2, `jf_pget_end`@3, `tg_pget_end`@11, `jf_unlock_end`@12, `se_SE_VS_INFO`@13, `tg_unlock_end`@22 (`unlock_eng_xfl\DOMDocument.xml:335-395`).
- `unlock_reward_eng_xfl`, `unlock_reward_xfl`: `jf_ini`@0, `jf_unlock_start`@1, `se_SE_INFO`@2, `tg_unlock_start`@10, `jf_unlock_end`@11, `se_SE_VS_INFO`@12, `tg_unlock_end`@20 (`unlock_reward_eng_xfl\DOMDocument.xml:246-304`).

## Transition extraction checkpoint

### Action primitive totals and addressing

- Authored scripts contain **945 `gotoAndPlay`**, **84 `gotoAndStop`**, **1,753 bare `play()`**, and **4,012 bare `stop()`** calls. Main-timeline scripts contribute 238/16/174/300; nested movie-clip scripts contribute 707/68/1,579/3,712.
- No `nextFrame()`, `prevFrame()`, or literal `tellTarget` token occurs in the generated XML. The converter explicitly documents `tellTarget` conversion to `MovieClip(this.parent).path` and `MovieClip(root).path` (`CLAUDE.md:185`), so literal absence is not evidence that the original RVB action stream lacked target-addressed transitions.
- Of the 1,029 `gotoAnd*` calls, receiver forms are:
  - 531 current-playhead/bare calls, all in nested movie clips (for example `common_eng_xfl\LIBRARY\UNIQUE_10.xml:21`);
  - 323 `MovieClip(root)` calls: 236 from main scripts and 87 from nested scripts (for example `hitchart_xfl\DOMDocument.xml:140-163` and `common_xfl\LIBRARY\UNIQUE_196.xml:11-82`);
  - 104 `MovieClip(this.parent)` calls, all nested (for example `common_eng_xfl\LIBRARY\UNIQUE_130.xml:11-82`);
  - 42 `MovieClip(this.parent.parent)` calls, all nested (for example `navi_001_yume_xfl\LIBRARY\UNIQUE_23.xml:30-47`);
  - 18 direct `this.<child>` calls, all on main timelines (for example `hitchart_xfl\DOMDocument.xml:141`);
  - 11 generated `this....gotoAndPlay(...)` lines, all nested, in custom2, ranking, result_ehs, result_eve, and result_w variants (for example `custom2_xfl\LIBRARY\UNIQUE_249.xml:41`). This malformed-looking conversion residue must not be overinterpreted as the original action text.
- The only bare play/stop forms are exactly `play();` and `stop();`; no receiver-qualified `.play()` or `.stop()` was found. Representative nested calls occur at `navi_001_yume_xfl\LIBRARY\UNIQUE_9.xml:10-29`.
- In total, **498** `gotoAnd*` lines explicitly address another playhead. **244** of those execute from a library movie-clip frame script and address a root, parent, grandparent, sibling, or child playhead. This is direct authored evidence of cross-playhead orchestration.

### Script target catalog

`GP` and `GS` below enumerate every distinct literal/expression argument used by `gotoAndPlay` and `gotoAndStop`, grouped by identical target set. The per-project counts remain in the quantitative table.

- `common_eng_xfl`, `common_xfl`: GP `jf_dec_1p_guest`, `jf_dec_1p_you`, `jf_dec_2p_guest`, `jf_dec_2p_you`, `jf_dec_3p_guest`, `jf_dec_3p_you`, `jf_dec_4p_guest`, `jf_dec_4p_you`, `jf_hp_off`, `jf_hp_on`, `jf_istb_insert`, `jf_istb_nesica`, `jf_istb_play`, `jf_istb_xxx`, `jf_msg_start`, `lf_loop`, `lf_loop_insert`, `lf_loop_start`, `lf_now_loop`, `lf_vsw_loop`, `lf_vsw_online_loop`; GS `jf_1p_xxx`, `jf_2p_xxx`, `jf_3p_xxx`, `jf_4p_xxx`, `jf_rlt_board_1`, `lf_loop`, `lf_rlt_board_ini`.
- `custom2_eng_xfl`, `custom2_xfl`: GP `jf_gs0_start`, `jf_gs1_start`, `jf_gs2_start`, `jf_gs3_start`, `jf_tab_tri_on`, `lf_bst_end`, `lf_focus_loop`, `lf_loop`, `lf_ope_end`, `lf_ope_start`, `lf_title_custom_fo`; GS none.
- `demo2_eng_rtm_xfl`, `demo2_eng_xfl`, `demo2_xfl`, `RECOVER_demo2`: GP `lf_list_start`, `lf_vs_start`; GS none (`demo2_xfl\DOMDocument.xml:217,234`).
- `event_reward_eng_xfl`, `event_reward_xfl`: GP `jf_next_on`, `lf_rank_start`; GS none.
- `gameover_eng_xfl`, `gameover_xfl`: GP `jf_gmov_tx`, `lf_loop_off`, `lf_loop_on`; GS none.
- `hitchart_eng_xfl`, `hitchart_xfl`: GP `jf_base_n11-30_nd`, `jf_base_n11-30_rd`, `jf_base_n11-30_st`, `jf_base_n11-30_th`, `jf_base_n4-10_th`, `lf_01_open`, `lf_02_open`, `lf_03_open`, `lf_10_close`, `lf_10_open`, `lf_20_close`, `lf_20_open`, `lf_30_close`, `lf_30_open`, `lf_jtex_close`, `lf_jtex_init`, `lf_jtex_open`, `lf_title_close`, `lf_title_init`, `lf_title_open`, `play_loop`; GS `lf_10_init`, `lf_20_init`, `lf_30_init` (`hitchart_xfl\DOMDocument.xml:140-312`).
- `matching_eng_xfl`, `matching_xfl`: GP `jf_tri_loop`, `jf_yn_focus`, `lf_loop`, `lf_title_match_fo`, `tg_navi_end`, `tg_navi_start`; GS `jf_err_ini`.
- `navi_001_yume_xfl`: GP `lf_loop`, `lf_stay`; GS `lf_mouth_bc`, `lf_mouth_bc2`, `lf_mouth_ed`, `lf_mouth_ed2`, `lf_mouth_fc` (`navi_001_yume_xfl\LIBRARY\UNIQUE_23.xml:26-47`; `navi_001_yume_xfl\LIBRARY\UNIQUE_35.xml:19-55`).
- `net_eng_xfl`, `net_xfl`, `stamp_eng_xfl`, `stamp_xfl`, `trophy_eng_xfl`, `trophy_xfl`: GP `lf_loop`; GS none.
- `ranking_eng_xfl`, `ranking_xfl`: GP `jf_ctrl_on`, `jf_rank_end`, `lf_title_rank_fo`; GS none.
- `result_ehs_eng_xfl`, `result_ehs_xfl`: GP `lf_cname_fullopen`, `lf_cname_open`, `lf_ep_start`, `lf_exp_tex_close`, `lf_exp_tex_open`, `lf_loop`, `lf_res_open2`, `lf_res_record`, `lf_title_close`, `lf_title_open`, `new_rec_loop`; GS `lf_exp_tex_wait`, `lf_title_wait`.
- `result_eng_xfl`, `result_xfl`: GP `jf_fullchain_on`, `jf_newrec_on`, `jf_nomiss_on`, `lf_c_start`, `lf_loop`, `tg_navi_end`, `tg_navi_start`; GS none.
- `result_eve_eng_xfl`, `result_eve_xfl`: GP `lf_base1_start`, `lf_fail_end`, `lf_fail_start`, `lf_title_eve_fo`; GS `lf_basic_start`, `lf_res_ttl_adv`, `lf_res_ttl_bsc`, `lf_res_ttl_ext`, `tg_ep_start` (`result_eve_xfl\LIBRARY\UNIQUE_107.xml:30-33`; `result_eve_xfl\LIBRARY\UNIQUE_130.xml:11-29`).
- `result_evesolo_eng_xfl`, `result_evesolo_xfl`: GP `jf_fullchain_on`, `jf_newrec_on`, `jf_nomiss_on`, `lf_eresult_start`, `lf_loop`, `lf_title_evesolo`, `lf_title_evesolo_fo`, `tg_navi_end`, `tg_navi_start`; GS `jf_all_ini`, `lf_eff_star`, `lf_eff_text`.
- `result_local_eng_xfl`, `result_local_xfl`: GP `jf_newrec_on`, `lf_loop`, `lf_title_total`, `lf_title_total_fo`, `lf_total_star_mark_start`, `lf_totalstar_num_start`, `tg_navi_end`, `tg_navi_start`; GS `jf_ini`, `lf_total_star_mark_ini`, `lf_totalstar_num_ini` (`result_local_xfl\LIBRARY\UNIQUE_142.xml:7-50`; `result_local_xfl\LIBRARY\UNIQUE_145.xml:7-50`; `result_local_xfl\LIBRARY\UNIQUE_246.xml:29-30`).
- `result_w_xfl`: GP `jf_fullchain_on`, `jf_newrec_on`, `jf_nomiss_on`, `jf_play_clear`, `jf_play_failed`, `jf_title_play`, `jf_title_play_fo`, `lf_fail_end`, `lf_fail_start`, `lf_loop`, `lf_play_start`; GS none.
- `reward2_eng_xfl`, `reward2_xfl`: GP `lf_title_fo`, `tg_navi_end`, `tg_navi_start`; GS none.
- `selectcourse_eng_xfl`, `selectcourse_xfl`: GP `jf_tri_on`, `lf_loop`, `lf_title_course_fo`; GS `lf_focus`, `lf_notfocus`.
- `selectgame2_eng_xfl`, `selectgame2_xfl`: GP `jf_tri_loop`, `jf_tri_on`, `jf_yn_focus`, `lf_event_focus`, `lf_event_n_on`, `lf_focus_loop`, `lf_local_focus`, `lf_local_n_on`, `lf_loop`, `lf_solo_focus`, `lf_solo_n_on`, `lf_taikai_loop`, `lf_title_fo`, `lf_tutorial_focus`, `lf_tutorial_n_on`, `tg_navi_end`, `tg_navi_start`; GS none (`selectgame2_eng_xfl\LIBRARY\UNIQUE_191.xml:15-58`).
- `selectmode2_eng_xfl`, `selectmode2_xfl`, `selectmode_eng_xfl`: GP `jf_tab_tri_on`, `jf_title_mode_fo`, `lf_adlib_loop`, `lf_extra`, `lf_hard`, `lf_item_loop`, `lf_loop`, `lf_normal`, `lf_simple`, `lf_skin_loop`, `start`, `tg_navi_end`, `tg_navi_start`; GS none.
- `selectmusic2_eng_xfl`, `selectmusic2_xfl`: GP `lf_decs`, `lf_fd_focus`, `lf_focus_loop`, `lf_focus_start`, `lf_loop`, `lf_title_selectmusic_fo`, `lf_title_selectmusic_start`, `tg_navi_end`, `tg_navi_start`; GS `lf_focus_ini` (`selectmusic2_xfl\LIBRARY\UNIQUE_235.xml:148`; `selectmusic2_xfl\LIBRARY\UNIQUE_303.xml:35`).
- `selectmusic_g_xfl`: GP `lf_focus_start`, `lf_loop`, `lf_title_selectmusic_fo`; GS none.
- `signature_eng_xfl`, `signature_xfl`: no GP or GS targets.
- `start2_eng_rtm_xfl`, `start2_eng_xfl`, `start2_xfl`: GP numeric frame `1`, `jf_card_load_start`, `jf_loop_m`, `jf_loop_s`, `jf_nmentry_on`, `jf_tri_loop`, `jf_yn_focus`, `lf_loop`, `lf_name_ini`, `lf_title_gamestart`, `lf_title_gamestart_fo`, `lf_title_nameentry`, `lf_title_nameentry_fo`, `tg_card_01_start`; GS none (`start2_xfl\LIBRARY\UNIQUE_166.xml:15-488`).
- `tutorial_eng_xfl`, `tutorial_xfl`: GP `jf_t_auto`, `lf_loop`; GS none.
- `unlock_eng_xfl`, `unlock_xfl`: GP numeric frame `1`, `jf_on`, `jf_tri_down_on`, `jf_tri_up_on`, `lf_eff_t_start`, `lf_loop`, `lf_pget_end`, `lf_pget_start`, `lf_title_unlock_fo`; GS none. The numeric 1,000-frame loop is explicit at `unlock_xfl\LIBRARY\UNIQUE_99.xml:7-21`.
- `unlock_reward_eng_xfl`, `unlock_reward_xfl`: GP `jf_mc_un_frame_focus_fi`, `jf_mc_un_frame_focus_fo`, `jf_mc_un_unfocus2_dnfi`, `jf_mc_un_unfocus2_dnfo`, `jf_mc_un_unfocus2_upfi`, `jf_mc_un_unfocus2_upfo`, `jf_mc_un_unfocus_dnfi`, `jf_mc_un_unfocus_dnfo`, `jf_mc_un_unfocus_upfi`, `jf_mc_un_unfocus_upfo`, `lf_loop`, `lf_mc_un_arrow_D_anim_loop`, `lf_mc_un_arrow_U_anim_loop`, `lf_mc_un_ef_01_start`, `lf_mc_un_ef_02_start`, `lf_mc_un_ef_02_stop`, `lf_mc_un_frame_focus_loop`, `lf_mc_un_gctrans1_idle`, `lf_mc_un_gctrans2_idle`, `lf_mc_un_reward_idle`, `lf_mc_un_setumei_idle`, `lf_un_catselect_focus_01_loop` through `lf_un_catselect_focus_06_loop`, `tg_instmsg01_end`, `tg_instmsg01_start`, `tg_title_unlock_end`, `tg_title_unlock_start`; GS none.

## Significant symbol and instance names

### Main-timeline named instances

These are the distinct named symbol instances placed directly on each document timeline. They are useful correlation strings even when a script does not address them explicitly.

| Project(s) | Main-timeline instance names |
|---|---|
| `common_eng_xfl`, `common_xfl` | `imc_ctrl_anim`, `imc_demo_n`, `imc_error`, `imc_foot`, `imc_head`, `imc_hp`, `imc_insert`, `imc_message`, `imc_now`, `imc_prsn`, `imc_rlt`, `imc_vs` |
| `custom2_eng_xfl`, `custom2_xfl` | `imc_arrow_mc`, `imc_blank`, `imc_bst`, `imc_custom`, `imc_ope_all`, `imc_scroll_dds`, `imc_tab`, `imc_title` |
| demo2 variants and `RECOVER_demo2` | `imc_title`, `imc_vs`; scripts additionally address an unplaced `imc_list` |
| `event_reward_eng_xfl`, `event_reward_xfl` | `imc_er` |
| `gameover_eng_xfl`, `gameover_xfl` | `imc_gmov`, `imc_news_shape` |
| `hitchart_eng_xfl`, `hitchart_xfl` | `imc_10`, `imc_20`, `imc_30`, `imc_jtex`, `imc_title` |
| `matching_eng_xfl`, `matching_xfl` | `imc_dec_w`, `imc_navi`, `imc_pl1` through `imc_pl4`, `imc_sheep`, `imc_stop_yn`, `imc_title` |
| `navi_001_yume_xfl` | `imc_ope` |
| `net_eng_xfl`, `net_xfl` | none |
| `ranking_eng_xfl`, `ranking_xfl` | `imc_ctrl`, `imc_date`, `imc_ran`, `imc_sh`, `imc_title` |
| `result_ehs_eng_xfl`, `result_ehs_xfl` | `imc_event_res`, `imc_exp_tex`, `imc_score_res`, `imc_title` |
| `result_eng_xfl`, `result_xfl`, result_evesolo variants | `imc_music_info`, `imc_navi`, `imc_play`, `imc_playresult`, `imc_title`, `imc_tune` |
| `result_eve_eng_xfl`, `result_eve_xfl` | `imc_course`, `imc_everesult`, `imc_r_fail`, `imc_title` |
| `result_local_eng_xfl` | `imc_dds_shape`, `imc_ep`, `imc_navi`, `imc_rank1` through `imc_rank4`, `imc_title`, `imc_tune` |
| `result_local_xfl` | the same set plus `mc_classchange` |
| `result_w_xfl` | `imc_music_info`, `imc_playresult`, `imc_r_fail`, `imc_title`, `imc_tune` |
| `reward2_eng_xfl`, `reward2_xfl` | `imc_dds`, `imc_dds2`, `imc_leff_w`, `imc_music`, `imc_navi`, `imc_target`, `imc_title`, `imc_titletx`, `imc_total` |
| `selectcourse_eng_xfl`, `selectcourse_xfl` | `imc_focus2`, `imc_title` |
| `selectgame2_eng_xfl`, `selectgame2_xfl` | `imc_navi`, `imc_select`, `imc_taikaimode`, `imc_title` |
| selectmode variants | `imc_navi`, `imc_scroll_dds`, `imc_slmode`, `imc_tab`, `imc_title`, `imc_tune` |
| `selectmusic2_eng_xfl`, `selectmusic2_xfl` | `imc_focus`, `imc_navi`, `imc_other`, `imc_scroll_dds`, `imc_sort`, `imc_title`, `imc_tune` |
| `selectmusic_g_xfl` | `imc_focus`, `imc_title`, `imc_tune`, `imc_vs_base` |
| `signature_eng_xfl`, `signature_xfl` | none |
| `stamp_eng_xfl`, `stamp_xfl` | `imc_scard`, `imc_window` |
| start2 variants | `imc_card_01`, `imc_card_02`, `imc_name`, `imc_title` |
| `trophy_eng_xfl`, `trophy_xfl` | `imc_box_text`, `imc_ca`, `imc_coin`, `imc_trophy_next`, `imc_trophy_text` |
| `tutorial_eng_xfl`, `tutorial_xfl` | `imc_00_tutorial`, `imc_11_other`, `imc_critical`, `imc_gage`, `imc_hit`, `imc_hit2`, `imc_hold`, `imc_slide`, `imc_tag` |
| `unlock_eng_xfl`, `unlock_xfl` | `imc_cd_shape`, `imc_mgchange`, `imc_mget`, `imc_mp`, `imc_pget`, `imc_scroll`, `imc_title`, `imc_tracks` |
| `unlock_reward_eng_xfl`, `unlock_reward_xfl` | `imc_scroll`, `imc_title`, `imc_un_arrow_UD`, `imc_un_catselect_main`, `imc_un_focus`, `imc_un_gctrans`, `imc_un_gctrans2`, `imc_un_info_getkey`, `imc_un_navi`, `imc_un_reward`, `imc_un_setumei`, `imc_un_title`, and the four `imc_un_unfocus*` instances |

Representative placement evidence includes `common_xfl\DOMDocument.xml:415-581`, `selectgame2_xfl\DOMDocument.xml:361-524`, `selectmusic2_xfl\DOMDocument.xml:741-1206`, `start2_xfl\DOMDocument.xml:738-812`, and `unlock_reward_xfl\DOMDocument.xml:321-1307`.

### Named non-`UNIQUE_*` symbol definitions

These are the strongest XFL-side export/linkage correlation strings. `DOMSymbolItem` naming proves the XFL name; whether and how the native runtime resolves it remains outside this evidence stream.

- custom2 variants: `mc_avt_list_link`, `mc_index_list_link`, `mc_list_tri_link`, `mc_navi_list_link`, `mc_ttl_list_link` (for example `custom2_xfl\LIBRARY\mc_avt_list_link.xml:1`).
- demo2 variants and `RECOVER_demo2`: `mc_index_link`, `mc_list_base_link` (`demo2_xfl\LIBRARY\mc_index_link.xml:1`; `demo2_xfl\LIBRARY\mc_list_base_link.xml:1`).
- hitchart variants: `mc_rank01_link` (`hitchart_xfl\LIBRARY\mc_rank01_link.xml:1`).
- result_local variants: `mc_mr` (`result_local_xfl\LIBRARY\mc_mr.xml:1`).
- selectmode2 variants: `mc_adlib_list_link`, `mc_buy_window_link`, `mc_item_list_link`, `mc_item_list_tri_link`, `mc_skin_list_link`, `mc_skin_list_tri_link` (for example `selectmode2_xfl\LIBRARY\mc_buy_window_link.xml:1`).
- legacy `selectmode_eng_xfl`: the same list except `mc_buy_window_link`.
- selectmusic2 variants: `mc_index_link`, `mc_music_link` (`selectmusic2_xfl\LIBRARY\mc_music_link.xml:1`).
- start2 variants: `mc_name_cursor_m_link`, `mc_name_cursor_s_link` (`start2_xfl\LIBRARY\mc_name_cursor_m_link.xml:1`; `start2_xfl\LIBRARY\mc_name_cursor_s_link.xml:1`).
- unlock variants: `mc_panel_link` (`unlock_xfl\LIBRARY\mc_panel_link.xml:1`).

## Nested-playhead analysis checkpoint

### Reachability and label density

- **57 of 59** XFL projects have at least one statically reachable multi-frame movie clip. Only the two signature variants are document-playhead-only in the generated XFL: their 181-frame main timeline contains 15 labels but no multi-frame library timeline (`signature_xfl\DOMDocument.xml:105-225`).
- **55 of 59** have a reachable multi-frame clip at depth 2 or deeper. The trophy variants have three direct child movie clips but no multi-frame descendant below them; the signature variants have none. Every other project has a deep nested playhead.
- Reachable multi-frame reference depth reaches **5** in custom2, result_evesolo, result_local, selectgame2, selectmode/selectmode2, and selectmusic2 variants.
- Across variants, nested label placements break down as 3,305 `jf_*`, 1,071 `tg_*`, 691 `lf_*`, 408 `se_*`/`SE_*`, 59 literal `start`, and eight placements under `catchcopy_ini`, `play_loop`, and `new_rec_loop`. The table counts all 5,542 placements. There are 1,901 distinct nested label strings across the retained variants; the script target catalog above is the exhaustive subset actually passed to `gotoAnd*`.
- The main timeline can be short while descendant timelines are long. Examples include common (4 main frames vs 240 nested), tutorial (9 vs 240), selectgame2 (14 vs 147), event_reward (19 vs 92), and unlock (23 vs 1,000). The 1,000-frame unlock child plays and returns numerically to frame 1 (`unlock_xfl\LIBRARY\UNIQUE_99.xml:7-21`) and is instantiated beneath the root `imc_mget` symbol (`unlock_xfl\DOMDocument.xml:468-483`; `unlock_xfl\LIBRARY\UNIQUE_104.xml:1378-1645`).

### Static reachability is not the full possible set

Exactly 90 of the 1,306 multi-frame definitions are not reachable by following static `DOMSymbolInstance libraryItemName` edges from a document timeline. They occur only in these project families:

| Project(s) | Unreachable MF per project | Notable definitions |
|---|---:|---|
| custom2 variants | 13 | nine `UNIQUE_*` clips plus the multi-frame `mc_avt_list_link`, `mc_list_tri_link`, `mc_navi_list_link`, `mc_ttl_list_link` |
| selectmode2 variants | 18 | twelve `UNIQUE_*` clips plus all six named `mc_*_link` definitions, including 59-frame `mc_buy_window_link` |
| `selectmode_eng_xfl` | 12 | seven `UNIQUE_*` clips plus its five named `mc_*_link` definitions |
| selectmusic2 variants | 4 | three `UNIQUE_*` clips plus `mc_music_link` |
| start2 variants | 2 | 116-frame `mc_name_cursor_m_link` and `mc_name_cursor_s_link` |
| unlock variants | 1 | `mc_panel_link` (90 frames in English, 3 in non-English) |

Because these include named linkage-style definitions, “not statically reachable” must not be treated as “unused at runtime.” Dynamic native/ActionScript instantiation remains an explicit coverage question.

### Representative independent-playhead patterns

1. **Nested self-loop:** a library movie clip executes `play()` and later bare `gotoAndPlay("lf_loop")`, restarting its own playhead. This is the dominant pattern: 531 bare nested `gotoAnd*` calls; examples are `common_eng_xfl\LIBRARY\UNIQUE_10.xml:7-21` and the 300-frame `net_xfl\LIBRARY\UNIQUE_9.xml:7-21`.
2. **Nested-to-child/root control:** common’s nested `UNIQUE_196` rotates `jf_istb_insert/play/nesica/xxx` while issuing root-child transitions to `imc_insert.imc_insert_base` (`common_xfl\LIBRARY\UNIQUE_196.xml:7-82`).
3. **Grandparent/sibling fan-out:** selectgame2’s `jf_s_solo/local/event/tutorial` frames run from a nested clip and drive four sibling focus/idle clips through `MovieClip(this.parent.parent).imc_select...` (`selectgame2_eng_xfl\LIBRARY\UNIQUE_191.xml:15-58`).
4. **Deep child fan-out:** navi’s `jf_ope_mouth_talk` and `jf_ope_mouth_stay` independently send five mouth clips to `lf_loop` or `lf_stay` (`navi_001_yume_xfl\LIBRARY\UNIQUE_23.xml:26-47`). This exactly cross-checks E-036’s asset description, while E-036 still rejects it as the reported bottom-right runtime renderer.
5. **Parent coordination:** start2’s 277-frame card clip repeatedly sends its parent’s `imc_title` to `lf_title_gamestart_fo` at state exits (`start2_xfl\LIBRARY\UNIQUE_166.xml:53-488`).
6. **Parallel state selection:** result_eve sends four sibling result clips to `lf_basic_start` and separately switches a nested title among basic/advanced/extra labels with `gotoAndStop` (`result_eve_xfl\LIBRARY\UNIQUE_107.xml:30-33`; `result_eve_xfl\LIBRARY\UNIQUE_130.xml:11-29`).
7. **Parallel counters:** result_local sends `imc_totalstar_anim` and `imc_total_star` to distinct start labels from a nested frame (`result_local_xfl\LIBRARY\UNIQUE_246.xml:29-30`) while each target owns its own stop/reset labels (`result_local_xfl\LIBRARY\UNIQUE_142.xml:7-50`; `result_local_xfl\LIBRARY\UNIQUE_145.xml:7-50`).
8. **Main-driven child trees:** hitchart’s 18-frame document timeline targets `imc_10`, `imc_20`, `imc_30`, their per-rank children, `imc_jtex`, and `imc_title` with 45 explicit cross-playhead calls (`hitchart_xfl\DOMDocument.xml:140-312`).

A document-loop-only gate is therefore structurally insufficient as a completeness assumption: 57 projects author reachable child movie-clip timelines, 55 author depth-2+ timelines, and nested frame scripts can directly change other playheads. This is an XFL coverage risk statement, not proof of how the native scheduler dispatches those clips.

## Prioritized labels and instances for IDA/source correlation

1. **Core action primitives:** exact `play();`, `stop();`, `gotoAndPlay`, and `gotoAndStop` execution; current, root, parent, and grandparent addressing must be distinguished. `nextFrame`/`prevFrame` have no corpus examples.
2. **Attract/demo:** main labels `jf_list_start`, `jf_vs_start`, `jf_catch_start`; instances `imc_vs` and script-only `imc_list`; nested 226-frame catch-copy state machine `UNIQUE_40` (`demo2_xfl\DOMDocument.xml:135-290`; `demo2_xfl\LIBRARY\UNIQUE_40.xml:7-206`).
3. **Common shared UI:** `imc_insert`, `imc_message`, `imc_rlt`, `imc_vs`; `jf_istb_*`, `lf_loop_insert`, `lf_loop_start`, and parent/root-addressed result/message states (`common_xfl\DOMDocument.xml:415-581`; `common_xfl\LIBRARY\UNIQUE_196.xml:7-82`).
4. **Start/card/name entry:** `imc_card_01`, `imc_card_02`, `imc_name`, `imc_title`; `jf_card_load_*`, `jf_card_player_*`, `lf_title_gamestart_fo`; dynamic candidates `mc_name_cursor_m_link`/`mc_name_cursor_s_link`.
5. **Mode/game selection:** `imc_slmode`, `imc_select`, `imc_navi`, `imc_title`; `jf_slmode_start/end`, `jf_s_solo/local/event/tutorial`; exported list/buy-window names. These families have 54-59 deep reachable multi-frame definitions or 16 nested cross-calls.
6. **Music selection:** `imc_focus`, `imc_other`, `imc_sort`, `imc_navi`, `imc_title`; `lf_focus_ini`, `lf_focus_start`, `lf_decs`, `lf_fd_focus`; named `mc_music_link`.
7. **Results:** `imc_playresult`, `imc_everesult`, `imc_event_res`, `imc_score_res`, `imc_panel2`; `lf_basic_start`, `lf_res_ttl_*`, `lf_totalstar_num_*`, `lf_total_star_mark_*`, and `tg_navi_start/end`.
8. **Unlock:** root `imc_mget` and its 1,000-frame numeric child loop; `imc_un_focus`, `imc_un_reward`, `imc_un_setumei`, four `imc_un_unfocus*` instances; `lf_mc_un_*_idle/loop` targets; dynamic `mc_panel_link`.
9. **Navigator nested control:** `imc_ope.imc_ope_mouth.imc_ope_mouth_anim`, `jf_ope_mouth_talk/stay`, `lf_loop/stay`, and the five mouth child instances. Retain E-036’s warning that this asset is not the separately traced bottom-right native sprite renderer.
10. **Controls for matrix comparison:** signature variants represent main-only timelines; trophy variants represent main plus direct-only multi-frame children; net represents a small graph with a 300-frame nested self-loop.

## Confirmed facts

- The current corpus contains 59 XFL projects covering every one of its 57 raw RVB/MTX pairs plus two extra XFL projects.
- Every document declares `frameRate="30"`; this is metadata, not verified runtime cadence.
- Authored XML contains 1,029 `gotoAnd*` calls, 5,765 bare play/stop calls, 1,216 statically reachable multi-frame child definitions, and 244 nested scripts that explicitly target another playhead.
- Literal `nextFrame`, `prevFrame`, and `tellTarget` are absent from generated XML; root/parent converted addressing is abundant.
- Only signature has no reachable multi-frame child playhead; only signature and trophy lack a depth-2+ multi-frame child.
- Texture-only `menu` and `title` directories do not add timeline projects to this snapshot.

## Likely timing-risk patterns

- Self-looping child clips can continue to consume frame advances while a parent or main timeline is stopped.
- Nested scripts can start, stop-position, or restart siblings, descendants, parents, and root children independently of the document’s current frame.
- Very short document timelines can own deep child graphs with much longer spans, so document-frame counts are a poor proxy for update-domain coverage.
- Named, statically unreachable movie clips may be dynamically instantiated through linkage/export paths and would be missed by a static-root-only coverage model.
- Numeric-frame loops (`gotoAndPlay(1)`) and malformed converted upward paths (`this....gotoAndPlay`) require separate correlation from ordinary literal-label loops.

## Unresolved questions

- Does the native runtime advance every reachable depth through one shared MovieClip primitive, or are document, child, dynamically linked, and script-jump paths dispatched separately?
- Do `gotoAndPlay`/`gotoAndStop` immediately mutate a target clip through the same path as a one-frame advance, or bypass any existing cadence gate?
- Which native/export resolver instantiates the 90 statically unreachable multi-frame definitions?
- Demo2 scripts target `MovieClip(root).imc_list`, but no `name="imc_list"` placement exists anywhere in its generated `DOMDocument.xml` or library XML (`demo2_xfl\DOMDocument.xml:217`). Is it created dynamically from `mc_list_base_link`, supplied by native code, or lost in conversion?
- What original tellTarget/upward-addressing text produced the 11 `this....gotoAndPlay` residues? Generated XFL alone cannot answer this faithfully.
- Is `frameRate="30"` copied from RVB metadata or supplied by the converter, and how—if at all—does the native runtime use it?

## Recommended items for the root coverage matrix

- Track each family against four authored domains: document playhead, direct child MovieClip, depth-2+ MovieClip, and dynamically linked/unreachable MovieClip.
- Add separate coverage columns for one-frame advance, bare `play`/`stop` state change, `gotoAndPlay`, `gotoAndStop`, and root/parent/grandparent target resolution.
- Use representative stress cases: common, start2, selectmode2, selectgame2, selectmusic2, result_eve/result_local, unlock/unlock_reward, and navi; use signature, trophy, and net as structural controls.
- Record `frameRate=30` only under asset metadata. Do not use it as runtime cadence evidence without binary/runtime corroboration.
- Flag demo2 `imc_list`, statically unreachable named linkages, and `this....` conversion residues as unresolved rather than marking them covered or unused.
