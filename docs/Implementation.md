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

## Current Step

Step 3 is complete: the recorder GUI can display Channel 16 by default and add/remove other loaded catalog channels at runtime.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.

## Left To Do

1. Add an `SdrSource` interface in `marine-core`.
2. Add a HackRF/SoapySDR implementation behind `SdrSource`.
3. Stream IQ samples and compute raw wideband power.
4. Display live HackRF power in the recorder GUI.
5. Add `ChannelReceiver` for per-channel frequency offset and channel power.
6. Add NFM demodulation and audio level measurement.
7. Add WAV recording for Channel 16.
8. Add JSON sidecar metadata for recordings.
9. Add a configurable second channel display.
10. Add per-channel squelch state and threshold controls.
11. Add per-channel recording controls.
12. Add a separate playback GUI for recorded WAV files.
13. Add squelch-gated segment recording and timeline metadata.

## Notes

- The SDR Connect/Start/Record controls are intentionally disabled until the SDR backend exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- Channel selection is runtime-only for now; selected channels are not persisted.
- The code should continue to keep the core library independent from GUI-specific behavior.
