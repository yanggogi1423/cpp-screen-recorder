# Validation snapshot — 2026-10-27

## Environment

Windows 11 build 26200 / MSVC 19.38 / Windows SDK 19041 with project-local C++/WinRT 2.0.240405.15 / Qt 6.8.3 / FFmpeg 9.0.2.

Final source commit `526b0c3` passed Windows and macOS build, core tests, QML smoke and deployment in [Actions run 40485999100](https://github.com/yanggogi1423/cpp-screen-recorder/actions/runs/40485999100). The macOS runner is arm64; this is not physical-device capture validation.

## Recorded media

- WGC captured this application's own visible HWND. An animated marker was updated in that window; no third-party window or microphone was selected.
- Two child processes rendered quiet 440 Hz and 880 Hz tones. Process-loopback captured each into its own RF64 WAV.
- During recording, route 1 gain changed from 1.0 to 0.5. RMS changed from approximately 353 to 176.
- Route 2 was muted and unmuted. PCM RMS was 0 in the muted interval and approximately 353 afterward.
- Each separated route's intended frequency exceeded the other test frequency by more than 20×. In practice the measured unwanted component was below 0.02 versus approximately 499 for the intended tone.
- The final H.264 640×360 / 30 FPS file contained a default mixed AAC stream and two independent AAC streams. Both tones were measured in the mixed stream, approximately 498 and 500 amplitude.
- Decoded frame hashes showed 150 distinct frames in a 5-second run. This verifies changing content, not only container metadata.

Reproduce using `tools/test-windows-capture.ps1`; analysis is in `tests/verify-recording.py`. Test media/logs are intentionally ignored by Git.

## Editor and deployed app

The deployed executable was launched without the development Qt directory in PATH. It loaded a real generated 5-second recording. The test exercised the actual QML save-choice dialog and its new-file / overwrite buttons, rather than directly calling the export method.

- Exporting 0.5–2.5 seconds yielded approximately 2.02 seconds, including AAC encoder padding.
- H.264 video and all three AAC audio streams remained present.
- New-file export preserved the input SHA256.
- Overwrite was tested only on a separate generated fixture copy and successfully replaced it.
- The original video/WAV recovery files were retained.
- Editor first-frame preparation is silent and pauses before user playback. A test assertion checks the paused state.

## Bugs found and status

1. Hidden HWND capture returned `E_INVALIDARG`. The test explicitly shows only its own window without activating it; the product validates that a selected HWND is visible.
2. Older SDK C++/WinRT headers conflicted with C++20. Modern project-local generated projections fixed the build without editing the system SDK.
3. macOS libc++ lacked `std::jthread`; the core now owns and joins `std::thread` explicitly. Missing CoreVideo linkage was also corrected. CI passed afterward.
4. Offscreen Qt font enumeration produced missing glyphs. Registering the installed Windows font for app use fixed the test rendering; the font is not bundled.
5. A moving-window integration run timed out and produced a longer-than-requested video. GPU map/copy work and frame sharing previously used the same mutex. They were separated and capture shutdown ordering was tightened. Two subsequent real runs passed at approximately five seconds; one was 151 frames and another 150, within one timer interval. This supports the improvement but does not establish the original timeout's complete root cause. Long-run timing/stress tests remain required before release.
6. QML teardown initially referenced a destroyed controller. The QML engine now dies before its context object, and the latest native test exit log contains no such binding errors.
7. A test script passed `.125` to FFmpeg, which rejects that duration syntax. The fixture now uses `0.125`; both the mix spectrum and subsequent editor checks passed.
8. Automated include sorting reordered Windows SDK headers that require property-key definitions. The required includes are now explicit and automatic include sorting is disabled in the repository formatter configuration. The final build and tests passed after this correction.

## Not yet acceptance-tested

Physical Mac screen/audio permissions and capture; real microphones and multiple hardware devices; OS-mixer volume influence; whole-display and drag-region UX; mixed-DPI monitors; device unplug/replug; sleep/wake; unsupported GPU encoders; 4K/high-FPS sustained load; long recordings; full disks; cancellation during large exports; signed installers and updates.

The local Windows package is a development preview, not a production-stability claim. Feature auditing and the remaining acceptance tests precede public binary release.
