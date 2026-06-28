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
- Added live audio monitoring:
  - `SdrSource` live-audio enable/disable API,
  - GNU Radio audio sink backend,
  - per-channel audio gains,
  - mixed playback of all active channel receivers,
  - recorder GUI Monitor/Mute control.
- Extended `marine-sdr-smoke` with `--live-audio` for explicit monitor-path checks.
- Added audio-level squelch:
  - default per-channel threshold,
  - open/squelched channel state,
  - GUI squelch state display,
  - live monitor muting for squelched channels.
- Extended `marine-sdr-smoke` to fail if squelch state is not produced.
- Added manual per-channel squelch controls:
  - automatic/open/muted mode in the channel table,
  - editable squelch threshold in dBFS,
  - runtime `SdrSource::setChannelSquelch` updates for active receivers,
  - immediate live monitor gain refresh when a channel is forced open or muted.
- Extended `marine-sdr-smoke` with `--manual-squelch-check` to verify forced-open,
  forced-muted, and threshold updates.
- Added continuous Channel 16 WAV recording:
  - `SdrSource` start/stop recording API,
  - GNU Radio `wavfile_sink` branch connected to demodulated channel audio,
  - recording state and output path in stream stats,
  - recorder GUI Record/Stop Rec toggle,
  - timestamped WAV path generation under the user's music directory.
- Extended `marine-sdr-smoke` with `--record-wav` to verify that a WAV file is
  created with a non-empty audio data chunk.

## Current Step

Step 13 is complete: Channel 16 can be manually recorded to a continuous WAV file
while the SDR is streaming.

Step 14 is reprioritized: replace the current add/remove channel picker with
all-channel controls. Every channel from `data/marine_channels.json` should appear
in the channel table. A `Selected` checkbox marks whether that channel is active.
Selected channels are the only channels included in the SDR config, receiver graph,
live monitor/playback mix, and later recording controls.

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
- Rebuilt `marine-recorder-gui` and `marine-sdr-smoke` after adding live audio monitoring.
- Verified that `marine-sdr-smoke --live-audio` opens the monitor path against the temporary file source.
- Rebuilt `marine-recorder-gui` and `marine-sdr-smoke` after adding squelch state.
- Verified that `marine-sdr-smoke` reports squelch state for both:
  - the temporary file source,
  - the attached HackRF One.
- Rebuilt `marine-recorder-gui` and `marine-sdr-smoke` after adding manual squelch controls.
- Verified `marine-sdr-smoke --manual-squelch-check` against the temporary file source.
- Verified `marine-sdr-smoke --live-audio --duration-ms 300` opens the live monitor path.
- Verified `marine-sdr-smoke --manual-squelch-check --device-args hackrf=0 --sample-rate 2000000 --duration-ms 2000`
  against the attached HackRF One.
- Rebuilt `marine-recorder-gui` and `marine-sdr-smoke` after adding WAV recording.
- Verified `marine-sdr-smoke --record-wav --duration-ms 300` creates a WAV file
  with non-zero audio data bytes against the temporary file source.

## Left To Do

1. Replace the dropdown/add/remove channel selection UI with an all-channel table:
   every catalog channel is listed, each row has a `Selected` checkbox, and Channel
   16 plus any `enabled_by_default` channels start selected.
2. Make selected channels the active channel set:
   build `SdrSourceConfig::channels` only from selected rows, show loaded/selected/
   visible channel counts, and prevent stale unselected channels from being
   monitored or recorded.
3. Add a `Show selected only` toggle that filters the table view without changing
   channel selection state.
4. Add multi-channel live playback/monitor controls before expanding recording:
   selected active channels should be playable at the same time, and the GUI should
   show how many channels are currently selected/active in the monitor path.
5. Add a playback GUI/workflow for existing recordings before adding broader
   per-channel recording controls.
6. Add JSON sidecar metadata for recordings.
7. Add per-channel recording controls for selected active channels.
8. Add squelch-gated segment recording and timeline metadata.

## Immediate Next Step

Replace the current channel dropdown/add/remove workflow with selected-channel
controls in the channel table.

Acceptance criteria:

- Remove the catalog dropdown, `Add Channel`, and `Remove Selected` controls from
  the planned primary workflow.
- Load every catalog channel into the table instead of only visible/added channels.
- Add a `Selected` checkbox column.
- Treat checked rows as active and unchecked rows as inactive.
- Keep Channel 16 selected by default, along with any other channels marked
  `enabled_by_default`.
- Build the SDR channel config only from selected rows.
- Add a `Show selected only` toggle that hides unchecked rows without changing
  their checked state.
- Update the channel count label to distinguish loaded, selected, and visible rows.
- Define connected/streaming behavior clearly: either disable selection changes
  while the SDR is open/streaming, or mark them as pending until reconnect/restart.
- Rebuild `marine-recorder-gui`; add or update a focused GUI/core check if there is
  an existing practical test seam for selected-channel config generation.

## Notes

- Primary recorder backend decision: use GNU Radio with gr-osmosdr, matching the working Gqrx stack.
- The first Record control is manually controlled and records Channel 16 continuously;
  squelch-gated segmenting remains a later step.
- Channel 16 remains the default-selected/default-recording channel.
- Channel selection and squelch settings are runtime-only for now; they are not persisted.
- The previous dropdown/add/remove channel UI was useful for proving multiple
  receiver branches, but it should be replaced rather than expanded.
- Playback/multi-channel monitor behavior should be stabilized before adding
  multi-channel recording controls.
- The code should continue to keep the core library independent from GUI-specific behavior.
