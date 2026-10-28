# FARecorder architecture

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

The CPU platform boundary produces immutable BGRA images on demand and interleaved signed 16-bit stereo PCM at 48 kHz. Native GPU encoders consume the retained platform surface instead of the CPU image. Every audio packet has a timestamp in the shared monotonic clock domain. The RF64 writer inserts silence for gaps, trims overlapping packets and pads silent tails. OS APIs and permission handling stay in their platform modules.

`RecordingSession` owns a worker thread, the FFmpeg process and audio capture lifetimes. The GUI remains responsive while encoding and muxing. Capture source changes are locked while recording. Mix gains and mute use independent shared atomic state, so microphone and app routes can be adjusted while recording without changing the operating system's volume settings. Gain is applied to recorded PCM before track writing. Muting writes silence and keeps the track timeline intact; unprocessed audio is not retained.

## Mixer semantics

- Default: **recording-only mixer**. Gain 0–200%, mute and a post-gain peak meter per selected route.
- System endpoint controls, application playback volume, and hardware microphone gain are separate OS controls.
- The application never calls OS volume setters. A recording slider does not turn down speakers or change a microphone used by a call.
- OS controls can still affect the input received by capture. Their behavior must be measured for each API/OS/device; it is not promised to be identical on Windows and macOS.
- An OS-linked mode is deliberately not exposed until its scope, feedback loop prevention, permissions and restoration behavior are designed and tested.
- The UI makes system audio and application audio mutually exclusive to avoid accidental duplicate capture. Microphones remain independently selectable.
- Distinct browser tabs or Discord call participants within the same process are not isolated sources.

## Files and recovery

Each recording stages in the application-local Recovery area with `video.mkv` (CPU) or `video.mp4` (native GPU), RF64 WAV tracks, a start-time-named combined recording, logs and `session.json`. Save copies the combined video directly into the chosen destination without recording-specific subdirectories. Pending state clears only on confirmed save. The final video has separately named AAC audio tracks. When two or more routes are selected, a default mixed playback track is also included for ordinary players, alongside the individual tracks.

The trim editor decodes and re-encodes the chosen interval, retaining every audio stream. Before exporting, the app explicitly asks whether to save a new file or overwrite the original combined video. Overwrite first generates a complete sibling temporary file, then uses `QSaveFile` for an atomic replacement. A failed export preserves the original. The separate WAV / `video.mkv` recovery sources are unaffected. Source deletion and intermediate-file cleanup require a later explicit product design.

## Current limits

This is a beta architecture with explicit validation scope, not a claim of production parity. CPU/FFmpeg options read GPU frames on demand before the raw BGRA pipe. The native_gpu option uses D3D11 NV12 surfaces with a hardware-verified Media Foundation transform on Windows, or Metal/CoreImage plus hardware-required AVAssetWriter/VideoToolbox on macOS. Capture callbacks retain GPU surfaces; only preview/CPU callers perform readback. Compact mode stops preview readback. High-resolution/high-FPS selection is a request, not a guaranteed sustained frame rate. GPU encoder availability is determined by the installed hardware and driver; there is no silent fallback. WGC is window/display capture, not an injected game-hook backend. SDR BGRA is supported; HDR/10-bit is not.

macOS code must compile in CI and pass physical-Mac permission, capture, timestamp and audio-device tests before it is called release-tested. Signing, notarization and physical bundle-update validation remain separate release work. GitHub update discovery/download and a standalone swap/rollback helper are implemented; see UPDATES.md.

## Primary references

- [Qt deployment](https://doc.qt.io/qt-6/cmake-deployment.html)
- [Microsoft Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [Microsoft process audio capture sample and OS floor](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/)
- [Apple ScreenCaptureKit sample](https://developer.apple.com/documentation/screencapturekit/capturing-screen-content-in-macos)
- [FFmpeg download providers](https://ffmpeg.org/download.html)

## Product shell (0.3)

Public application/executable/bundle names are FARecorder. Internal library namespaces and the QML URI remain `luma` / `Luma`. AppInfo.h is the displayed release metadata. QSettings uses FARecorder/FARecorder and migrates the former Luma/Luma Recorder preferences once, leaving the former data in place and starting the renamed product in MP4. Fixture profiles explicitly use the isolated IniFormat for migration as well.

Compact mode is controller state, allowed only while recording/paused. It never starts a new session or changes capture/mixer settings. The QML window preserves normal geometry and maximization, updates minimum dimensions before shrinking, hides the normal header/menu/content and restores before save/error/editor. Preview image copying stops while compact; capture, encoding, meters and elapsed timing continue.

## Live region translation (0.3.0-alpha.3)

RegionGeometry converts a global logical selection to display-local physical pixels and keeps output dimensions even. The selected region initializes output dimensions to its native size. Windows selects QScreen by HMONITOR; macOS uses display logical bounds. VideoCapture::setRegion changes only the origin, with fixed dimensions and serialized frame access. Cached full-frame data makes static-source translation immediate. The floating frame uses a hollow QWindow mask with all controls outside the ROI and Windows capture exclusion. See REGION_RECORDING.md for verification and boundaries.

## Source discovery and shell (0.4)

SourceWatcher receives Windows WinEvent window-lifecycle/title events, display/device messages and IMMNotificationClient endpoint events. macOS uses NSWorkspace launch/termination, screen-change and CoreAudio device/default-endpoint notifications. Signals are debounced by 300ms. Active-app polling at 2.5s covers changing audio sessions or windows without useful OS events; returning to the app also schedules discovery.

QtConcurrent performs platform enumeration off the GUI thread. Snapshots are applied only while idle and outside audio audition. Equal snapshots are ignored. Video and audio model notifications are separate, and surviving source IDs reuse the original AudioMix pointer. Disappearing sources are deselected. Permission/query failure suspends polling until a relevant OS event or app reactivation.

Theme.qml and the Quiet components own appearance. The normal shell has one title/menu row and a full-width source/panel header; the compact shell retains only recording controls. Help contains static explanations, keyboard bindings and license documents. MIT applies only to original project material; bundled third-party components retain their own licenses.
