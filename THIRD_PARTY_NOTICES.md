# Licenses and third-party components

FARecorder original source code, documentation and project-created artwork are licensed under the MIT License. Copyright (c) 2026 yanggogi1423. See the root `LICENSE`. Third-party code, fonts and binaries are excluded from that grant and retain their own licenses.

FARecorder uses dynamically linked Qt 6.8.3 (Core, Gui, Qml, Quick, Quick Controls, Multimedia and dependencies). Qt and its bundled components retain their respective licenses. Qt sources and notices are available from the matching module archives at https://download.qt.io/archive/qt/6.8/6.8.3/submodules/ and in the Qt source `LICENSES` directories. The deployment does not statically link Qt; replacement shared libraries must match the deployed Qt ABI.

FFmpeg is run as a separate executable. The bundled Windows engine is the unchanged Gyan 9.0.2 essentials build, configured with GPL components including libx264. Preserve the build's LICENSE and README alongside any redistributed executable. Build information, source and build scripts are provided by https://www.gyan.dev/ffmpeg/builds/ ; the matching upstream source is https://github.com/FFmpeg/FFmpeg/commit/946fcce07b and its downloadable archive is https://github.com/FFmpeg/FFmpeg/archive/946fcce07b.tar.gz . The bundled FFmpeg-README.txt records the exact engine version, build configuration and external library versions. The Qt Multimedia FFmpeg libraries are a separate Qt-provided playback dependency and keep their own notices.

D2Coding 1.4.0 standard Regular and Bold fonts are embedded without modification. Copyright (c) 2015 NAVER Corporation; licensed under SIL Open Font License 1.1. The original copyright and full license are in `app/assets/fonts/OFL.txt`, embedded in the application and deployed as `licenses/D2Coding/OFL.txt` (also inside the macOS bundle). Official release: https://github.com/naver/d2-coding-font/releases/tag/VER1.4.0 . No system font installation is performed.

Microsoft C++/WinRT tooling (project-local development dependency) is under the MIT license: https://github.com/microsoft/cppwinrt . Windows system libraries and macOS frameworks are supplied by their operating systems.

The MIT License for FARecorder does not relicense Qt, D2Coding or FFmpeg. Preserve the separate component notices and corresponding license/source information when redistributing a package. Development builds are unsigned previews.
