# 0.2 validation — 2026-10-27

Windows 11 build 26200, Qt 6.8.3, MSVC 19.38, FFmpeg 9.0.2, NVIDIA RTX 4070. The app remains C++20/Qt; generated artwork and ICO/ICNS are static assets, not runtime AI dependencies.

## Automated checks

- Settings, live gain/mute/clipping, RF64 gaps/overlap/tail checks retained.
- Pause math: open/closed/multiple pauses, packets spanning pause boundaries, removal of paused audio, adjusted timestamps, stop-time freeze and rejection of post-stop packets.
- Five-second audio-check pipeline tested with a synthetic capture implementation, including sample writing and actual peak values.
- Simple and expanded QML views load and render separately. A third UI case enters 1600×900, 55 FPS and a seven-second delay, switches both views and verifies that the simple controls still display the actual values.
- CPU encoder preflight runs real FFmpeg encoding using the selected dimensions and frame rate. It does not infer support from an encoder-name list.
- Test settings use unique temporary profiles and do not overwrite user preferences.

## Actual Windows workflow

`tools/test-windows-workflow.ps1` launches the deployed app without the development Qt PATH. It captures only the app's own HWND and two self-generated quiet 440/880 Hz child-process tones. No user microphone or another application's window is selected.

The workflow covers audio audition → encoder preflight → native hotkey dispatcher → record → gain/mute changes → pause → resume → stop → multitrack save.

- Audition produces a 5.0-second RF64 sample and real level readings. The workflow clicks the actual sample-listen button, verifies loaded duration/playing state, stops playback and then starts recording.
- Global hotkeys register with the OS. Tests post `WM_HOTKEY` to the application's native dispatcher; this exercises the real handler and controller state transitions without sending keys to a user's foreground application. Physical keyboard interaction still needs manual acceptance testing.
- Approximately six seconds of wall time with one second paused yields approximately five seconds of H.264 640×360 / 30 FPS video. AAC padding adds a small container-duration difference.
- PCM analysis confirms source isolation, 50% recorded gain, mute/unmute and both source frequencies in the default mixed stream.
- Independent tracks and the default mix remain present; decoded frame hashes confirm changing content.
- The separate paused-stop test stops before resuming. Only the pre-pause video/audio are retained. Stop freezes the pause metadata before capture teardown and muxing.
- Simple/expanded mode switches preserve output settings and selected sources.
- The real HWND style is checked to confirm the Windows native caption is absent.

Results are written to ignored `artifacts/workflow-verification.json`, `workflow-normal-result.json`, `paused-stop-result.json` and `audition.wav`.

## Encoder behavior

CPU H.264 is validated automatically. On this RTX 4070 machine, NVENC is checked separately. AMF, which is not available on this machine, is used to exercise the failure path: report unavailable, return to idle, retain the user's choice and do not silently switch encoders. Low-level FFmpeg output is stored in diagnostics; the UI gives a short action-oriented message.

One offscreen encoder fixture timed out during development. The probe now explicitly disables interactive stdin, tests use a fresh settings profile instead of a reusable PID-only profile, and fixture stage timing is logged. Subsequent runs are checked with that trace; the complete cause of the earlier isolated timeout was not established. A 15-second production timeout and cancellation remain in place.

## Visual and packaging checks

The rendered views are inspected for readable text, clipped controls, black/gray consistency, generated asset loading and the expanded side panel. Windows uses a frameless window with application window controls. The EXE contains an icon and version resource; macOS includes an ICNS bundle asset. Transport controls also have accessible names/tooltips.

The existing editor's new-file/overwrite confirmation and multitrack trim path are retained and regression-tested against generated recordings. Large overwrite copies still use the existing safe commit path; moving that file-copy step off the UI thread is follow-up work.

## Boundaries

macOS must pass build/core/UI CI, but physical Mac capture, microphone/loopback permission and hotkey behavior remain unverified here. Its microphone path still uses the system default input. Real microphone devices, OS-mixer influence, mixed-DPI monitors, high-FPS/4K endurance, sleep/device recovery, signed installers and updates are not claimed complete. A three-frame encoder probe validates startup/configuration, not sustained recording performance.
