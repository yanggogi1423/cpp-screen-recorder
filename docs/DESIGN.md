# FARecorder interface — 0.3

The interface is a recording instrument with one transport area, not a general dashboard. It uses neutral black/gray surfaces, small separators, restrained typography and red only for recording/critical actions. It has no purple accent, decorative grid or marketing subtitle.

## Three views, one configuration

- **Simple** is the default. It contains screen/window/region selection, live preview, common resolution/FPS/delay choices, microphone/computer-audio switches, a five-second audio check, sample playback, the timer and record/pause/stop controls.
- **Expanded** opens a side panel for exact dimensions/FPS, CPU/GPU encoder selection and checking, file format, delay, cursor, per-application/device mixer, hotkey status and output folder.
- Changing view does not recreate the recording session, reset the sources or change output settings. The chosen view is remembered. Both normal views retain the recording mixer. Starting a session automatically enters the compact recording view.
- During recording the simple audio switches mute/unmute selected routes; they cannot add a new capture source. A route that was not selected is disabled until recording ends.

On Windows the native caption is replaced by a compact application header with drag, minimize, maximize/restore and close controls, plus edge resizing. The window size is constrained to the available logical screen dimensions. macOS keeps the native window frame.

## Generated artwork

`app/assets/app-icon.png`, `record.png`, `stop.png`, `pause.png` and `resume.png` were generated specifically for this project with the built-in ImageGen tool. The icon uses a red recording circle in a black tile. Transport controls use separate transparent raster glyphs. They retain text tooltips and accessible names, so their meaning does not depend on the artwork alone. ICO and ICNS are packaging conversions of the generated app icon.

## Reference principles

- [Cap](https://cap.so/): a short, direct capture workflow.
- [Screen Studio](https://screen.studio/): clear capture choices and a focused recording/editing experience.
- [OBS mixer guide](https://obsproject.com/kb/audio-mixer-guide): clear source identity, independent levels, mute and meters.

These are references for interaction structure; no third-party artwork or branding is included.

## Compact recording and menus

The 400×104 logical-pixel recording view contains status, elapsed time, pause/resume, stop and a restore button. Drag the timer area to move it. Its menu, normal header, preview and settings are hidden. Restore returns to the previous view and window geometry while recording continues, allowing live mixer changes. Stop/failure restores the normal window before saving/editing. Pause does not reopen or shrink the window again.

Normal simple/expanded views have File, Edit and Help menus. File selects/opens the configured recording root and exits through the existing close guard. Edit opens a video for trimming or edits the most recent file. Help shows the version, repository owner as creator, last source-update date and repository/issues links. The simple footer also exposes the configured path and folder chooser. Output defaults to MP4; MKV remains available in expanded settings.

## Typography — 0.3.0-alpha.2

D2Coding 1.4.0 standard Regular/Bold faces are embedded in Qt resources and loaded before any QML view is created on both platforms. The application default, window controls, menus and timer use the same family. This replaces the former Windows/macOS system-family split and Consolas timer override. The official unmodified fonts and OFL notice ship with the application; no OS font installation is needed.
