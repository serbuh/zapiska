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
- Added `GrOsmoSdrSource`, the initial GNU Radio/gr-osmosdr backend.
- Added CMake requirements for GNU Radio blocks/runtime and gr-osmosdr.
- Added a first flowgraph lifecycle path:
  - GNU Radio `top_block`,
  - `osmosdr::source`,
  - GNU Radio `null_sink`,
  - open/start/stop/close.
- Added `marine-sdr-smoke`, a console smoke test for the SDR backend lifecycle.

## Current Step

Step 5 is complete: `marine-core` now has a GNU Radio/gr-osmosdr source backend that can create and run a minimal source flowgraph.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.
- Built and ran `marine-sdr-smoke` against a temporary gr-osmosdr file source.

## Left To Do

1. Stream IQ samples and compute raw wideband power.
2. Display live HackRF power in the recorder GUI.
3. Add `ChannelReceiver` for per-channel frequency offset and channel power.
4. Add NFM demodulation and audio level measurement.
5. Add WAV recording for Channel 16.
6. Add JSON sidecar metadata for recordings.
7. Add a configurable second channel display.
8. Add per-channel squelch state and threshold controls.
9. Add per-channel recording controls.
10. Add a separate playback GUI for recorded WAV files.
11. Add squelch-gated segment recording and timeline metadata.

## Immediate Next Step

Add wideband power measurement to `GrOsmoSdrSource`.

Acceptance criteria:

- Add a small GNU Radio sink/probe block for complex IQ samples.
- Compute reduced-rate wideband power from the shared IQ stream.
- Update `SdrStreamStats.samplesRead`.
- Emit reduced-rate stats updates suitable for GUI display.
- Keep the GUI decoupled from high-rate IQ blocks.

After that, wire the recorder GUI Connect/Start buttons to `GrOsmoSdrSource`.

## Notes

- Primary recorder backend decision: use GNU Radio with gr-osmosdr, matching the working Gqrx stack.
- The SDR Connect/Start/Record controls are intentionally disabled until the SDR backend exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- Channel selection is runtime-only for now; selected channels are not persisted.
- The code should continue to keep the core library independent from GUI-specific behavior.
