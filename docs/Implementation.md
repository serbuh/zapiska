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

## Current Step

Step 2 is complete: the recorder GUI now loads the exported channel catalog instead of hardcoding Channel 16 directly in the window.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.

## Left To Do

1. Add recorder GUI controls for selecting enabled channels.
2. Add an `SdrSource` interface in `marine-core`.
3. Add a HackRF/SoapySDR implementation behind `SdrSource`.
4. Stream IQ samples and compute raw wideband power.
5. Display live HackRF power in the recorder GUI.
6. Add `ChannelReceiver` for per-channel frequency offset and channel power.
7. Add NFM demodulation and audio level measurement.
8. Add WAV recording for Channel 16.
9. Add JSON sidecar metadata for recordings.
10. Add a configurable second channel display.
11. Add per-channel squelch state and threshold controls.
12. Add per-channel recording controls.
13. Add a separate playback GUI for recorded WAV files.
14. Add squelch-gated segment recording and timeline metadata.

## Notes

- The first GUI controls are intentionally disabled until the SDR backend exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- The GUI shows only default-enabled channels from the loaded catalog.
- The code should continue to keep the core library independent from GUI-specific behavior.
