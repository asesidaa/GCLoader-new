# Judgement Offset Advisor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. This project's approved flow is inline execution; do not dispatch subagents.

**Goal:** Add a read-only ConfigGUI utility that analyzes the latest loader-log.txt and recommends one absolute JudgTimeOffset when the real completed-stage evidence is sufficiently consistent.

**Architecture:** A focused ConfigGUI model module owns streaming log parsing, complete-stage validation, one-to-one ordinary-tap extraction, robust estimators, and the value-type result. Main.cpp owns only one-shot ImGui state and presentation immediately below Absolute-time judgement. The runtime judgement patch, diagnostic producer, configuration schema, and Test Mode editor remain unchanged.

**Tech Stack:** C++23, MSVC x86, std::expected, std::from_chars, std::format, ImGui, CMake, PowerShell 7 build/deployment scripts.

**Spec:** docs/superpowers/specs/2026-08-23-judgement-offset-advisor-design.md

## Global Constraints

- Work only in H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend, except for the explicitly environment-local PowerShell deployment script under H:\gc\temp.
- Use CLion MCP for repository reads, edits, and per-file diagnostics. Use native git commands for status, diff, staging, and commits.
- Preserve all pre-existing dirty worktree changes. Stage and commit only files owned by the current task.
- Do not modify iDmacDrv32 runtime code, absolute-time hooks, diagnostic output, config.toml schema, system.cfg, or Test Mode timing behavior.
- Analyze only loader-log.txt adjacent to the config.toml path used to launch ConfigGUI.
- The utility is read-only: it must never write an offset or mark the configuration dirty.
- Use the native stage-end score counters as the observed MISS/GOOD/COOL/GREAT authority.
- Use only complete, naturally ended, diagnostically trustworthy stages.
- Use only one-to-one ordinary pressed timings for calibration. Never assign the ordinary GREAT window to held, direction, component, duration, ad-lib, free-tap, or scoreless timing paths.
- The ordinary-tap GREAT interval remains exactly -33 through +33 ms.
- Reconstruct raw_error_ms as native_ms minus note_target_ms, independently of the offset used during play.
- Use exact integer and half-millisecond arithmetic. Do not accumulate floating-point time or search an arbitrary offset range.
- If any estimator has disconnected equal optima, show no suggestion and report: Data is too diverse to give a suggestion.
- Do not add try/catch. Ordinary file and parse failures return std::expected errors. Standard-library allocation failure is not translated and may terminate normally.
- Use std::format for new formatted strings. Do not introduce stringstream, snprintf, or hand-built formatting.
- Do not add synthetic gameplay tests, replay fixtures, an emulated judgement oracle, CTest targets, or arbitrary expected-value unit tests.
- Verification authority is the real zero-offset H:\gc\loader-log.txt plus complete MSVC x86 Debug and Release ConfigGUI builds.
- Ignore whitespace/newline-only differences and do not spend work normalizing them.
- Do not claim in-game or GUI acceptance until the user runs the deployed executable and reports the result.

---

## File Structure

### Product files

- Create: tools/ConfigGUI/JudgementOffsetAdvisor.h
  - Public value types and the one production analysis entry point.
  - No ImGui or runtime-patch dependencies.
- Create: tools/ConfigGUI/JudgementOffsetAdvisor.cpp
  - Streaming parser, lifecycle validation, eligibility checks, robust statistics, exact interval overlap, and recommendation.
- Modify: tools/ConfigGUI/CMakeLists.txt
  - Compile JudgementOffsetAdvisor.cpp into gc_config_gui_model.
- Modify: tools/ConfigGUI/Main.cpp
  - One-shot UI state, Analyze latest run button, summary rows, native totals, and per-song table.

### Environment-local file

- Create: H:\gc\temp\judgement-offset-advisor-inspect.cpp
  - Small console caller for the production analysis entry point; prints real-log results and contains no expected-value assertions.
- Create: H:\gc\temp\inspect-judgement-offset-advisor.ps1
  - Builds the console caller with the same MSVC x86 environment and runs it against a selected real log.
- Create: H:\gc\temp\deploy-config-gui.ps1
  - Back up and deploy only ConfigGUI.exe, verify hashes, and leave iDmacDrv32.dll untouched.
- These three files are intentionally outside git.

---

### Task 1: Implement the production judgement-offset analysis model

**Files:**
- Create: tools/ConfigGUI/JudgementOffsetAdvisor.h
- Create: tools/ConfigGUI/JudgementOffsetAdvisor.cpp
- Modify: tools/ConfigGUI/CMakeLists.txt

**Interfaces:**
- Consumes: existing AbsoluteJudgement log records headed by semantic-stage-open, absolute-stage-activation, scope-trace, semantic-stage-end, and semantic-stage-termination.
- Produces:

~~~cpp
namespace gc::config_gui {

struct NativeResultCounts {
    std::uint64_t miss{};
    std::uint64_t good{};
    std::uint64_t cool{};
    std::uint64_t great{};
};

struct JudgementOffsetSongStatistics {
    std::uint64_t stage_generation{};
    std::size_t eligible_judgements{};
    double median_error_before_offset_ms{};
    double median_absolute_deviation_ms{};
    NativeResultCounts native_results{};
};

enum class JudgementOffsetEstimatorRule : std::uint8_t {
    trim_5_percent,
    trim_7_5_percent,
    trim_10_percent,
    two_mad,
};

struct JudgementOffsetEstimatorResult {
    JudgementOffsetEstimatorRule rule{};
    std::size_t retained_samples{};
    std::size_t maximum_great{};
    std::optional<std::int32_t> centered_offset_ms;
};

struct JudgementOffsetEstimate {
    std::array<JudgementOffsetEstimatorResult, 4> estimators{};
    std::optional<std::int32_t> estimator_min_ms;
    std::optional<std::int32_t> estimator_max_ms;
    std::optional<std::int32_t> suggested_offset_ms;
    std::optional<std::size_t> projected_eligible_great;
    bool data_too_diverse{};
};

enum class ObservedGameplayOffsetKind : std::uint8_t {
    unavailable,
    uniform,
    varied,
};

struct ObservedGameplayOffset {
    ObservedGameplayOffsetKind kind{
        ObservedGameplayOffsetKind::unavailable};
    std::int32_t uniform_offset_ms{};
};

enum class JudgementOffsetSuggestionStrength : std::uint8_t {
    provisional,
    stable,
};

struct JudgementOffsetAnalysis {
    std::vector<JudgementOffsetSongStatistics> songs;
    NativeResultCounts native_results{};
    std::size_t eligible_judgements{};
    std::size_t observed_eligible_great{};
    ObservedGameplayOffset observed_gameplay_offset{};
    std::optional<JudgementOffsetEstimate> estimate;
    std::optional<JudgementOffsetSuggestionStrength> suggestion_strength;
};

enum class JudgementOffsetAdvisorErrorCode : std::uint8_t {
    cannot_open_log,
    log_read_failed,
    malformed_record,
};

struct JudgementOffsetAdvisorError {
    JudgementOffsetAdvisorErrorCode code{};
    std::size_t line{};
    std::string message;
};

[[nodiscard]] std::expected<
    JudgementOffsetAnalysis,
    JudgementOffsetAdvisorError>
AnalyzeJudgementOffsetLog(const std::filesystem::path& log_path);

} // namespace gc::config_gui
~~~

- Later tasks may read these value types but must not expose raw per-note records.

- [ ] **Step 1: Add the public value contract**

Create JudgementOffsetAdvisor.h with the exact interface above and only the required standard headers: array, cstddef, cstdint, expected, filesystem, optional, string, and vector.

Keep all model symbols in gc::config_gui. Do not include imgui.h or any runtime AbsoluteJudgement header; the log is the module boundary.

- [ ] **Step 2: Add exact field parsing primitives**

In JudgementOffsetAdvisor.cpp, add internal helpers under an anonymous namespace:

~~~cpp
struct RecordLocation {
    std::size_t line{};
    std::string_view body;
};

[[nodiscard]] std::expected<std::string_view, JudgementOffsetAdvisorError>
RequireField(
    RecordLocation record,
    std::string_view key);

template<std::integral T>
[[nodiscard]] std::expected<T, JudgementOffsetAdvisorError>
ParseDecimalField(
    RecordLocation record,
    std::string_view key);

[[nodiscard]] std::expected<std::uint64_t, JudgementOffsetAdvisorError>
ParseStageGeneration(RecordLocation record);
~~~

RequireField must locate key only at a token boundary, require the equals sign, and return the value through the next ASCII space or end of record. ParseDecimalField must use std::from_chars, require complete consumption, and return malformed_record with the source line and field name on missing, empty, overflowing, or non-decimal input.

Locate the payload by finding the exact marker AbsoluteJudgement: in each physical line. Ignore unrelated log lines. Read with std::ifstream and std::getline without enabling stream exceptions. Return cannot_open_log when opening fails and log_read_failed when badbit is set after iteration.

- [ ] **Step 3: Track arbitrary sequential stage transitions without a timeout**

Add an internal StageAccumulator and keep a vector in first-open order:

~~~cpp
struct TimingSample {
    std::int64_t raw_error_ms{};
    std::int32_t native_grade{};
};

struct StageAccumulator {
    std::uint64_t generation{};
    bool opened{};
    bool activated{};
    bool ended{};
    bool terminated_by_test_mode{};
    bool diagnostics_trustworthy{};
    NativeResultCounts native_results{};
    std::vector<TimingSample> eligible_samples;
    std::vector<std::int32_t> observed_offsets_ms;
};
~~~

Lifecycle rules:

1. semantic-stage-open creates exactly one accumulator for stage_generation.
2. absolute-stage-activation marks the matching opened, not-yet-ended stage.
3. scope-trace records attach to their exact stage_generation.
4. semantic-stage-end marks the matching stage ended, requires activated=1, reads cumulative score totals, and evaluates the diagnostic fields.
5. semantic-stage-termination source=test_mode_entry marks that same generation rejected even though the native termination path logs semantic-stage-end immediately before the termination marker.
6. A later open does not assume a fixed song count. Any prior open stage without a natural end remains incomplete and is ignored.
7. End-of-file with an open/active stage is not a parse failure; that stage simply does not contribute.
8. A relevant record with missing required fields, duplicate markers for the same generation, or impossible marker ordering returns malformed_record. It does not abort ConfigGUI.

A stage is diagnostically trustworthy only when all of these cumulative semantic-stage-end fields are exactly zero:

~~~text
cumulative_timing_grade_drops
cumulative_scope_trace_drops
cumulative_score_observation_read_failures
cumulative_score_counter_regressions
cumulative_final_accounting_mismatches
cumulative_clock_unavailable
cumulative_sequence_errors
cumulative_overload_drops
cumulative_cleanup_drops
cumulative_rounded_fallback
~~~

Read native results only from:

~~~text
cumulative_score_miss_delta
cumulative_score_good_delta
cumulative_score_cool_delta
cumulative_score_great_delta
~~~

A complete rejected or diagnostically untrustworthy stage contributes neither calibration samples nor displayed native totals. A trustworthy completed stage with zero eligible samples still contributes its native totals and song row.

- [ ] **Step 4: Extract complete scope entries and enforce one-to-one sample eligibility**

A scope-trace physical line may contain several entry_begin ... entry_end blocks. Walk every complete block within that line; do not assume one scope per line.

For every timing_begin ... timing_end inside a scope, parse recognition_ms, note_target_ms, signed_error_ms, and native_grade. Verify signed_error_ms equals recognition_ms minus note_target_ms. Record the observed applied offset as recognition_ms minus the enclosing scope's native_ms. If one accepted stage contains only one value, its run contribution is uniform; more than one distinct value makes the run display varied.

Add exactly one eligible TimingSample only when the enclosing entry proves all of the following:

~~~text
kind=event
scope_query_pressed_true=1
scope_query_held_true=0
scope_query_direction_nonzero=0
scope_timing_grade_calls=1
scope_timing_grade_records=1
scope_timing_grade_drops=0
exactly one scope_timing_begin block
scope_score_miss_delta + scope_score_good_delta +
scope_score_cool_delta + scope_score_great_delta = 1
~~~

Map native_grade 0, 1, 2, and 3 to MISS, GOOD, COOL, and GREAT respectively. Require the one nonzero score delta to match that grade. A mismatch makes the scope ineligible rather than inventing a result. Values outside 0 through 3 make the relevant record malformed for the supported fixed binary.

For an eligible timing, store:

~~~cpp
const auto raw_error_ms =
    enclosing_scope_native_ms - timing.note_target_ms;
~~~

Do not use signed_error_ms as raw error, because it already includes the gameplay offset. Do not expose the note address or individual sample in the public result.

- [ ] **Step 5: Implement exact per-stage descriptive statistics and filters**

Sort each complete stage's raw errors independently. Compute the standard median and median absolute deviation exactly in half-millisecond units:

~~~cpp
[[nodiscard]] std::int64_t MedianTwice(
    std::span<const std::int64_t> sorted_values);

[[nodiscard]] std::int64_t MedianAbsoluteDeviationTwice(
    std::span<const std::int64_t> sorted_values,
    std::int64_t median_twice);
~~~

For odd counts, MedianTwice is twice the middle integer. For even counts, it is the sum of the two middle integers. Convert only the public display values to double by dividing by 2.0; halves are exactly representable and do not feed candidate-time accumulation.

Produce four retained populations per stage before pooling:

~~~cpp
[[nodiscard]] std::vector<std::int64_t> TrimSymmetric(
    std::span<const std::int64_t> sorted_values,
    std::uint32_t fraction_numerator,
    std::uint32_t fraction_denominator);

[[nodiscard]] std::vector<std::int64_t> RetainWithinTwoMad(
    std::span<const std::int64_t> sorted_values,
    std::int64_t median_twice,
    std::int64_t mad_twice);
~~~

Use floor(n * numerator / denominator) on each side with rational pairs 5/100, 75/1000, and 10/100. For the MAD filter, retain e exactly when abs(2*e - median_twice) <= 2*mad_twice. Concatenate retained values across stages only after each stage has applied its own boundary.

- [ ] **Step 6: Implement exact maximum-GREAT overlap and centered selection**

For each retained raw error e, create the inclusive integer interval:

~~~text
[-33 - e, +33 - e]
~~~

Use signed 64-bit event coordinates. Add +1 at the lower endpoint and -1 at upper plus one, sort/coalesce events, and collect every closed segment with the global maximum overlap. Do not iterate over an arbitrary configured range.

For each maximum-overlap segment, find the integer subrange minimizing:

~~~text
sum(abs(raw_error_ms + candidate_offset_ms))
~~~

The unrestricted L1 minimizer is the median interval of negated retained errors. Clamp that interval to each maximum-overlap segment; if they do not intersect, the nearest segment endpoint is that segment's minimum. Evaluate the exact 64-bit loss at those candidates.

Selection rules:

1. A single connected globally minimum-loss candidate interval is valid.
2. Choose its arithmetic midpoint and round an exact half away from zero.
3. Two or more disconnected intervals with the same global minimum loss make that estimator ambiguous; centered_offset_ms is empty.
4. retained_samples and maximum_great remain populated even for an ambiguous estimator.

Implement the midpoint helper without floating point:

~~~cpp
[[nodiscard]] std::int64_t MidpointRoundedHalfAwayFromZero(
    std::int64_t lower,
    std::int64_t upper) noexcept;
~~~

- [ ] **Step 7: Assemble the four estimators and public analysis result**

Use the fixed estimator order trim 5%, trim 7.5%, trim 10%, and two MAD.

If any estimator is ambiguous:

- set estimate.data_too_diverse=true;
- leave estimator_min_ms, estimator_max_ms, suggested_offset_ms, and projected_eligible_great empty;
- preserve song, native-result, eligible-count, and estimator retained/maximum statistics.

Otherwise compute estimator_min_ms and estimator_max_ms from the four centered choices. When max minus min is at most 3 ms, sort the four choices and take the midpoint of the two middle integers with MidpointRoundedHalfAwayFromZero. When spread exceeds 3 ms, keep the range but leave the suggestion empty.

For a suggestion, project GREAT over every unfiltered eligible raw error with the exact -33 through +33 interval. Count observed eligible GREAT from the authoritative matched grade in TimingSample, not from all timing helper calls.

Set suggestion_strength only when a suggestion exists:

- one complete natural song: provisional;
- two or more complete natural songs: stable.

Observed gameplay offset is unavailable when accepted stages have no timing observations, uniform when every observed recognition_ms minus native_ms matches, and varied otherwise.

- [ ] **Step 8: Add the module to the existing ConfigGUI model target**

Modify tools/ConfigGUI/CMakeLists.txt:

~~~cmake
add_library(gc_config_gui_model STATIC
        AudioBackendEditorModel.cpp
        AudioOperationWorker.cpp
        InputEditorModel.cpp
        JudgementOffsetAdvisor.cpp
)
~~~

Do not add a new target, dependency, test, or runtime-library link.

- [ ] **Step 9: Run model diagnostics and the first build gate**

Request CLion diagnostics for:

~~~text
tools/ConfigGUI/JudgementOffsetAdvisor.h
tools/ConfigGUI/JudgementOffsetAdvisor.cpp
~~~

Resolve every error and every new warning attributable to these files. Then run:

~~~powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' -Preset msvc32-debug -Target ConfigGUI
~~~

Expected: CMake configure succeeds and the MSVC x86 Debug ConfigGUI target completes successfully.

Review only the functional diff for the three owned files. Do not normalize unrelated formatting or touch existing dirty files.

- [ ] **Step 10: Commit the analysis model**

Stage only:

~~~powershell
git add -- tools/ConfigGUI/JudgementOffsetAdvisor.h tools/ConfigGUI/JudgementOffsetAdvisor.cpp tools/ConfigGUI/CMakeLists.txt
git diff --cached --name-only
git commit -m "feat: add judgement offset analysis model"
~~~

Expected staged names: exactly the two new model files and tools/ConfigGUI/CMakeLists.txt.

---

### Task 2: Add the compact ConfigGUI presentation

**Files:**
- Modify: tools/ConfigGUI/Main.cpp

**Interfaces:**
- Consumes: gc::config_gui::AnalyzeJudgementOffsetLog and JudgementOffsetAnalysis from Task 1.
- Produces: one read-only Judgement offset advisor block below Absolute-time judgement.
- Does not produce configuration mutations, runtime state, background work, or persisted advisor state.

- [ ] **Step 1: Add presentation-only includes and state**

Include JudgementOffsetAdvisor.h and the standard format header. Add an anonymous-namespace UI state:

~~~cpp
struct JudgementOffsetAdvisorUiState {
    std::optional<gc::config_gui::JudgementOffsetAnalysis> analysis;
    std::string error;
};
~~~

Do not place UI strings or ImGui state in the analysis module.

- [ ] **Step 2: Add formatting helpers using std::format**

Add helpers that return strings for:

- signed integer milliseconds: -9 ms, +12 ms;
- exact half-millisecond song statistics without a trailing .0;
- estimator range: -10..-8 ms;
- native counts and ratios.

Use std::format exclusively for new formatted values. Feed completed strings to ImGui::TextUnformatted or ImGui::TextWrapped("%s", value.c_str()); do not add a stringstream.

- [ ] **Step 3: Add the one-shot analysis action**

Add a helper with this interface:

~~~cpp
void DrawJudgementOffsetAdvisor(
    const std::filesystem::path& log_path,
    JudgementOffsetAdvisorUiState& state);
~~~

Render the label Judgement offset advisor and button Analyze latest run. On click:

~~~cpp
auto result = gc::config_gui::AnalyzeJudgementOffsetLog(log_path);
if (result) {
    state.analysis = std::move(*result);
    state.error.clear();
} else {
    state.analysis.reset();
    state.error = result.error().message;
}
~~~

This call is synchronous and user-triggered. Do not add a watcher, worker thread, timer, retry, timeout, or fallback log path.

- [ ] **Step 4: Render all aggregate result states**

For a successful analysis, render these rows in this order:

~~~text
Suggested JudgTimeOffset
Estimator range
Last observed gameplay offset

Complete songs
Eligible judgements
Observed eligible GREAT
Projected eligible GREAT

Native results
MISS / GOOD / COOL / GREAT
~~~

Rules:

- suggested value exists: show the absolute integer value;
- one-song suggestion: append provisional to the suggestion value;
- two-or-more-song suggestion: show the value without a warning suffix;
- spread above 3 ms: show No suggestion while retaining the numeric range;
- disconnected equal optimum: show No suggestion, estimator range as Diverse, and exactly Data is too diverse to give a suggestion.;
- no eligible population: show No suggestion and Unavailable for range/projection;
- uniform observed offset: show its signed value;
- varied observed offsets: show Varied;
- no observed timing: show Unavailable;
- missing/unreadable/malformed log: show the expected error in the existing red inline-error style.

Never label the value as an adjustment and never write it into InputConfig.

- [ ] **Step 5: Render the per-song table**

Use an ImGui table with exactly these columns:

~~~text
Song
Samples
Median error before offset
MAD
MISS
GOOD
COOL
GREAT
~~~

Number rows 1 through N in accepted-stage order. Use the exact half-millisecond formatter for median and MAD. Show only aggregates; no expandable raw data, note addresses, timestamps, or tooltips that reinterpret grades.

- [ ] **Step 6: Place the block below Absolute-time judgement**

Extend DrawExperimental to receive:

~~~cpp
const std::filesystem::path& judgement_log_path,
JudgementOffsetAdvisorUiState& judgement_offset_advisor,
~~~

Call DrawJudgementOffsetAdvisor after the Absolute-time judgement checkbox, tooltip, and backend warning, before Timer freeze patches.

In main, derive the path once:

~~~cpp
const std::filesystem::path config_file_path{config_path};
const auto judgement_log_path =
    config_file_path.parent_path() / "loader-log.txt";
JudgementOffsetAdvisorUiState judgement_offset_advisor;
~~~

Pass both values into DrawExperimental. A relative config.toml therefore resolves to loader-log.txt in the current ConfigGUI directory; an absolute config path resolves beside that config.

The advisor must not modify dirty, save_status, InputConfig, or AudioOperationWorker.

- [ ] **Step 7: Run CLion diagnostics and both build gates**

Request CLion diagnostics for:

~~~text
tools/ConfigGUI/Main.cpp
tools/ConfigGUI/JudgementOffsetAdvisor.h
tools/ConfigGUI/JudgementOffsetAdvisor.cpp
~~~

Fix every error and every new warning caused by the touched code. Existing unrelated warnings outside this change remain out of scope.

Run:

~~~powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' -Preset msvc32-debug -Target ConfigGUI
& 'H:\gc\temp\build-asio-audio-backend.ps1' -Preset msvc32-release -Target ConfigGUI
~~~

Expected: both MSVC x86 ConfigGUI builds complete successfully.

Review the functional diff and verify by inspection that no code path writes config, starts background analysis, changes runtime logging, or touches iDmacDrv32.

- [ ] **Step 8: Commit the ConfigGUI presentation**

Stage only Main.cpp:

~~~powershell
git add -- tools/ConfigGUI/Main.cpp
git diff --cached --name-only
git commit -m "feat: show judgement offset advisor in ConfigGUI"
~~~

Expected staged name: exactly tools/ConfigGUI/Main.cpp.

---

### Task 3: Validate the real-log result and deploy ConfigGUI only

**Files:**
- Create outside git: H:\gc\temp\judgement-offset-advisor-inspect.cpp
- Create outside git: H:\gc\temp\inspect-judgement-offset-advisor.ps1
- Create outside git: H:\gc\temp\deploy-config-gui.ps1
- Read as runtime authority: H:\gc\loader-log.txt
- Deploy: H:\gc\ConfigGUI.exe

**Interfaces:**
- Consumes: build-msvc32-release\dist\ConfigGUI.exe from Task 2.
- Produces: backed-up, hash-verified deployed ConfigGUI.exe.
- Leaves H:\gc\iDmacDrv32.dll byte-for-byte untouched.

- [ ] **Step 1: Create the real-log production-analysis inspector**

Create H:\gc\temp\judgement-offset-advisor-inspect.cpp. It must include the production JudgementOffsetAdvisor.h, accept exactly one log path, call AnalyzeJudgementOffsetLog, and print:

~~~text
complete_songs
eligible_judgements
observed_eligible_great
native MISS/GOOD/COOL/GREAT
observed gameplay offset state/value
each estimator rule, retained count, maximum GREAT, and centered value/ambiguous
estimator range
suggested absolute offset
projected eligible GREAT
each song's sample count, median, MAD, and native totals
~~~

Use std::format and std::cout. Return 2 for a wrong argument count, 1 for an expected analysis error, and 0 for a successful analysis. Do not add try/catch, expected-value assertions, fabricated log records, or duplicated analysis logic.

~~~cpp
#include "JudgementOffsetAdvisor.h"

#include <array>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::string FormatOptional(const std::optional<std::int32_t> value) {
    return value ? std::format("{}", *value) : "none";
}

const char* RuleName(
    const gc::config_gui::JudgementOffsetEstimatorRule rule) noexcept {
    using Rule = gc::config_gui::JudgementOffsetEstimatorRule;
    switch (rule) {
    case Rule::trim_5_percent: return "trim_5_percent";
    case Rule::trim_7_5_percent: return "trim_7_5_percent";
    case Rule::trim_10_percent: return "trim_10_percent";
    case Rule::two_mad: return "two_mad";
    }
    return "invalid";
}

} // namespace

int wmain(const int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: judgement-offset-advisor-inspect <loader-log.txt>\n";
        return 2;
    }

    auto analyzed = gc::config_gui::AnalyzeJudgementOffsetLog(
        std::filesystem::path{argv[1]});
    if (!analyzed) {
        std::cerr << std::format(
            "analysis_error line={} message={}\n",
            analyzed.error().line,
            analyzed.error().message);
        return 1;
    }

    const auto& result = *analyzed;
    std::cout << std::format(
        "complete_songs={} eligible_judgements={} observed_eligible_great={}\n"
        "native miss={} good={} cool={} great={}\n",
        result.songs.size(),
        result.eligible_judgements,
        result.observed_eligible_great,
        result.native_results.miss,
        result.native_results.good,
        result.native_results.cool,
        result.native_results.great);

    using OffsetKind = gc::config_gui::ObservedGameplayOffsetKind;
    switch (result.observed_gameplay_offset.kind) {
    case OffsetKind::unavailable:
        std::cout << "observed_gameplay_offset=unavailable\n";
        break;
    case OffsetKind::uniform:
        std::cout << std::format(
            "observed_gameplay_offset={}\n",
            result.observed_gameplay_offset.uniform_offset_ms);
        break;
    case OffsetKind::varied:
        std::cout << "observed_gameplay_offset=varied\n";
        break;
    }

    if (!result.estimate) {
        std::cout << "estimate=unavailable\n";
    } else {
        for (const auto& estimator : result.estimate->estimators) {
            std::cout << std::format(
                "estimator rule={} retained={} maximum_great={} centered={}\n",
                RuleName(estimator.rule),
                estimator.retained_samples,
                estimator.maximum_great,
                FormatOptional(estimator.centered_offset_ms));
        }
        std::cout << std::format(
            "estimator_min={} estimator_max={} suggested={} projected={} diverse={}\n",
            FormatOptional(result.estimate->estimator_min_ms),
            FormatOptional(result.estimate->estimator_max_ms),
            FormatOptional(result.estimate->suggested_offset_ms),
            result.estimate->projected_eligible_great
                ? std::format("{}", *result.estimate->projected_eligible_great)
                : "none",
            result.estimate->data_too_diverse ? 1 : 0);
    }

    for (std::size_t index = 0; index < result.songs.size(); ++index) {
        const auto& song = result.songs[index];
        std::cout << std::format(
            "song={} samples={} median={} mad={} miss={} good={} cool={} great={}\n",
            index + 1,
            song.eligible_judgements,
            song.median_error_before_offset_ms,
            song.median_absolute_deviation_ms,
            song.native_results.miss,
            song.native_results.good,
            song.native_results.cool,
            song.native_results.great);
    }
    return 0;
}
~~~

Create H:\gc\temp\inspect-judgement-offset-advisor.ps1 with this exact build flow:

~~~powershell
[CmdletBinding()]
param(
    [string] $LogPath = 'H:\gc\loader-log.txt',
    [string] $RepoRoot =
        'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend'
)

$ErrorActionPreference = 'Stop'
$vsDevShell =
    'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\Launch-VsDevShell.ps1'
$probeSource = 'H:\gc\temp\judgement-offset-advisor-inspect.cpp'
$outputRoot = 'H:\gc\temp\judgement-offset-advisor-inspect'
$probeExe = Join-Path $outputRoot 'judgement-offset-advisor-inspect.exe'

& $vsDevShell -Arch x86 -HostArch x86 -SkipAutomaticLocation
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$null = New-Item -ItemType Directory -Path $outputRoot -Force
& cl.exe /nologo /std:c++latest /permissive- /Zc:__cplusplus /EHsc /W4 `
    /I"$RepoRoot\tools\ConfigGUI" `
    $probeSource `
    "$RepoRoot\tools\ConfigGUI\JudgementOffsetAdvisor.cpp" `
    /Fe:$probeExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $probeExe $LogPath
exit $LASTEXITCODE
~~~

This is a real-log inspection path for the production module, not a gameplay test or a second estimator implementation.

- [ ] **Step 2: Run the production model against the real zero-offset log**

Run:

~~~powershell
& 'H:\gc\temp\inspect-judgement-offset-advisor.ps1' -LogPath 'H:\gc\loader-log.txt'
~~~

Compare its output with the exact oracle in Step 6 below. If any lifecycle, count, estimator, or projection differs, treat the implementation as unverified and correct the production parser/estimator before deployment. Do not change the oracle to match the implementation.

- [ ] **Step 3: Create the persisted GUI-only deployment script**

Create H:\gc\temp\deploy-config-gui.ps1 with this behavior:

~~~powershell
[CmdletBinding()]
param(
    [string] $Candidate =
        'H:\gc\artifacts\GCLoader\.worktrees\asio-audio-backend\build-msvc32-release\dist\ConfigGUI.exe',
    [string] $Destination = 'H:\gc\ConfigGUI.exe',
    [string] $BackupRoot = 'H:\gc\deploy-backups'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
    throw "Candidate ConfigGUI not found: $Candidate"
}
if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
    throw "Deployed ConfigGUI not found: $Destination"
}

$blocking = @(Get-Process -Name 'ConfigGUI' -ErrorAction SilentlyContinue)
if ($blocking.Count -ne 0) {
    $ids = $blocking.Id -join ', '
    throw "Deployment refused while ConfigGUI is running: $ids"
}

$null = New-Item -ItemType Directory -Path $BackupRoot -Force
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmssfff'
$backup = Join-Path $BackupRoot "ConfigGUI-$timestamp.exe"
Copy-Item -LiteralPath $Destination -Destination $backup

$candidateHash =
    (Get-FileHash -Algorithm SHA256 -LiteralPath $Candidate).Hash
Copy-Item -LiteralPath $Candidate -Destination $Destination -Force
$deployedHash =
    (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash

if ($candidateHash -ne $deployedHash) {
    throw 'Deployed ConfigGUI hash does not match the Release candidate'
}

"Candidate ConfigGUI SHA256: $candidateHash"
"Deployed ConfigGUI SHA256:  $deployedHash"
"Backup path: $backup"
~~~

This is an environment utility, not repository source. Do not stage it.

- [ ] **Step 4: Re-run the final Release build and inspect ownership**

Run:

~~~powershell
& 'H:\gc\temp\build-asio-audio-backend.ps1' -Preset msvc32-release -Target ConfigGUI
git status --short
git log -2 --oneline
~~~

Expected: Release build succeeds; the two feature commits exist; all unrelated pre-existing dirty files remain preserved and unstaged.

- [ ] **Step 5: Deploy only ConfigGUI**

Run:

~~~powershell
& 'H:\gc\temp\deploy-config-gui.ps1'
~~~

Expected: candidate and deployed ConfigGUI SHA-256 values are identical, a timestamped backup path is printed, and iDmacDrv32.dll is not read or written by the script.

- [ ] **Step 6: Preserve the exact real-log acceptance oracle**

Do not synthesize another log. The deployed advisor must be checked against H:\gc\loader-log.txt from the finalized zero-offset run.

Expected accepted stages and aggregate output:

~~~text
Complete songs                 2
Eligible judgements            710
Observed eligible GREAT        642 / 710
Projected eligible GREAT       659 / 710
Native results                 MISS 0  GOOD 8  COOL 76  GREAT 873
Last observed gameplay offset  0 ms
Estimator range                -10..-8 ms
Suggested JudgTimeOffset       -9 ms
~~~

Expected per-song rows:

~~~text
Song 1  Samples 349  Median +12 ms  MAD 12 ms  MISS 0  GOOD 3  COOL 41  GREAT 405
Song 2  Samples 361  Median  +7 ms  MAD 12 ms  MISS 0  GOOD 5  COOL 35  GREAT 468
~~~

The four centered estimator values are -8, -9, -9, and -10 ms in fixed rule order.

These values come from a naturally played, complete two-song binary run. They are acceptance evidence, not an invented judgement emulator.

- [ ] **Step 7: Hand off GUI/runtime acceptance without overstating proof**

Report:

- Debug and Release build results;
- CLion diagnostic result for every touched source;
- both feature commit hashes;
- deployed ConfigGUI hash and backup path;
- that the runtime DLL was untouched;
- that real GUI acceptance remains user-owned.

The user then opens H:\gc\ConfigGUI.exe with the normal H:\gc\config.toml, selects Analyze latest run, and compares the displayed statistics with Step 6. Do not claim this UI observation or a subsequent gameplay session before the user reports it.
