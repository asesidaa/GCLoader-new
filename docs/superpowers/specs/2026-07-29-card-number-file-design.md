# GCLoader Card Number File Design

Date: 2026-07-29

Status: Approved design contract

## Context

GCLoader currently returns one compile-time 24-byte RFID payload. Its first
eight bytes are the fixed RFID prefix from the original emulation, and its
last sixteen bytes are the ASCII card number `7020392010281502`. Changing the
card number therefore requires rebuilding the loader.

Operators need to change only the card number while the game is stopped or
running, without editing `config.toml` or rebuilding the DLL.

The repository is `H:\gc\artifacts\GCLoader`. `H:\gc` remains the runtime and
deployment tree.

## Goals

- Read the card number from `card.txt` in the process current directory.
- Re-read the file for every armed card payload transfer so edits apply to the
  next card load without restarting GCLoader.
- Preserve the current fixed eight-byte RFID prefix.
- Preserve `7020392010281502` as the built-in fallback card number.
- Ship a default `card.txt` in the repository and in every generated `dist`
  directory.
- Use standard-library filesystem paths end to end so process current
  directories containing Chinese, Japanese, or other non-ASCII characters
  work on Windows.
- Keep malformed or unavailable operator input from breaking RFID emulation.

## Non-Goals

- Making the RFID prefix configurable.
- Adding a `config.toml` field or ConfigGUI control.
- Watching the file, caching it, or retaining a last-known-good custom value.
- Supporting hexadecimal, separated, or otherwise formatted card numbers.
- Changing card-present signaling, the card-read key, or one-shot consumption.
- Deploying the built DLL or changing files in the runtime tree as part of
  implementation.

## File Contract

`card.txt` contains one 16-digit decimal card number:

```text
7020392010281502
```

Leading and trailing ASCII whitespace, including the final newline normally
written by text editors, is ignored. After trimming, the content must contain
exactly sixteen characters and every character must be `0` through `9`.

Missing files, read failures, empty files, incorrect lengths, and non-decimal
characters all select the built-in default. A transient malformed file during
an edit therefore affects at most that card load and is retried on the next
load.

## Chosen Approach

The RFID device reads `card.txt` when it handles the card-present general
output command (`0x32`), immediately before appending the 24-byte card payload.
It does not read the file while no card is present.

This point is the actual one-shot card transfer to the game. Reading here
ensures that:

- every card load sees a fresh file read;
- an edit made after pressing the card-read key but before payload transfer is
  honored;
- file I/O remains outside the perpetual card-key polling worker; and
- no watcher, cache invalidation, or cross-thread card-data state is required.

Reading on the card-key edge was rejected because it snapshots the value
before the game actually asks for the card. A background watcher was rejected
because it adds lifetime and synchronization complexity without improving the
requested workflow.

## Source Architecture

Add a focused RFID card-data unit under `src/Rfid`:

- `CardData.h` owns the 24-byte type, fixed prefix, built-in default number,
  default payload, and the current-directory load entry point.
- `CardData.cpp` opens `card.txt` for each call, trims and validates its
  contents, and assembles a payload or returns the built-in default.
- `Jvs/Device.cpp` requests card data only in the existing card-present
  `0x32` branch and appends the returned payload.

The loader performs no persistent caching. All file and allocation failures
are contained inside the non-throwing load entry point and produce the
default payload. File access uses `std::filesystem::path` and the
standard-library stream overload that consumes that path directly; it does
not narrow the path through `.string()` or call an ANSI Win32 file API.

The repository-root `card.txt` contains the default number. Top-level CMake
copies it into `${GC_DIST_DIR}/card.txt` alongside `config.toml`.

## Testing

Test coverage will establish:

- exact 16-digit content produces the expected 24-byte payload;
- surrounding whitespace is accepted;
- missing, empty, short, long, and non-decimal content use the current default;
- changing the file between two load calls returns the two different card
  numbers, proving there is no cache;
- a current directory containing Chinese and Japanese path components can
  load and reload `card.txt`;
- two armed JVS card transfers can observe two file versions while preserving
  the fixed prefix and existing one-shot card consumption; and
- CMake stages the repository default file into `dist`.

Implementation follows red-green-refactor: new behavior is first expressed by
failing focused tests, then the smallest production change is added.
