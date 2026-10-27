# Third-party components

FARecorder uses dynamically linked Qt 6.8.3 (Core, Gui, Qml, Quick, Quick Controls, Multimedia and dependencies). Qt and its bundled components retain their respective licenses. Qt sources and notices are available from the matching module archives at https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ and in the Qt source `LICENSES` directories. The deployment does not statically link Qt; replacement shared libraries must match the deployed Qt ABI.

FFmpeg is run as a separate executable. The Windows development engine is the Gyan 9.0.2 essentials build, configured with GPL components including libx264. Preserve the build's LICENSE and README alongside any redistributed executable. Build information, source and build scripts are provided by https://www.gyan.dev/ffmpeg/builds/ ; upstream source is at https://ffmpeg.org/releases/ . The Qt Multimedia FFmpeg libraries are a separate Qt-provided playback dependency and keep their own notices.

Microsoft C++/WinRT tooling (project-local development dependency) is under the MIT license: https://github.com/microsoft/cppwinrt . Windows system libraries and macOS frameworks are supplied by their operating systems.

No license is assigned to the project's original source in this initial development snapshot. A distribution policy and complete third-party source/notice bundle must be confirmed before a public binary release. CI artifacts are development builds, not signed product releases.
