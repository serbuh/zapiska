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
- Wired the recorder GUI to `GrOsmoSdrSource` with Connect, Start, Stop, and Disconnect controls.
- Added recorder GUI display for:
  - SDR backend state,
  - actual center frequency,
  - actual sample rate,
  - samples read,
  - wideband power in dBFS,
  - SDR open/start errors.
- Added `ChannelReceiver`, a per-channel GNU Radio receiver branch that:
  - frequency-translates a configured channel offset to baseband,
  - low-pass filters the configured channel bandwidth,
  - decimates the channel stream,
  - measures reduced-rate channel power with `IqPowerSink`.
- Added channel configuration and channel power stats to `SdrSourceConfig`/`SdrStreamStats`.
- Wired the recorder GUI signal meter to live Channel 16 power.
- Extended `marine-sdr-smoke` to fail if channel power is not produced.
- Added `AudioLevelSink`, a GNU Radio sink that consumes demodulated float audio and emits reduced-rate audio levels.
- Added an NFM demodulation branch to `ChannelReceiver`:
  - filtered channel IQ,
  - GNU Radio quadrature demodulator,
  - audio sample counting,
  - audio level in dBFS.
- Wired the recorder GUI audio meter to live Channel 16 audio level.
- Extended `marine-sdr-smoke` to fail if demodulated audio samples or audio-level updates are not produced.

## Current Step

Step 9 is complete: `marine-core` now demodulates Channel 16 NFM audio enough to report audio sample counts and reduced-rate audio level.

## Verification

- Configured the project with CMake using `/tmp/zapiska-build`.
- Built the `marine-recorder-gui` target successfully.
- Built and ran `marine-sdr-smoke` against a temporary gr-osmosdr file source.
- Verified that `marine-sdr-smoke` reports non-zero `samplesRead` and a wideband power value.
- Rebuilt the `marine-recorder-gui` target after wiring the SDR controls.
- Rebuilt `marine-recorder-gui` and `marine-sdr-smoke` after adding `ChannelReceiver`.
- Verified that `marine-sdr-smoke` reports Channel 16 samples and channel power for both:
  - the temporary file source,
  - the attached HackRF One.
- Rebuilt `marine-recorder-gui` and `marine-sdr-smoke` after adding NFM demodulation.
- Verified that `marine-sdr-smoke` reports Channel 16 audio samples and audio level for both:
  - the temporary file source,
  - the attached HackRF One.

## Left To Do

1. Add WAV recording for Channel 16.
2. Add JSON sidecar metadata for recordings.
3. Add a configurable second channel display.
4. Add per-channel squelch state and threshold controls.
5. Add per-channel recording controls.
6. Add a separate playback GUI for recorded WAV files.
7. Add squelch-gated segment recording and timeline metadata.

## Immediate Next Step

Add WAV recording for Channel 16.

Acceptance criteria:

- Add a core recording path that can write Channel 16 demodulated audio to a WAV file.
- Keep the first recorder manually controlled and continuous; squelch-gated segmenting remains a later step.
- Enable the GUI Record control only when the SDR is streaming and Channel 16 audio is available.
- Surface recording errors in the GUI.
- Extend smoke or add a focused recorder check that proves a WAV file is created with non-zero audio frames.

## Notes

- Primary recorder backend decision: use GNU Radio with gr-osmosdr, matching the working Gqrx stack.
- The Record control remains disabled until audio recording exists.
- Channel 16 remains the only default-enabled/default-recording channel.
- Channel selection is runtime-only for now; selected channels are not persisted.
- The code should continue to keep the core library independent from GUI-specific behavior.
