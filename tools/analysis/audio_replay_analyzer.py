from __future__ import annotations

import argparse
from array import array
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
import json
import math
from pathlib import Path
import struct
import sys
import wave


SCHEMA_VERSION = 1
WINDOW_MILLISECONDS = (33, 40, 50, 67, 100)
COARSE_CORRELATION = 0.985
FULL_CORRELATION = 0.97
MAXIMUM_NORMALIZED_ERROR = 0.25
MAXIMUM_CANDIDATES = 20
KNOWN_TIMELINE_KINDS = {
    "voice_created",
    "voice_play",
    "voice_stop",
    "seek_requested",
    "seek_applied",
    "converter_reset",
    "render_span",
    "audio_resync",
    "endpoint_block",
    "pcm_gap",
    "event_gap",
    "capture_limit",
    "checkpoint",
}


@dataclass(frozen=True)
class Pcm16Wave:
    sample_rate: int
    channels: int
    frames: Sequence[tuple[int, int]]


@dataclass(frozen=True)
class ReplayCandidate:
    start_frame: int
    lag_frames: int
    window_frames: int
    correlation: float
    normalized_error: float
    causal_events: Sequence[dict[str, object]]


@dataclass(frozen=True)
class SessionAnalysis:
    conclusive_frames: int
    incomplete_ranges: Sequence[tuple[int, int]]
    candidates: Sequence[ReplayCandidate]
    causal_findings: Sequence[str]


class _Pcm16FrameSequence(Sequence[tuple[int, int]]):
    def __init__(self, samples: array) -> None:
        self._samples = samples

    def __len__(self) -> int:
        return len(self._samples) // 2

    def __getitem__(
        self, index: int | slice
    ) -> tuple[int, int] | list[tuple[int, int]]:
        if isinstance(index, slice):
            start, stop, step = index.indices(len(self))
            return [self[position] for position in range(start, stop, step)]
        if index < 0:
            index += len(self)
        if index < 0 or index >= len(self):
            raise IndexError(index)
        sample = index * 2
        return self._samples[sample], self._samples[sample + 1]

    def __iter__(self) -> Iterator[tuple[int, int]]:
        samples = self._samples
        for index in range(0, len(samples), 2):
            yield samples[index], samples[index + 1]


@dataclass(frozen=True)
class _RiffInfo:
    sample_rate: int
    channels: int
    bits_per_sample: int
    block_align: int
    data_offset: int
    actual_data_bytes: int


def _read_riff_info(path: Path) -> _RiffInfo:
    try:
        file_size = path.stat().st_size
        with path.open("rb") as source:
            header = source.read(12)
            if (
                len(header) != 12
                or header[:4] != b"RIFF"
                or header[8:12] != b"WAVE"
            ):
                raise ValueError("input is not a RIFF WAVE file")

            offset = 12
            format_fields: tuple[int, int, int, int] | None = None
            data_offset: int | None = None
            while offset + 8 <= file_size:
                source.seek(offset)
                chunk_header = source.read(8)
                if len(chunk_header) != 8:
                    raise ValueError("truncated RIFF chunk header")
                chunk_id, chunk_size = struct.unpack("<4sI", chunk_header)
                payload_offset = offset + 8
                if chunk_id == b"fmt ":
                    if chunk_size < 16 or payload_offset + 16 > file_size:
                        raise ValueError("truncated WAVE format chunk")
                    source.seek(payload_offset)
                    payload = source.read(16)
                    (
                        format_tag,
                        channels,
                        sample_rate,
                        _byte_rate,
                        block_align,
                        bits_per_sample,
                    ) = struct.unpack("<HHIIHH", payload)
                    format_fields = (
                        format_tag,
                        channels,
                        sample_rate,
                        block_align << 16 | bits_per_sample,
                    )
                elif chunk_id == b"data":
                    data_offset = payload_offset
                    break

                next_offset = payload_offset + chunk_size + (chunk_size & 1)
                if next_offset > file_size:
                    raise ValueError("truncated RIFF chunk")
                offset = next_offset
    except OSError as error:
        raise ValueError(f"cannot read WAVE file: {error}") from error

    if format_fields is None or data_offset is None:
        raise ValueError("WAVE file is missing fmt or data chunk")
    format_tag, channels, sample_rate, packed = format_fields
    block_align = packed >> 16
    bits_per_sample = packed & 0xFFFF
    if (
        format_tag != 1
        or channels != 2
        or bits_per_sample != 16
        or block_align != 4
        or sample_rate <= 0
    ):
        raise ValueError(
            "expected PCM16 stereo WAVE with four-byte block alignment"
        )
    return _RiffInfo(
        sample_rate=sample_rate,
        channels=channels,
        bits_per_sample=bits_per_sample,
        block_align=block_align,
        data_offset=data_offset,
        actual_data_bytes=max(0, file_size - data_offset),
    )


def read_checkpointed_pcm16(
    path: Path, checkpoint_bytes: int
) -> Pcm16Wave:
    info = _read_riff_info(path)
    if not isinstance(checkpoint_bytes, int) or checkpoint_bytes < 0:
        raise ValueError("checkpoint byte count must be nonnegative")
    if checkpoint_bytes > info.actual_data_bytes:
        raise ValueError("checkpoint is larger than the actual WAVE data")
    readable_bytes = min(info.actual_data_bytes, checkpoint_bytes)
    readable_bytes -= readable_bytes % info.block_align

    samples = array("h")
    try:
        with path.open("rb") as source:
            source.seek(info.data_offset)
            remaining = readable_bytes
            while remaining:
                chunk = source.read(min(remaining, 1 << 20))
                if not chunk:
                    raise ValueError("truncated checkpointed WAVE data")
                if len(chunk) & 1:
                    raise ValueError("misaligned PCM16 data")
                part = array("h")
                part.frombytes(chunk)
                if sys.byteorder != "little":
                    part.byteswap()
                samples.extend(part)
                remaining -= len(chunk)
    except OSError as error:
        raise ValueError(f"cannot read checkpointed PCM: {error}") from error

    return Pcm16Wave(
        sample_rate=info.sample_rate,
        channels=info.channels,
        frames=_Pcm16FrameSequence(samples),
    )


def _integer(
    record: dict[str, object],
    field: str,
    *,
    nonnegative: bool = True,
) -> int:
    value = record.get(field)
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"timeline field {field!r} must be an integer")
    if nonnegative and value < 0:
        raise ValueError(f"timeline field {field!r} must be nonnegative")
    return value


def read_timeline(path: Path) -> Sequence[dict[str, object]]:
    records: list[dict[str, object]] = []
    previous_checkpoint: tuple[int, int, int] | None = None
    try:
        with path.open("r", encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                try:
                    value = json.loads(line)
                except json.JSONDecodeError as error:
                    raise ValueError(
                        f"malformed timeline JSON at line {line_number}"
                    ) from error
                if not isinstance(value, dict):
                    raise ValueError(
                        f"timeline line {line_number} is not an object"
                    )
                if (
                    "schema_version" in value
                    and value["schema_version"] != SCHEMA_VERSION
                ):
                    raise ValueError("unknown timeline schema version")
                kind = value.get("kind")
                if not isinstance(kind, str) or kind not in KNOWN_TIMELINE_KINDS:
                    raise ValueError(
                        f"unknown timeline record at line {line_number}"
                    )
                if kind == "checkpoint":
                    checkpoint = (
                        _integer(value, "wav_data_bytes"),
                        _integer(value, "pcm_sequence"),
                        _integer(value, "event_sequence"),
                    )
                    if (
                        previous_checkpoint is not None
                        and any(
                            current < previous
                            for current, previous in zip(
                                checkpoint, previous_checkpoint
                            )
                        )
                    ):
                        raise ValueError("non-monotonic timeline checkpoint")
                    if not isinstance(value.get("conclusive"), bool):
                        raise ValueError(
                            "checkpoint conclusive field must be boolean"
                        )
                    previous_checkpoint = checkpoint
                elif kind in {"pcm_gap", "event_gap"}:
                    begin = _integer(value, "output_frame_begin")
                    end = _integer(value, "output_frame_end")
                    if end < begin:
                        raise ValueError("inverted incomplete output range")
                records.append(value)
    except OSError as error:
        raise ValueError(f"cannot read timeline: {error}") from error
    return tuple(records)


def _session_metadata(
    timeline: Sequence[dict[str, object]],
) -> dict[str, object]:
    for record in timeline:
        if record.get("kind") == "session_metadata":
            return record
    return {}


def _endpoint_blocks(
    timeline: Sequence[dict[str, object]],
) -> list[dict[str, object]]:
    return [
        record
        for record in timeline
        if record.get("kind") == "endpoint_block"
    ]


def _output_base(
    timeline: Sequence[dict[str, object]],
    sample_rate: int,
) -> int:
    metadata = _session_metadata(timeline)
    frames_per_block = metadata.get(
        "frames_per_block", max(1, sample_rate // 100)
    )
    if not isinstance(frames_per_block, int) or frames_per_block <= 0:
        frames_per_block = max(1, sample_rate // 100)
    for block in _endpoint_blocks(timeline):
        sequence = block.get("pcm_sequence")
        output = block.get("output_frame_begin")
        if isinstance(sequence, int) and isinstance(output, int):
            return output - sequence * frames_per_block
    return 0


def _incomplete_ranges(
    timeline: Sequence[dict[str, object]],
) -> tuple[tuple[int, int], ...]:
    result: list[tuple[int, int]] = []
    for record in timeline:
        if record.get("kind") not in {"pcm_gap", "event_gap"}:
            continue
        begin = record.get("output_frame_begin")
        end = record.get("output_frame_end")
        if isinstance(begin, int) and isinstance(end, int) and end >= begin:
            result.append((begin, end))
    return tuple(result)


def _intersects(
    begin: int,
    end: int,
    ranges: Sequence[tuple[int, int]],
) -> bool:
    return any(begin < range_end and range_begin < end
               for range_begin, range_end in ranges)


def _block_signature(
    frames: Sequence[tuple[int, int]],
    begin: int,
    block_frames: int,
) -> tuple[float, ...] | None:
    values: list[float] = []
    for index in range(32):
        offset = round(index * (block_frames - 1) / 31)
        left, right = frames[begin + offset]
        values.extend((float(left), float(right)))
    mean = sum(values) / len(values)
    centered = [value - mean for value in values]
    norm = math.sqrt(sum(value * value for value in centered))
    if norm <= 1e-12:
        return None
    return tuple(value / norm for value in centered)


def _full_metrics(
    frames: Sequence[tuple[int, int]],
    first_begin: int,
    second_begin: int,
    frame_count: int,
) -> tuple[float, float]:
    dot = 0.0
    first_power = 0.0
    second_power = 0.0
    difference_power = 0.0
    for offset in range(frame_count):
        first = frames[first_begin + offset]
        second = frames[second_begin + offset]
        for first_sample, second_sample in zip(first, second):
            left = float(first_sample)
            right = float(second_sample)
            dot += left * right
            first_power += left * left
            second_power += right * right
            difference = left - right
            difference_power += difference * difference
    if first_power <= 0.0 or second_power <= 0.0:
        return 0.0, math.inf
    correlation = dot / math.sqrt(first_power * second_power)
    sample_count = max(1, frame_count * 2)
    rms_difference = math.sqrt(difference_power / sample_count)
    reference_rms = max(
        math.sqrt(first_power / sample_count),
        math.sqrt(second_power / sample_count),
        1.0,
    )
    return correlation, rms_difference / reference_rms


def _normalized_qpc_100ns(
    record: dict[str, object],
    frequency: int,
) -> int | None:
    ticks = record.get("qpc_ticks")
    if (
        not isinstance(ticks, int)
        or ticks <= 0
        or frequency <= 0
    ):
        return None
    return ticks * 10_000_000 // frequency


def _candidate_endpoint(
    candidate_output_frame: int,
    timeline: Sequence[dict[str, object]],
) -> dict[str, object] | None:
    blocks = _endpoint_blocks(timeline)
    for block in blocks:
        begin = block.get("output_frame_begin")
        end = block.get("submitted_tail")
        if (
            isinstance(begin, int)
            and isinstance(end, int)
            and begin <= candidate_output_frame < end
        ):
            return block
    valid = [
        block
        for block in blocks
        if isinstance(block.get("output_frame_begin"), int)
    ]
    if not valid:
        return None
    return min(
        valid,
        key=lambda block: abs(
            int(block["output_frame_begin"]) - candidate_output_frame
        ),
    )


def _nearby_events(
    candidate_start: int,
    sample_rate: int,
    timeline: Sequence[dict[str, object]],
    output_base: int,
) -> tuple[dict[str, object], ...]:
    candidate_output = output_base + candidate_start
    frame_tolerance = sample_rate // 4
    metadata = _session_metadata(timeline)
    frequency = metadata.get("qpc_frequency", 0)
    if not isinstance(frequency, int):
        frequency = 0
    endpoint = _candidate_endpoint(candidate_output, timeline)
    endpoint_qpc = (
        endpoint.get("endpoint_qpc_100ns")
        if endpoint is not None
        else None
    )
    if not isinstance(endpoint_qpc, int):
        endpoint_qpc = None

    output_kinds = {
        "seek_applied",
        "converter_reset",
        "render_span",
        "pcm_gap",
        "event_gap",
    }
    ignored_kinds = {
        "session_metadata",
        "endpoint_block",
        "checkpoint",
        "capture_limit",
    }
    nearby: list[dict[str, object]] = []
    for record in timeline:
        kind = record.get("kind")
        if kind in ignored_kinds:
            continue
        output = record.get("output_frame_begin")
        has_output = (
            isinstance(output, int)
            and (output != 0 or kind in output_kinds)
        )
        output_near = has_output and abs(output - candidate_output) <= (
            frame_tolerance
        )
        event_qpc = _normalized_qpc_100ns(record, frequency)
        qpc_near = (
            endpoint_qpc is not None
            and event_qpc is not None
            and abs(event_qpc - endpoint_qpc) <= 2_500_000
        )
        if output_near or qpc_near:
            nearby.append(record)
    return tuple(nearby)


def scan_replay_candidates(
    wave_data: Pcm16Wave,
    timeline: Sequence[dict[str, object]] = (),
) -> Sequence[ReplayCandidate]:
    if (
        wave_data.sample_rate <= 0
        or wave_data.channels != 2
    ):
        raise ValueError("replay scan requires PCM16 stereo frames")
    frames = wave_data.frames
    block_frames = max(1, round(wave_data.sample_rate / 100))
    block_count = len(frames) // block_frames
    if block_count < 4:
        return ()

    output_base = _output_base(timeline, wave_data.sample_rate)
    gaps = _incomplete_ranges(timeline)
    signatures = [
        _block_signature(frames, block * block_frames, block_frames)
        for block in range(block_count)
    ]

    runs: list[tuple[int, int, int]] = []
    for lag_blocks in range(1, 26):
        run_start: int | None = None
        for block in range(lag_blocks, block_count):
            target_output = output_base + block * block_frames
            source_output = target_output - lag_blocks * block_frames
            invalid = (
                _intersects(
                    target_output,
                    target_output + block_frames,
                    gaps,
                )
                or _intersects(
                    source_output,
                    source_output + block_frames,
                    gaps,
                )
            )
            first = signatures[block]
            second = signatures[block - lag_blocks]
            correlated = (
                not invalid
                and first is not None
                and second is not None
                and sum(
                    left * right for left, right in zip(first, second)
                ) >= COARSE_CORRELATION
            )
            if correlated:
                if run_start is None:
                    run_start = block
            elif run_start is not None:
                if block - run_start >= 3:
                    runs.append((run_start, block, lag_blocks))
                run_start = None
        if run_start is not None and block_count - run_start >= 3:
            runs.append((run_start, block_count, lag_blocks))

    raw_candidates: list[ReplayCandidate] = []
    for run_start, _run_end, lag_blocks in runs:
        lag_frames = lag_blocks * block_frames
        best: ReplayCandidate | None = None
        for start_block in range(
            max(lag_blocks, run_start - 1),
            run_start + 1,
        ):
            start = start_block * block_frames
            for milliseconds in WINDOW_MILLISECONDS:
                window_frames = max(
                    1,
                    round(
                        wave_data.sample_rate * milliseconds / 1_000
                    ),
                )
                source_start = start - lag_frames
                if (
                    source_start < 0
                    or start + window_frames > len(frames)
                    or source_start + window_frames > len(frames)
                ):
                    continue
                target_output = output_base + start
                source_output = output_base + source_start
                if (
                    _intersects(
                        target_output,
                        target_output + window_frames,
                        gaps,
                    )
                    or _intersects(
                        source_output,
                        source_output + window_frames,
                        gaps,
                    )
                ):
                    continue
                correlation, error = _full_metrics(
                    frames, start, source_start, window_frames
                )
                if (
                    correlation < FULL_CORRELATION
                    or error > MAXIMUM_NORMALIZED_ERROR
                ):
                    continue
                candidate = ReplayCandidate(
                    start_frame=start,
                    lag_frames=lag_frames,
                    window_frames=window_frames,
                    correlation=correlation,
                    normalized_error=error,
                    causal_events=(),
                )
                if best is None or (
                    candidate.correlation -
                    0.25 * candidate.normalized_error
                ) > (
                    best.correlation - 0.25 * best.normalized_error
                ):
                    best = candidate
        if best is not None:
            raw_candidates.append(best)

    ranked = sorted(
        raw_candidates,
        key=lambda candidate: (
            -(candidate.correlation -
              0.25 * candidate.normalized_error),
            candidate.start_frame,
        ),
    )
    cluster_frames = wave_data.sample_rate // 10
    selected: list[ReplayCandidate] = []
    for candidate in ranked:
        if any(
            abs(candidate.start_frame - retained.start_frame)
            <= cluster_frames
            for retained in selected
        ):
            continue
        selected.append(candidate)
        if len(selected) == MAXIMUM_CANDIDATES:
            break

    enriched = [
        ReplayCandidate(
            start_frame=candidate.start_frame,
            lag_frames=candidate.lag_frames,
            window_frames=candidate.window_frames,
            correlation=candidate.correlation,
            normalized_error=candidate.normalized_error,
            causal_events=_nearby_events(
                candidate.start_frame,
                wave_data.sample_rate,
                timeline,
                output_base,
            ),
        )
        for candidate in selected
    ]
    return tuple(enriched)


def _event_name(record: dict[str, object]) -> str:
    kind = str(record.get("kind", "unknown"))
    output = record.get("output_frame_begin")
    return f"{kind}@{output}" if isinstance(output, int) else kind


def _causal_findings(
    timeline: Sequence[dict[str, object]],
) -> tuple[str, ...]:
    findings: list[str] = []
    for record in timeline:
        kind = record.get("kind")
        voice = record.get("voice_id", 0)
        output = record.get("output_frame_begin", 0)
        source_begin = record.get("source_frame_begin", 0)
        source_end = record.get("source_frame_end", 0)
        if (
            kind == "seek_applied"
            and isinstance(source_begin, int)
            and isinstance(source_end, int)
            and source_end < source_begin
        ):
            findings.append(
                "backward SeekApplied "
                f"voice={voice} output={output} "
                f"source={source_begin}->{source_end}"
            )
        elif kind == "converter_reset":
            reason = record.get("reset_reason")
            findings.append(
                f"ConverterReset({reason}) voice={voice} output={output}"
            )
        elif (
            kind == "audio_resync"
            and (
                record.get("resync_decision") == "allowed_out_of_margin"
                or record.get("decision") == 2
            )
        ):
            findings.append(
                "allowed out-of-margin AudioResync "
                f"drift_ms={record.get('drift_ms', record.get('signed_value0'))} "
                f"margin_ms={record.get('margin_ms', record.get('signed_value1'))}"
            )
        elif kind == "pcm_gap":
            findings.append(
                "PCM gap output="
                f"[{record.get('output_frame_begin')},"
                f"{record.get('output_frame_end')})"
            )
        elif kind == "event_gap":
            findings.append(
                "event gap output="
                f"[{record.get('output_frame_begin')},"
                f"{record.get('output_frame_end')})"
            )

    spans_by_voice: dict[
        tuple[object, object], list[dict[str, object]]
    ] = {}
    for record in timeline:
        if record.get("kind") == "render_span":
            key = (record.get("voice_id"), record.get("epoch"))
            spans_by_voice.setdefault(key, []).append(record)
    resets = [
        record
        for record in timeline
        if record.get("kind") in {"seek_applied", "converter_reset"}
    ]
    for key, spans in spans_by_voice.items():
        spans.sort(key=lambda value: int(value.get("output_frame_begin", 0)))
        for previous, current in zip(spans, spans[1:]):
            previous_end = previous.get("source_frame_end")
            current_begin = current.get("source_frame_begin")
            if not isinstance(previous_end, int) or not isinstance(
                current_begin, int
            ):
                continue
            flags = int(previous.get("flags", 0)) | int(
                current.get("flags", 0)
            )
            if flags & 0x1:
                continue
            output_begin = int(current.get("output_frame_begin", 0))
            previous_output_end = int(
                previous.get("output_frame_end", output_begin)
            )
            explained = any(
                reset.get("voice_id") == key[0]
                and previous_output_end
                <= int(reset.get("output_frame_begin", 0))
                <= output_begin
                for reset in resets
            )
            if explained or current_begin == previous_end:
                continue
            relation = (
                "overlapping non-loop RenderSpan"
                if current_begin < previous_end
                else "source gap between RenderSpan records"
            )
            findings.append(
                f"{relation} voice={key[0]} epoch={key[1]} "
                f"source={previous_end}->{current_begin}"
            )

    previous_endpoint: dict[str, object] | None = None
    for block in _endpoint_blocks(timeline):
        if previous_endpoint is not None:
            regression_fields = (
                "endpoint_clock_position",
                "endpoint_qpc_100ns",
                "presented_output_frame",
                "submitted_tail",
            )
            regressed = [
                field
                for field in regression_fields
                if isinstance(block.get(field), int)
                and isinstance(previous_endpoint.get(field), int)
                and int(block[field]) < int(previous_endpoint[field])
            ]
            if regressed:
                findings.append(
                    "endpoint clock regression fields=" +
                    ",".join(regressed)
                )
            begin = block.get("output_frame_begin")
            prior_tail = previous_endpoint.get("submitted_tail")
            discontinuity = block.get("discontinuity_frames", 0)
            if isinstance(begin, int) and isinstance(prior_tail, int):
                jump = begin - prior_tail
                if block.get("pacing_kind") == 0:
                    if begin != prior_tail:
                        findings.append(
                            "sequential endpoint block does not begin at "
                            f"prior tail prior_tail={prior_tail} "
                            f"begin={begin}"
                        )
                elif jump != 0:
                    if not isinstance(discontinuity, int) or jump != (
                        discontinuity
                    ):
                        findings.append(
                            "endpoint discontinuity "
                            f"prior_tail={prior_tail} begin={begin} "
                            f"reported={discontinuity}"
                        )
                    else:
                        findings.append(
                            "endpoint recoverable discontinuity "
                            f"frames={discontinuity}"
                        )
        previous_endpoint = block

    unique: list[str] = []
    for finding in findings:
        if finding not in unique:
            unique.append(finding)
    return tuple(unique)


def _load_session(path: Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid session.json: {error}") from error
    if not isinstance(value, dict) or value.get("schema_version") != (
        SCHEMA_VERSION
    ):
        raise ValueError("unknown session schema version")
    for field in (
        "sample_rate",
        "channels",
        "bits_per_sample",
        "frames_per_block",
        "qpc_frequency",
    ):
        if (
            not isinstance(value.get(field), int)
            or isinstance(value.get(field), bool)
            or int(value[field]) <= 0
        ):
            raise ValueError(f"invalid session field {field}")
    if value["channels"] != 2 or value["bits_per_sample"] != 16:
        raise ValueError("session is not PCM16 stereo")
    return value


def analyze_session(session_directory: Path) -> SessionAnalysis:
    session = _load_session(session_directory / "session.json")
    timeline = read_timeline(session_directory / "timeline.jsonl")
    checkpoints = [
        record
        for record in timeline
        if record.get("kind") == "checkpoint"
    ]
    if not checkpoints:
        raise ValueError("timeline has no conclusive checkpoint")
    checkpoint = checkpoints[-1]
    checkpoint_bytes = _integer(checkpoint, "wav_data_bytes")
    wave_data = read_checkpointed_pcm16(
        session_directory / "submitted.wav", checkpoint_bytes
    )
    if wave_data.sample_rate != session["sample_rate"]:
        raise ValueError("session and WAVE sample rates differ")

    enriched_timeline = (
        {
            "kind": "session_metadata",
            "sample_rate": session["sample_rate"],
            "frames_per_block": session["frames_per_block"],
            "qpc_frequency": session["qpc_frequency"],
        },
        *timeline,
    )
    incomplete = list(_incomplete_ranges(timeline))
    if checkpoint.get("conclusive") is not True and not incomplete:
        incomplete.append((0, len(wave_data.frames)))
    candidates = scan_replay_candidates(
        wave_data, enriched_timeline
    )
    findings = list(_causal_findings(timeline))
    if checkpoint.get("conclusive") is not True:
        findings.append("last checkpoint is non-conclusive")
    return SessionAnalysis(
        conclusive_frames=len(wave_data.frames),
        incomplete_ranges=tuple(incomplete),
        candidates=candidates,
        causal_findings=tuple(findings),
    )


def _write_pcm16_wave(
    path: Path,
    wave_data: Pcm16Wave,
    begin: int,
    end: int,
) -> None:
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(wave_data.sample_rate)
        position = begin
        while position < end:
            chunk_end = min(end, position + 65_536)
            samples = array("h")
            for frame in range(position, chunk_end):
                samples.extend(wave_data.frames[frame])
            if sys.byteorder != "little":
                samples.byteswap()
            output.writeframesraw(samples.tobytes())
            position = chunk_end


VERDICT_ROWS = (
    (
        "Backward seek plus repeated submitted waveform",
        "Sample 07: application-side source rewind",
    ),
    (
        "No seek, overlapping source spans, repeated submitted PCM",
        "Mixer cursor or input reuse",
    ),
    (
        "Monotonic source spans, repeated submitted PCM",
        "Post-voice replay or stale final mix, sample 13-like",
    ),
    (
        "Endpoint clock jump plus artifact in submitted PCM",
        "Application/endpoint scheduling boundary follow-up",
    ),
    (
        "Artifact in submitted WAV without timeline anomaly",
        "Investigate final mixing and PCM handoff",
    ),
    (
        "Artifact heard live but absent from submitted WAV",
        "Fault is downstream of ReleaseRenderBuffer",
    ),
    (
        "Relevant PCM queue range missing",
        "Repeat the diagnostic run; no clean downstream verdict",
    ),
)


def _write_outputs(
    output_directory: Path,
    analysis: SessionAnalysis,
    wave_data: Pcm16Wave,
    format_description: str,
) -> None:
    output_directory.mkdir(parents=True, exist_ok=True)
    candidate_directory = output_directory / "candidates"
    candidate_directory.mkdir(parents=True, exist_ok=True)
    for index in range(1, MAXIMUM_CANDIDATES + 1):
        candidate = candidate_directory / f"candidate-{index:03d}.wav"
        try:
            candidate.unlink()
        except FileNotFoundError:
            pass

    candidates = tuple(analysis.candidates[:MAXIMUM_CANDIDATES])
    for index, candidate in enumerate(candidates, 1):
        begin = max(
            0, candidate.start_frame - 2 * wave_data.sample_rate
        )
        end = min(
            analysis.conclusive_frames,
            candidate.start_frame + 2 * wave_data.sample_rate,
        )
        _write_pcm16_wave(
            candidate_directory / f"candidate-{index:03d}.wav",
            wave_data,
            begin,
            end,
        )

    duration = (
        analysis.conclusive_frames / wave_data.sample_rate
        if wave_data.sample_rate
        else 0.0
    )
    lines = [
        "# Audio Replay Diagnostic Analysis",
        "",
        "## Endpoint/session format",
        "",
        f"- {format_description}",
        f"- Conclusive frames: {analysis.conclusive_frames}",
        f"- Conclusive duration: {duration:.3f} seconds",
        "",
        "## Incomplete ranges",
        "",
    ]
    if analysis.incomplete_ranges:
        lines.extend(
            f"- [{begin}, {end})"
            for begin, end in analysis.incomplete_ranges
        )
    else:
        lines.append("- None")
    lines.extend(["", "## Causal findings", ""])
    if analysis.causal_findings:
        lines.extend(f"- {finding}" for finding in analysis.causal_findings)
    else:
        lines.append("- No deterministic timeline anomaly.")

    lines.extend(
        [
            "",
            "## Ranked replay candidates",
            "",
            "| Rank | Start | Lag | Window | Correlation | Error | Score | Nearby events |",
            "|---:|---:|---:|---:|---:|---:|---:|---|",
        ]
    )
    for index, candidate in enumerate(candidates, 1):
        score = (
            candidate.correlation -
            0.25 * candidate.normalized_error
        )
        nearby = ", ".join(
            _event_name(event) for event in candidate.causal_events
        ) or "none"
        lines.append(
            f"| {index} | "
            f"{candidate.start_frame / wave_data.sample_rate:.3f}s | "
            f"{candidate.lag_frames * 1000 / wave_data.sample_rate:.1f}ms | "
            f"{candidate.window_frames * 1000 / wave_data.sample_rate:.1f}ms | "
            f"{candidate.correlation:.6f} | "
            f"{candidate.normalized_error:.6f} | "
            f"{score:.6f} | {nearby} |"
        )
    if not candidates:
        lines.append("| - | - | - | - | - | - | - | none |")

    lines.extend(
        [
            "",
            "## Diagnostic verdict matrix",
            "",
            "| Evidence | Verdict |",
            "|---|---|",
        ]
    )
    lines.extend(
        f"| {evidence} | {verdict} |"
        for evidence, verdict in VERDICT_ROWS
    )
    lines.extend(
        [
            "",
            "## Evidence warning",
            "",
            "Clean submitted PCM moves the investigation downstream only after the user confirms the live artifact occurred during this exact capture.",
            "A waveform match without aligned causal evidence is a listening candidate, not a defect verdict.",
            "",
        ]
    )
    (output_directory / "report.md").write_text(
        "\n".join(lines), encoding="utf-8"
    )


def write_analysis_outputs(
    session_directory: Path, analysis: SessionAnalysis
) -> None:
    session = _load_session(session_directory / "session.json")
    timeline = read_timeline(session_directory / "timeline.jsonl")
    checkpoints = [
        record
        for record in timeline
        if record.get("kind") == "checkpoint"
    ]
    if not checkpoints:
        raise ValueError("timeline has no checkpoint")
    wave_data = read_checkpointed_pcm16(
        session_directory / "submitted.wav",
        _integer(checkpoints[-1], "wav_data_bytes"),
    )
    if analysis.conclusive_frames > len(wave_data.frames):
        raise ValueError("analysis exceeds checkpointed PCM")
    last_checkpoint = checkpoints[-1]
    conclusive_checkpoints = [
        checkpoint
        for checkpoint in checkpoints
        if checkpoint.get("conclusive") is True
    ]
    last_conclusive = (
        conclusive_checkpoints[-1]
        if conclusive_checkpoints
        else None
    )
    description = (
        f"PCM16 stereo, {session['sample_rate']} Hz, "
        f"{session['frames_per_block']} frames per block, "
        f"QPC frequency {session['qpc_frequency']}\n"
        f"- Last checkpoint: pcm_sequence="
        f"{last_checkpoint.get('pcm_sequence')}, wav_data_bytes="
        f"{last_checkpoint.get('wav_data_bytes')}, conclusive="
        f"{str(last_checkpoint.get('conclusive')).lower()}\n"
        "- Last conclusive checkpoint: " +
        (
            f"pcm_sequence={last_conclusive.get('pcm_sequence')}, "
            f"wav_data_bytes={last_conclusive.get('wav_data_bytes')}"
            if last_conclusive is not None
            else "none"
        )
    )
    _write_outputs(
        session_directory, analysis, wave_data, description
    )


def _analyze_wav_only(
    source: Path,
    output_directory: Path,
) -> SessionAnalysis:
    info = _read_riff_info(source)
    wave_data = read_checkpointed_pcm16(
        source, info.actual_data_bytes
    )
    analysis = SessionAnalysis(
        conclusive_frames=len(wave_data.frames),
        incomplete_ranges=(),
        candidates=scan_replay_candidates(wave_data),
        causal_findings=(),
    )
    _write_outputs(
        output_directory,
        analysis,
        wave_data,
        f"PCM16 stereo, {wave_data.sample_rate} Hz, WAV-only control\n"
        f"- Last conclusive checkpoint: full WAV data "
        f"({len(wave_data.frames) * 4} bytes)",
    )
    return analysis


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Analyze temporary submitted WASAPI PCM for short replay"
    )
    parser.add_argument(
        "session_directory",
        nargs="?",
        type=Path,
        help="audio-diagnostics timestamped session directory",
    )
    parser.add_argument(
        "--wav-only",
        type=Path,
        help="analyze a PCM16 stereo control WAV without a timeline",
    )
    parser.add_argument(
        "--output-directory",
        type=Path,
        help="output directory for --wav-only analysis",
    )
    arguments = parser.parse_args(argv)

    try:
        if arguments.wav_only is not None:
            if arguments.session_directory is not None:
                raise ValueError(
                    "session directory and --wav-only are mutually exclusive"
                )
            output = arguments.output_directory or (
                arguments.wav_only.parent /
                f"{arguments.wav_only.stem}-analysis"
            )
            analysis = _analyze_wav_only(arguments.wav_only, output)
            print(output / "report.md")
        else:
            if arguments.session_directory is None:
                raise ValueError("a session directory or --wav-only is required")
            if arguments.output_directory is not None:
                raise ValueError(
                    "--output-directory is only valid with --wav-only"
                )
            analysis = analyze_session(arguments.session_directory)
            write_analysis_outputs(arguments.session_directory, analysis)
            print(arguments.session_directory / "report.md")
    except ValueError as error:
        print(f"audio replay analysis failed: {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"audio replay writer failed: {error}", file=sys.stderr)
        return 1
    return 2 if analysis.incomplete_ranges else 0


if __name__ == "__main__":
    raise SystemExit(main())
