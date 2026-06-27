# Implementation Plan

This document tracks the current implementation state for Zapiska's marine recorder.

## Done

- Added the architecture plan in `docs/Arch.md`.
- Exported marine channels from Gqrx bookmarks to `data/marine_channels.json`.
- Created the initial standalone CMake project.
- Added `marine-core` as the shared library target.
- Added `marine-recorder-gui` as the first GUI executable target.
- Added a minimal recorder window with:
  - disconnected device state,
  - default center frequency display,
  - default sample rate display,
  - disabled future Connect/Start/Record controls,
  - Channel 16 row,
  - placeholder signal meter,
  - placeholder squelch and recording state.
- Added channel catalog loading from `data/marine_channels.json`.
- Added a post-build copy of `data/marine_channels.json` next to `marine-recorder-gui`.
- Updated the recorder GUI to show loaded/enabled channel counts.
- Added runtime channel selection controls:
  - catalog channel picker,
  - add selected channel,
  - remove selected visible channel.
- Added the `SdrSource` interface in `marine-core`.
- Added SDR core data types for:
  - device discovery,
  - source configuration,
  - IQ sample blocks,
  - stream statistics,
  - source state updates.

## Current Step

Step 4 is complete: `marine-core` now has the SDR source abstraction needed by the future GNU Radio/gr-osmosdr backend.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.

## Left To Do

1. Add a GNU Radio/gr-osmosdr implementation behind `SdrSource`.
2. Stream IQ samples and compute raw wideband power.
3. Display live HackRF power in the recorder GUI.
4. Add `ChannelReceiver` for per-channel frequency offset and channel power.
5. Add NFM demodulation and audio level measurement.
6. Add WAV recording for Channel 16.
7. Add JSON sidecar metadata for recordings.
8. Add a configurable second channel display.
9. Add per-channel squelch state and threshold controls.
10. Add per-channel recording controls.
11. Add a separate playback GUI for recorded WAV files.
12. Add squelch-gated segment recording and timeline metadata.

## Immediate Next Step

Add `GrOsmoSdrSource` in `marine-core`.

Acceptance criteria:

- CMake finds GNU Radio runtime and gr-osmosdr.
- `GrOsmoSdrSource` implements the existing `SdrSource` interface.
- The backend can create a GNU Radio `top_block`.
- The backend can create an `osmosdr::source` using a HackRF-compatible device string.
- The backend can apply center frequency, sample rate, and basic gain settings.
- Start/stop lifecycle works without involving the recorder GUI yet.
- If GNU Radio or gr-osmosdr is missing, CMake fails clearly rather than silently building an unusable backend.

After that, the next slice should add a small GNU Radio sink/probe block that computes wideband IQ power and reports it to the GUI.

## Notes

- Primary recorder backend decision: use GNU Radio with gr-osmosdr, matching the working Gqrx stack.
- The SDR Connect/Start/Record controls are intentionally disabled until the SDR backend exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- Channel selection is runtime-only for now; selected channels are not persisted.
- The code should continue to keep the core library independent from GUI-specific behavior.
