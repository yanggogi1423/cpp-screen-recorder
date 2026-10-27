# Recorder interface — 0.2

The interface is a recording instrument with one transport area, not a general dashboard. It uses neutral black/gray surfaces, small separators, restrained typography and red only for recording/critical actions. It has no purple accent, decorative grid or marketing subtitle.

## Two views, one configuration

- **Simple** is the default. It contains screen/window/region selection, live preview, common resolution/FPS/delay choices, microphone/computer-audio switches, a five-second audio check, sample playback, the timer and record/pause/stop controls.
- **Expanded** opens a side panel for exact dimensions/FPS, CPU/GPU encoder selection and checking, file format, delay, cursor, per-application/device mixer, hotkey status and output folder.
- Changing view does not recreate the recording session, reset the sources or change output settings. The chosen view is remembered. A running recording retains the same controls in either view.
- During recording the simple audio switches mute/unmute selected routes; they cannot add a new capture source. A route that was not selected is disabled until recording ends.

On Windows the native caption is replaced by a compact application header with drag, minimize, maximize/restore and close controls, plus edge resizing. The window size is constrained to the available logical screen dimensions. macOS keeps the native window frame.

## Generated artwork

`app/assets/app-icon.png`, `record.png`, `stop.png`, `pause.png` and `resume.png` were generated specifically for this project with the built-in ImageGen tool. The icon uses a red recording circle in a black tile. Transport controls use separate transparent raster glyphs. They retain text tooltips and accessible names, so their meaning does not depend on the artwork alone. ICO and ICNS are packaging conversions of the generated app icon.

## Reference principles

- [Cap](https://cap.so/): a short, direct capture workflow.
- [Screen Studio](https://screen.studio/): clear capture choices and a focused recording/editing experience.
- [OBS mixer guide](https://obsproject.com/kb/audio-mixer-guide): clear source identity, independent levels, mute and meters.

These are references for interaction structure; no third-party artwork or branding is included.
