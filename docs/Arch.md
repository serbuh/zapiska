# Marine Recorder Architecture

## Goal

Build a standalone application that uses a HackRF to monitor and record marine VHF channels.

The first useful version records Marine Channel 16 and shows a simple live signal view. The design should also support enabling a second nearby channel early, then later expanding to a wider marine-band capture with multiple channel receivers, squelch-aware recording, and timeline playback.

This project should not depend on Gqrx internals. Gqrx is useful as a reference and manual validation tool, but the recorder should be its own application with a shared core library.

## Initial Scope

The MVP should provide:

- HackRF device connection.
- Tuning around Marine Channel 16.
- One shared IQ stream.
- One enabled `ChannelReceiver` for Channel 16.
- Optional config-driven second channel for display and later recording.
- Narrowband FM demodulation.
- Simple recorder GUI with signal display.
- Continuous WAV recording for Channel 16.
- Metadata written next to each recording.

Marine Channel 16:

```text
Channel: 16
Frequency: 156.800 MHz
Mode: NFM voice
```

The second channel must be configurable rather than hardcoded. This keeps early experiments flexible and avoids tying the architecture to a single channel pair.

## Application Shape

Use separate applications backed by a shared core library:

```text
libmarine-core
  SDR device access
  IQ streaming
  channel receivers
  filtering and demodulation
  squelch detection
  audio recording
  metadata/index writing

marine-recorder-gui
  connects to HackRF
  starts/stops receiving
  shows live channel signal state
  records audio

marine-playback-gui
  opens recording folders
  shows recordings and timeline data
  plays recorded audio
```

The playback GUI can be integrated into the recorder GUI later. Keeping it separate at first makes the recording path easier to stabilize.

## Technology Choices

Recommended stack:

- C++ for the core and GUI.
- Qt for the recorder and playback GUIs.
- SoapySDR for HackRF access.
- liquid-dsp or focused internal DSP code for filtering, frequency shifting, resampling, and NFM demodulation.
- WAV for first audio recordings.
- JSON sidecar files for recording metadata.

SoapySDR is preferred over direct HackRF-only APIs because it keeps the device layer portable. HackRF remains the first supported target.

## Signal Pipeline

The core processing model should be:

```text
HackRF source
  -> shared IQ stream
  -> ChannelReceiver[16]
  -> FM demodulator
  -> audio filter/resampler
  -> squelch detector
  -> recorder
  -> metadata/index
```

For two channels:

```text
HackRF source
  -> shared IQ stream
  -> ChannelReceiver[16]
  -> ChannelReceiver[configured second channel]
```

Each channel receiver is responsible for:

- Frequency shifting from the shared center frequency to the channel offset.
- Channel filtering.
- Power/signal measurement.
- NFM demodulation.
- Audio level measurement.
- Squelch state.
- Optional audio recording.

Even when only Channel 16 is enabled, the code should still use the `ChannelReceiver` abstraction. Later multi-channel support should be an expansion of the same model, not a rewrite.

## Tuning Model

Do not make the application tune directly to a single channel and assume that only one channel exists. Tune HackRF to a center frequency and compute per-channel offsets in software.

Example for early one-channel use:

```text
HackRF center frequency: 156.800 MHz
Sample rate:            2.0 MS/s

Channel 16 frequency:   156.800 MHz
Channel 16 offset:      0 Hz
```

Example with a second nearby channel:

```text
HackRF center frequency: 156.800 MHz
Sample rate:            2.0 MS/s

Channel 16 frequency:   156.800 MHz
Channel 16 offset:      0 Hz

Second channel:         configured by user
Second offset:          channel_frequency - center_frequency
```

This is the same model needed later for a wider marine-band capture:

```text
HackRF center frequency: selected to cover target channels
Sample rate:            4.0-8.0 MS/s or as required

ChannelReceiver[06]
ChannelReceiver[08]
ChannelReceiver[09]
ChannelReceiver[12]
ChannelReceiver[13]
ChannelReceiver[14]
ChannelReceiver[16]
...
```

## Recorder GUI MVP

The recorder GUI should be simple and operational, not a full SDR workstation.

Primary controls:

- Device selector.
- Connect/disconnect.
- Center frequency.
- Sample rate.
- RF/LNA/VGA gain controls if available.
- Start/stop receiving.
- Start/stop recording.
- Recording folder selector.

Shared receiver status:

- Device state.
- Center frequency.
- Sample rate.
- Gain values.
- Dropped sample count if available.
- Current recording folder.

Per-channel display:

```text
Channel 16 | 156.800 MHz | signal meter | audio level | squelch state | recording state
```

For the first GUI signal view, prioritize:

- Channel power meter.
- Audio level meter.
- Simple narrow spectrum around the channel.

A waterfall can be added later. It is useful, but not required for the first working recorder.

## Threading Model

Keep realtime receiving and GUI drawing separate:

```text
SDR thread
  reads IQ samples from HackRF/SoapySDR

DSP thread
  distributes IQ blocks to channel receivers
  demodulates audio
  computes power/spectrum/squelch state
  writes audio to recorders

GUI thread
  handles user input
  displays reduced-rate signal data
  starts/stops receiver and recorder state
```

The GUI should receive display summaries, not the full high-rate IQ stream. This keeps rendering and user interaction from blocking radio processing.

## Recording Format

For the first version, record continuous WAV audio for Channel 16:

```text
recordings/
  2026-06-27/
    ch16_156800000_143522.wav
    ch16_156800000_143522.json
```

Sidecar metadata example:

```json
{
  "channel": "16",
  "frequency_hz": 156800000,
  "mode": "nfm",
  "audio_sample_rate_hz": 48000,
  "started_at": "2026-06-27T14:35:22Z",
  "device": "hackrf",
  "center_frequency_hz": 156800000,
  "iq_sample_rate_hz": 2000000,
  "squelch_enabled": false
}
```

Early metadata should be written even before a full recording index exists. It makes playback, debugging, and later migration easier.

## Configuration

Start with a small channel configuration file:

```yaml
channels:
  - id: "16"
    name: "Marine Channel 16"
    frequency_hz: 156800000
    mode: "nfm"
    enabled: true
    record: true

  - id: "second"
    name: "Configured second channel"
    frequency_hz: 156625000
    mode: "nfm"
    enabled: false
    record: false
```

The recorder should be able to run with only Channel 16 enabled. The second channel is for early validation of the shared-IQ, multi-receiver architecture.

## Playback GUI MVP

The first playback GUI does not need SDR device access.

Initial features:

- Open recording folder.
- List WAV recordings and metadata.
- Play/pause selected recording.
- Seek within a recording.
- Show basic waveform or level overview if cheap to compute.

Later playback features:

- Timeline view across channels.
- Squelch segment display.
- Overlapping transmissions on different channels.
- Per-channel mute/solo.
- Jump to active transmissions.

## Future Wideband Recording

Later, the system should support recording a wider spectrum that contains many marine channels.

The likely future modes are:

1. Demodulate many channels live from one wide IQ stream.
2. Record only unsquelched audio segments per channel.
3. Optionally record raw wideband IQ for debugging or forensic replay.

The long-term data model should track timeline segments:

```text
segment:
  channel: 16
  frequency_hz: 156800000
  start_time: 2026-06-27T14:35:25.120Z
  end_time:   2026-06-27T14:35:31.840Z
  audio_file: segments/ch16_20260627_143525_120.wav
```

This supports a playback interface where all unsquelched channel activity appears on the same time axis.

## Milestones

1. Create standalone project structure with shared core library and recorder GUI target.
2. Open HackRF through SoapySDR and stream IQ samples.
3. Tune around Channel 16 and compute basic IQ power.
4. Implement `ChannelReceiver` for one configured channel.
5. Add NFM demodulation and audio resampling.
6. Add recorder GUI with connect, start/stop, and Channel 16 signal meters.
7. Record continuous Channel 16 WAV files with JSON metadata.
8. Add optional second configured channel display.
9. Add per-channel squelch state.
10. Add per-channel recording control.
11. Add basic playback GUI for recorded WAV files.
12. Add squelch-based segment recording and timeline metadata.
13. Expand to wider marine-band capture and many channel receivers.

## Non-Goals For The First Version

- Full Gqrx-like SDR controls.
- Waterfall-heavy interface.
- Wideband IQ archive by default.
- Multi-channel timeline playback.
- Automatic marine channel database management.
- Network streaming.

These are valid later features, but they should not block the first Channel 16 recorder.

## Open Decisions

- Exact DSP implementation: liquid-dsp vs focused internal DSP.
- Initial audio sample rate: 16 kHz vs 48 kHz.
- Initial second channel choice.
- Whether first recording mode is always-continuous or optionally squelch-gated.
- Whether recorder and playback live in one process later or remain separate applications.

