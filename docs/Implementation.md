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

## Current Step

Step 1 is complete: the project now has isolated marine recorder targets and a minimal GUI that links through `marine-core`.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.

## Left To Do

1. Load channel configuration from `data/marine_channels.json` instead of hardcoding Channel 16.
2. Add recorder GUI controls for selecting enabled channels.
3. Add an `SdrSource` interface in `marine-core`.
4. Add a HackRF/SoapySDR implementation behind `SdrSource`.
5. Stream IQ samples and compute raw wideband power.
6. Display live HackRF power in the recorder GUI.
7. Add `ChannelReceiver` for per-channel frequency offset and channel power.
8. Add NFM demodulation and audio level measurement.
9. Add WAV recording for Channel 16.
10. Add JSON sidecar metadata for recordings.
11. Add a configurable second channel display.
12. Add per-channel squelch state and threshold controls.
13. Add per-channel recording controls.
14. Add a separate playback GUI for recorded WAV files.
15. Add squelch-gated segment recording and timeline metadata.

## Notes

- The first GUI controls are intentionally disabled until the SDR backend exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- The code should continue to keep the core library independent from GUI-specific behavior.
