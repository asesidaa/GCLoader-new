from __future__ import annotations

from array import array
import json
from pathlib import Path
import tempfile
import unittest
import wave

from tools.analysis.audio_replay_analyzer import (
    Pcm16Wave,
    ReplayCandidate,
    SessionAnalysis,
    analyze_shared_song_clock,
    analyze_session,
    read_checkpointed_pcm16,
    read_timeline,
    scan_replay_candidates,
    write_analysis_outputs,
)


SAMPLE_RATE = 48_000
ARTIFACT_SECONDS = (3, 7, 11)


def deterministic_noise(seconds: int = 16) -> list[tuple[int, int]]:
    state = 0x5A17C0DE
    frames: list[tuple[int, int]] = []
    for _ in range(seconds * SAMPLE_RATE):
        state = (1_664_525 * state + 1_013_904_223) & 0xFFFFFFFF
        left = ((state >> 8) & 0xFFFF) - 32_768
        state = (1_664_525 * state + 1_013_904_223) & 0xFFFFFFFF
        right = ((state >> 8) & 0xFFFF) - 32_768
        frames.append((left, right))
    return frames


def inject_rewind(
    frames: list[tuple[int, int]],
    start_frame: int,
    milliseconds: int,
) -> None:
    count = SAMPLE_RATE * milliseconds // 1_000
    frames[start_frame : start_frame + count] = list(
        frames[start_frame - count : start_frame]
    )


def inject_crossfaded_replay(
    frames: list[tuple[int, int]],
    start_frame: int,
) -> None:
    count = SAMPLE_RATE * 50 // 1_000
    fade = SAMPLE_RATE * 10 // 1_000
    original = list(frames[start_frame : start_frame + count])
    replay = list(frames[start_frame - count : start_frame])
    blended: list[tuple[int, int]] = []
    for index, (replayed, untouched) in enumerate(zip(replay, original)):
        if index < fade:
            replay_weight = index / fade
        elif index >= count - fade:
            replay_weight = (count - index - 1) / fade
        else:
            replay_weight = 1.0
        original_weight = 1.0 - replay_weight
        blended.append(
            (
                round(replayed[0] * replay_weight +
                      untouched[0] * original_weight),
                round(replayed[1] * replay_weight +
                      untouched[1] * original_weight),
            )
        )
    frames[start_frame : start_frame + count] = blended


def write_pcm16_wave(
    path: Path,
    frames: list[tuple[int, int]],
    sample_rate: int = SAMPLE_RATE,
    channels: int = 2,
    sample_width: int = 2,
) -> None:
    samples = array("h")
    if channels == 1:
        samples.extend(frame[0] for frame in frames)
    else:
        for frame in frames:
            samples.extend(frame)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(channels)
        output.setsampwidth(sample_width)
        output.setframerate(sample_rate)
        if sample_width == 2:
            output.writeframes(samples.tobytes())
        else:
            output.writeframes(
                bytes((sample + 32_768) >> 8 for sample in samples)
            )


def assert_candidates_near(
    case: unittest.TestCase,
    candidates: tuple[ReplayCandidate, ...] | list[ReplayCandidate],
) -> None:
    starts = [candidate.start_frame for candidate in candidates]
    for seconds in ARTIFACT_SECONDS:
        expected = seconds * SAMPLE_RATE
        case.assertTrue(
            any(abs(start - expected) <= SAMPLE_RATE * 20 // 1_000
                for start in starts),
            f"no candidate within 20 ms of {seconds} seconds: {starts}",
        )


def write_session(
    directory: Path,
    frames: list[tuple[int, int]],
    *,
    sample_rate: int = SAMPLE_RATE,
    timeline: list[dict[str, object]] | None = None,
    schema_version: int = 1,
) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    write_pcm16_wave(
        directory / "submitted.wav", frames, sample_rate=sample_rate
    )
    (directory / "session.json").write_text(
        json.dumps(
            {
                "schema_version": schema_version,
                "sample_rate": sample_rate,
                "channels": 2,
                "bits_per_sample": 16,
                "frames_per_block": max(1, sample_rate // 100),
                "qpc_frequency": 1_000_000,
                "maximum_seconds": 1_800,
            }
        ),
        encoding="utf-8",
    )
    records = list(timeline or ())
    records.append(
        {
            "kind": "checkpoint",
            "pcm_sequence": max(0, len(frames) // max(1, sample_rate // 100) - 1),
            "event_sequence": 0,
            "wav_data_bytes": len(frames) * 4,
            "dropped_pcm_blocks": 0,
            "lost_events": 0,
            "conclusive": True,
        }
    )
    (directory / "timeline.jsonl").write_text(
        "".join(json.dumps(record) + "\n" for record in records),
        encoding="utf-8",
    )


class WaveParsingTests(unittest.TestCase):
    def test_reads_only_complete_checkpointed_pcm16_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "capture.wav"
            frames = [(index, -index) for index in range(10)]
            write_pcm16_wave(path, frames)
            parsed = read_checkpointed_pcm16(path, 18)
            self.assertEqual(parsed.sample_rate, SAMPLE_RATE)
            self.assertEqual(parsed.channels, 2)
            self.assertEqual(list(parsed.frames), frames[:4])
            with self.assertRaises(ValueError):
                read_checkpointed_pcm16(path, len(frames) * 4 + 4)

    def test_rejects_non_pcm16_or_non_stereo_input(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            frames = [(100, -100)] * 8
            mono = root / "mono.wav"
            write_pcm16_wave(mono, frames, channels=1)
            with self.assertRaises(ValueError):
                read_checkpointed_pcm16(mono, 16)

            pcm8 = root / "pcm8.wav"
            write_pcm16_wave(pcm8, frames, sample_width=1)
            with self.assertRaises(ValueError):
                read_checkpointed_pcm16(pcm8, 16)

    def test_rejects_malformed_jsonl_and_schema_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            malformed = root / "malformed.jsonl"
            malformed.write_text('{"kind":\n', encoding="utf-8")
            with self.assertRaises(ValueError):
                read_timeline(malformed)

            non_monotonic = root / "non-monotonic.jsonl"
            non_monotonic.write_text(
                json.dumps(
                    {
                        "kind": "checkpoint",
                        "pcm_sequence": 2,
                        "event_sequence": 2,
                        "wav_data_bytes": 16,
                        "conclusive": True,
                    }
                )
                + "\n"
                + json.dumps(
                    {
                        "kind": "checkpoint",
                        "pcm_sequence": 1,
                        "event_sequence": 2,
                        "wav_data_bytes": 12,
                        "conclusive": True,
                    }
                )
                + "\n",
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                read_timeline(non_monotonic)

            session = root / "session"
            write_session(
                session,
                [(0, 0)] * 16,
                schema_version=2,
            )
            with self.assertRaises(ValueError):
                analyze_session(session)


class ReplayDetectionTests(unittest.TestCase):
    def test_clean_noise_has_no_replay_candidate(self) -> None:
        wave_data = Pcm16Wave(
            SAMPLE_RATE, 2, deterministic_noise()
        )
        self.assertEqual(scan_replay_candidates(wave_data), ())

    def test_detects_40ms_source_rewind(self) -> None:
        frames = deterministic_noise()
        for seconds in ARTIFACT_SECONDS:
            inject_rewind(frames, seconds * SAMPLE_RATE, 40)
        candidates = scan_replay_candidates(
            Pcm16Wave(SAMPLE_RATE, 2, frames)
        )
        assert_candidates_near(self, candidates)
        self.assertTrue(
            all(candidate.correlation >= 0.97 for candidate in candidates)
        )

    def test_detects_50ms_edge_crossfaded_replay(self) -> None:
        frames = deterministic_noise()
        for seconds in ARTIFACT_SECONDS:
            inject_crossfaded_replay(frames, seconds * SAMPLE_RATE)
        candidates = scan_replay_candidates(
            Pcm16Wave(SAMPLE_RATE, 2, frames)
        )
        assert_candidates_near(self, candidates)

    def test_ignores_candidates_overlapping_pcm_gaps(self) -> None:
        frames = deterministic_noise()
        start = 3 * SAMPLE_RATE
        inject_rewind(frames, start, 40)
        candidates = scan_replay_candidates(
            Pcm16Wave(SAMPLE_RATE, 2, frames),
            (
                {
                    "kind": "pcm_gap",
                    "output_frame_begin": start - SAMPLE_RATE // 10,
                    "output_frame_end": start + SAMPLE_RATE // 10,
                    "conclusive": False,
                },
            ),
        )
        self.assertFalse(
            any(abs(candidate.start_frame - start) <= SAMPLE_RATE // 10
                for candidate in candidates)
        )

    def test_correlates_seek_and_resync_events_within_250ms(self) -> None:
        frames = deterministic_noise()
        start = 3 * SAMPLE_RATE
        inject_rewind(frames, start, 40)
        timeline = (
            {
                "kind": "session_metadata",
                "qpc_frequency": 1_000_000,
                "frames_per_block": 480,
            },
            {
                "kind": "endpoint_block",
                "pcm_sequence": start // 480,
                "output_frame_begin": start,
                "submitted_tail": start + 480,
                "endpoint_qpc_100ns": 30_000_000,
            },
            {
                "kind": "seek_applied",
                "output_frame_begin": start - 1_000,
                "source_frame_begin": 20_000,
                "source_frame_end": 18_080,
                "qpc_ticks": 0,
            },
            {
                "kind": "audio_resync",
                "qpc_ticks": 3_000_000,
                "signed_value0": 63,
                "signed_value1": 48,
                "resync_decision": "allowed_out_of_margin",
            },
        )
        candidates = scan_replay_candidates(
            Pcm16Wave(SAMPLE_RATE, 2, frames), timeline
        )
        candidate = min(
            candidates,
            key=lambda value: abs(value.start_frame - start),
        )
        kinds = {event["kind"] for event in candidate.causal_events}
        self.assertIn("seek_applied", kinds)
        self.assertIn("audio_resync", kinds)


class SharedSongClockTests(unittest.TestCase):
    def test_decodes_signed_desired_tick_bits(self) -> None:
        summary = analyze_shared_song_clock(
            (
                {
                    "kind": "gameplay_song_clock",
                    "cursor_source": "rounded",
                    "value0": 0,
                    "value1": (-3) & ((1 << 64) - 1),
                    "value2": 0,
                },
            )
        )
        self.assertEqual(summary.maximum_absolute_tick_error, 3)

    def test_summarizes_fractional_corrections_and_anomalies(self) -> None:
        def clock_event(
            source: str,
            current: int,
            desired: int,
            step: int,
            *,
            backlog: int = 0,
            generation: int = 0,
            output: int = 0,
            source_frame: int = 0,
            flags: int = 0,
        ) -> dict[str, object]:
            return {
                "kind": "gameplay_song_clock",
                "cursor_source": source,
                "flags": flags,
                "generation": generation,
                "output_frame_begin": output,
                "source_frame_begin": source_frame,
                "value0": current,
                "value1": desired & ((1 << 64) - 1),
                "value2": (step << 32) | backlog,
                "value3": step,
            }

        timeline = (
            clock_event(
                "exact",
                100,
                102,
                2,
                generation=1,
                output=1_000,
                source_frame=10_000,
                flags=0x1,
            ),
            clock_event("rounded", 200, 200, 0, output=1_050),
            clock_event("rounded", 200, 201, 1, output=1_100),
            clock_event(
                "rounded", 201, 203, 2, backlog=3, output=1_150
            ),
            clock_event(
                "invalid",
                203,
                0,
                1,
                generation=1,
                output=1_200,
                source_frame=9_000,
                flags=0x2,
            ),
            {
                "kind": "seek_applied",
                "output_frame_begin": 1_225,
                "source_frame_begin": 20_000,
                "source_frame_end": 16_957,
            },
            clock_event(
                "exact",
                203,
                204,
                1,
                generation=2,
                output=1_300,
                source_frame=10_500,
                flags=0x1,
            ),
        )
        replay = ReplayCandidate(
            start_frame=1_225,
            lag_frames=3_312,
            window_frames=2_400,
            correlation=0.999,
            normalized_error=0.01,
            causal_events=(),
        )

        summary = analyze_shared_song_clock(
            timeline, (replay,), SAMPLE_RATE
        )

        self.assertEqual(summary.total_observations, 6)
        self.assertEqual(summary.exact, 2)
        self.assertEqual(summary.rounded, 3)
        self.assertEqual(summary.invalid, 1)
        self.assertEqual(summary.step_zero, 1)
        self.assertEqual(summary.step_one, 2)
        self.assertEqual(summary.step_multi, 2)
        self.assertEqual(summary.maximum_absolute_tick_error, 2)
        self.assertEqual(summary.maximum_remaining_backlog, 3)
        self.assertEqual(len(summary.generation_transitions), 1)
        self.assertEqual(len(summary.same_generation_backwards), 1)
        self.assertEqual(len(summary.watchdog_backward_seeks), 1)
        self.assertEqual(len(summary.signature_replay_candidates), 1)


class ReportTests(unittest.TestCase):
    def test_writes_ranked_report_and_bounded_candidate_wavs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            session = Path(temporary) / "session"
            sample_rate = 8_000
            frames = [
                ((index * 37) % 60_000 - 30_000,
                 (index * 53) % 60_000 - 30_000)
                for index in range(sample_rate * 5)
            ]
            write_session(session, frames, sample_rate=sample_rate)
            candidates = tuple(
                ReplayCandidate(
                    start_frame=sample_rate + index * 100,
                    lag_frames=320,
                    window_frames=320,
                    correlation=0.99 - index * 0.0001,
                    normalized_error=0.02,
                    causal_events=(),
                )
                for index in range(21)
            )
            analysis = SessionAnalysis(
                conclusive_frames=len(frames),
                incomplete_ranges=(),
                candidates=candidates,
                causal_findings=("converter reset: seek",),
                shared_song_clock=analyze_shared_song_clock(
                    (
                        {
                            "kind": "gameplay_song_clock",
                            "cursor_source": "exact",
                            "flags": 0x1,
                            "generation": 7,
                            "output_frame_begin": sample_rate,
                            "source_frame_begin": sample_rate,
                            "value0": 60,
                            "value1": 62,
                            "value2": 2 << 32,
                            "value3": 2,
                        },
                    ),
                    candidates,
                    sample_rate,
                ),
            )
            candidate_directory = session / "candidates"
            candidate_directory.mkdir()
            (candidate_directory / "keep.txt").write_text(
                "preserve", encoding="utf-8"
            )
            (candidate_directory / "candidate-021.wav").write_bytes(
                b"preserve"
            )

            write_analysis_outputs(session, analysis)

            report = (session / "report.md").read_text(encoding="utf-8")
            self.assertIn("## Causal findings", report)
            self.assertIn("## Ranked replay candidates", report)
            self.assertIn("## Shared song clock", report)
            self.assertIn("Exact: 1 (100.00%)", report)
            self.assertIn("Step zero/one/multi: 0 / 0 / 1", report)
            self.assertIn("## Diagnostic verdict matrix", report)
            self.assertIn(
                "only after the user confirms the live artifact occurred",
                report,
            )
            self.assertTrue(
                (candidate_directory / "candidate-001.wav").is_file()
            )
            self.assertTrue(
                (candidate_directory / "candidate-020.wav").is_file()
            )
            self.assertEqual(
                (candidate_directory / "candidate-021.wav").read_bytes(),
                b"preserve",
            )
            self.assertEqual(
                (candidate_directory / "keep.txt").read_text(
                    encoding="utf-8"
                ),
                "preserve",
            )
            with wave.open(
                str(candidate_directory / "candidate-001.wav"), "rb"
            ) as clip:
                self.assertEqual(clip.getnchannels(), 2)
                self.assertEqual(clip.getsampwidth(), 2)
                self.assertLessEqual(
                    clip.getnframes(), sample_rate * 4
                )


if __name__ == "__main__":
    unittest.main()
