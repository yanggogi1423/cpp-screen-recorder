# Luma architecture

## Language and boundaries

The application is C++20 with Qt 6.8 / QML. There is no .NET runtime dependency.

```
QML UI / player / trim confirmation
        |
RecorderController — selection, countdown, settings, live mixer controls
        |
RecordingSession — shared clock, RF64 tracks, FFmpeg encoding and muxing
        |
Platform / VideoCapture / AudioCapture interfaces
        + Windows: Graphics Capture + D3D11, WASAPI process loopback / devices
        + macOS: ScreenCaptureKit, AVFoundation microphone, AVAudioConverter
```

The platform boundary produces BGRA frames and interleaved signed 16-bit stereo PCM at 48 kHz. Every audio packet has a timestamp in the shared monotonic clock domain. The RF64 writer inserts silence for gaps, trims overlapping packets and pads silent tails. OS APIs and permission handling stay in their platform modules.

`RecordingSession` owns a worker thread, the FFmpeg process and audio capture lifetimes. The GUI remains responsive while encoding and muxing. Capture source changes are locked while recording. Mix gains and mute use independent shared atomic state, so microphone and app routes can be adjusted while recording without changing the operating system's volume settings. Gain is applied to recorded PCM before track writing. Muting writes silence and keeps the track timeline intact; unprocessed audio is not retained.

## Mixer semantics

- Default: **recording-only mixer**. Gain 0–200%, mute and a post-gain peak meter per selected route.
- System endpoint controls, application playback volume, and hardware microphone gain are separate OS controls.
- The application never calls OS volume setters. A recording slider does not turn down speakers or change a microphone used by a call.
- OS controls can still affect the input received by capture. Their behavior must be measured for each API/OS/device; it is not promised to be identical on Windows and macOS.
- An OS-linked mode is deliberately not exposed until its scope, feedback loop prevention, permissions and restoration behavior are designed and tested.
- Selecting system audio and an included app simultaneously can duplicate sound. The UI explains this; automatic overlap detection is a follow-up.
- Distinct browser tabs or Discord call participants within the same process are not isolated sources.

## Files and recovery

Each recording gets a unique folder containing `video.mkv`, RF64 WAV tracks, `recording.mkv` or `recording.mp4`, logs and `session.json`. The final video has separately named AAC audio tracks. Typical media players play one audio track at a time; use an editor that supports multiple audio streams to mix them.

The trim editor decodes and re-encodes the chosen interval, retaining every audio stream. Before exporting, the app explicitly asks whether to save a new file or overwrite the original combined video. Overwrite first generates a complete sibling temporary file, then uses `QSaveFile` for an atomic replacement. A failed export preserves the original. The separate WAV / `video.mkv` recovery sources are unaffected. Source deletion and intermediate-file cleanup require a later explicit product design.

## Current limits

This is an alpha architecture and implementation, not a claim of production parity. The frame path currently reads GPU frames into CPU memory before FFmpeg. High-resolution/high-FPS selection is a request, not a guaranteed sustained frame rate. GPU encoder availability is determined by the installed hardware and driver; there is no silent fallback. WGC is window/display capture, not an injected game-hook backend. SDR BGRA is supported; HDR/10-bit is not.

macOS code must compile in CI and pass physical-Mac permission, capture, timestamp and audio-device tests before it is called release-tested. Signing, notarization and installer/update channels are separate release work.

## Primary references

- [Qt deployment](https://doc.qt.io/qt-6/cmake-deployment.html)
- [Microsoft Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [Microsoft process audio capture sample and OS floor](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [Apple ScreenCaptureKit sample](https://developer.apple.com/documentation/screencapturekit/capturing-screen-content-in-macos)
- [FFmpeg download providers](https://ffmpeg.org/download.html)
