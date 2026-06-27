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
  - Zapiska `IqPowerSink`,
  - open/start/stop/close.
- Added `marine-sdr-smoke`, a console smoke test for the SDR backend lifecycle.
- Added `IqPowerSink`, a small GNU Radio sink that:
  - consumes complex IQ samples,
  - counts total samples read,
  - computes reduced-rate wideband power in dBFS,
  - emits throttled stats updates through `SdrStreamStats`.
- Extended `marine-sdr-smoke` to fail if the flowgraph does not produce samples or a power update.

## Current Step

Step 6 is complete: `marine-core` now measures wideband IQ power from the gr-osmosdr stream without exposing high-rate IQ data to the GUI.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.
- Built and ran `marine-sdr-smoke` against a temporary gr-osmosdr file source.
- Verified that `marine-sdr-smoke` reports non-zero `samplesRead` and a wideband power value.

## Left To Do

1. Display live HackRF power in the recorder GUI.
2. Add `ChannelReceiver` for per-channel frequency offset and channel power.
3. Add NFM demodulation and audio level measurement.
4. Add WAV recording for Channel 16.
5. Add JSON sidecar metadata for recordings.
6. Add a configurable second channel display.
7. Add per-channel squelch state and threshold controls.
8. Add per-channel recording controls.
9. Add a separate playback GUI for recorded WAV files.
10. Add squelch-gated segment recording and timeline metadata.

## Immediate Next Step

Wire the recorder GUI Connect/Start buttons to `GrOsmoSdrSource` and display live wideband power.

Acceptance criteria:

- Add a persistent `GrOsmoSdrSource` member to the recorder window.
- Enable Connect/Start/Stop for the SDR backend.
- Show backend state, actual sample rate, sample count, and wideband power.
- Surface open/start errors in the GUI.
- Keep recording controls disabled until audio recording exists.

## Notes

- Primary recorder backend decision: use GNU Radio with gr-osmosdr, matching the working Gqrx stack.
- The SDR Connect/Start/Record controls are intentionally disabled until the SDR backend exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- Channel selection is runtime-only for now; selected channels are not persisted.
- The code should continue to keep the core library independent from GUI-specific behavior.
