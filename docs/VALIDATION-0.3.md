# FARecorder 0.3 validation — 2026-10-27

Windows 11 build 26200, Qt 6.8.3, MSVC 19.38, FFmpeg 9.0.2. This extends [0.2 validation](VALIDATION-0.2.md).

## Automated checks

Six local CTest cases pass: core, simple QML, expanded QML, custom-resolution/FPS/delay QML, settings migration/persistence, and actual CPU encoder preflight. The QML cases assert FARecorder branding, MP4 defaults, visible normal menus/folder controls and the Help action opening the version dialog.

The isolated settings case seeds the former Luma profile, checks migration of the output folder, FPS and expanded mode with MP4 as the new default, then reconstructs the controller and verifies that a subsequently selected MKV format, Unicode folder path and simple mode persist. Legacy settings are left intact. All fixtures use unique IniFormat profiles; migration also explicitly uses that format to avoid reading real user settings during tests.

## Actual Windows recording

`tools/test-windows-workflow.ps1` uses only this application's own HWND and two quiet synthetic tone processes. It checks native hotkey registration, audition/sample playback, encoder preflight, then real WGC/WASAPI capture.

- The recording starts in a **400×104 logical-pixel** compact window. Normal header, menus and normal content are hidden.
- The real restore button returns to the normal view; switching to expanded preserves recording settings. Re-entering compact mode keeps the same session.
- The real compact pause and stop buttons are exercised. Resume goes through the native hotkey dispatcher. No keyboard input is sent to other applications.
- Stop restores the expanded view and its menus before save/editor completion.
- The selected custom output root contains `FARecorder_…/recording.mp4`.
- Approximately six wall-clock seconds with a 0.998-second pause produced 5.033 seconds of 640×360 H.264 video, 151 frames and three AAC tracks (default mix plus two independent tracks). Decoding found 147 distinct frames.
- Source isolation, recording-only gain from RMS 353 to 176, mute RMS 0 and unmute RMS 353 passed. The five-second audition remained valid.
- A separate pause-then-stop case produced 2.033 seconds of video and 0.817 seconds of pause metadata, with the normal view restored.

The first compact test exposed a real minimum-size binding order issue: the resize could run before the normal minimum dimensions were relaxed. The resize now runs on the next QML event turn after those bindings update; both normal-stop and paused-stop workflows pass with exact compact dimensions. A previously open Luma instance initially held the hotkeys; it was closed through the normal window-close request, without force-killing or bypassing its recording guard.

## Editor and packaging

`tools/test-windows-editor.ps1` opens the generated MP4, verifies the paused first-frame preview, and clicks the actual save-choice buttons for the 0.5–2.5-second interval. Both outputs were 2.0 seconds with three AAC tracks. Save-new preserved the original SHA256. Overwrite replaced only a dedicated test copy.

The EXE version resource reports FARecorder / FARecorder.exe / 0.3.0-alpha.1. Simple, expanded, compact and About renders are inspected for clipping and readable controls. Portable packaging includes the Qt deployment, FFmpeg and component notices; its extracted executable is smoke-tested without the development Qt PATH.

The Windows/macOS build matrix is available in [GitHub Actions](https://github.com/yanggogi1423/cpp-screen-recorder/actions). macOS runs core, QML and settings checks; its physical recording, permissions, microphone and hotkeys remain unverified here. CI is not a substitute for those device tests. High-FPS/4K endurance and signed installers remain outside this pass.

## D2Coding update — 0.3.0-alpha.2, 2026-10-28

D2Coding 1.4.0 standard Regular/Bold are embedded from NAVER's official release, with the unchanged OFL copyright/license included in sources, Qt resources and deployment. The global application font, main window and both timers use D2Coding. No system fonts are installed.

All six local checks pass. Simple, expanded and About renders are inspected for Korean/Latin text, numbers and control clipping. The actual Windows workflow additionally renders the 400×104 compact recording view and verifies recording, pause, output and independent audio tracks with the new font.

This update also exposed a Korean-only MSVC installation whose CMake `/showIncludes` prefix was corrupted, leaving zero Ninja header dependencies and stale objects after a metadata header change. The build script now applies the correct project-local prefix only for that language-resource combination and offers `-Clean` to rebuild old objects. Dependency inspection reports 241 headers for the controller; touching AppInfo.h schedules both dependent sources for rebuilding. English CI installations retain normal prefix detection.
