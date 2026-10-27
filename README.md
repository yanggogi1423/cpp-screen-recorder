# Luma Recorder

A modern desktop recorder built with **C++20 + Qt 6 / QML**, with native capture backends for Windows and macOS.

현재 **0.1.0 alpha 개발 중**입니다. 기능을 구현했다는 것과 실제 장비에서 검증했다는 것을 구분합니다. 최종 지원 상태와 테스트 증거는 [기능 점검표](docs/FEATURE_AUDIT.md)에 기록합니다. 아직 정식 릴리즈가 아닙니다.

![Luma의 첫 화면](docs/images/main.png)

## 만들고 있는 경험

- 전체 디스플레이 / 특정 창 / 드래그한 영역 녹화
- 해상도 직접 입력, 1–240 FPS 요청값, CPU 및 플랫폼별 GPU 인코더 선택
- 시작 지연 0–60초: 0이면 즉시, 기본은 3초
- 앱별 소리·마이크·시스템 소리를 독립된 오디오 트랙과 WAV로 저장
- **녹화 중에도** 트랙별 음량 0–200% / 뮤트 / 실제 입력에 따른 피크 표시
- 운영체제의 스피커·통화 마이크 음량과 분리된 녹화용 믹서
- 녹화 종료 후 재생·시작/종료 구간 자르기
- 편집 저장 시 **새 파일 저장 / 원본 덮어쓰기 / 취소**를 명시적으로 선택
- 어두운 테마, 보라색 포인트, 화면 미리보기, 설정 저장

## 사용 순서

1. **새로고침**으로 화면과 오디오 소스를 가져옵니다. macOS는 화면·오디오 녹화 권한이 필요합니다.
2. 전체 화면 / 특정 창 / 영역 지정을 고르고 소스를 연결합니다.
3. 녹음할 앱과 마이크를 선택합니다. Windows의 앱 목록에는 오디오 세션을 만든 프로그램이 표시되므로, 앱에서 소리를 재생한 뒤 새로고침하세요.
4. 해상도·FPS·인코더·시작 지연·저장 폴더를 정하고 녹화를 시작합니다.
5. 녹화 중에는 선택한 트랙의 음량과 뮤트를 조절할 수 있습니다. 선택한 소스 자체는 녹화 중 바꿀 수 없습니다.
6. 종료 후 편집 화면에서 재생·자르기를 하고 저장 방식을 선택합니다.

`Ctrl+Shift+R`은 현재 앱에 포커스가 있을 때 시작/종료합니다. 카운트다운 중 `Esc`로 취소할 수 있습니다. 전역 단축키는 아직 구현하지 않았습니다.

## 오디오 저장과 믹서

각 세션 폴더에는 영상 원본 `video.mkv`, 개별 RF64 `.wav`, 여러 AAC 트랙이 담긴 `recording.mkv` 또는 `.mp4`, 로그와 `session.json`이 저장됩니다. RF64는 일반 WAV의 4 GiB 크기 한계를 피합니다.

오디오가 2개 이상이면 일반 플레이어용 전체 믹스 트랙을 기본 트랙으로 추가하고, 독립 트랙도 함께 보관합니다. 멀티트랙을 지원하는 편집기에서 개별 트랙을 조절할 수 있습니다.

녹화용 믹서는 녹음되는 PCM에 적용되며 운영체제 음량 설정을 바꾸지 않습니다. 뮤트한 구간은 해당 트랙에 무음으로 기록되므로 나중에 원래 소리를 복원할 수 없습니다. 중복 녹음을 막기 위해 시스템 전체 소리와 앱별 소리는 동시에 선택하지 않으며, 마이크는 어느 쪽과도 함께 선택할 수 있습니다.

## 개발 환경

- CMake 3.24 이상, C++20 컴파일러, Qt 6.8.3
- Windows: Windows 11 권장. 앱별 오디오는 Windows 빌드 20348 이상 필요. Visual Studio 2022 C++ 데스크톱 도구 및 Windows SDK
- macOS: macOS 13 이상, Xcode command-line tools. 마이크는 AVFoundation 경로 사용
- 녹화·편집용 FFmpeg (`libx264`와 AAC 필요)

### Windows

```powershell
./tools/setup-windows.ps1
./tools/build-windows.ps1 -Deploy
./dist/Luma-Windows-x64/bin/Luma.exe
```

Qt와 빌드 의존성은 프로젝트 `.deps` / `.venv`에 설치합니다. FFmpeg는 프로젝트 `tools/ffmpeg/bin`에 설치하며 시스템 PATH나 권한 설정은 변경하지 않습니다. `QT_ROOT_DIR`로 기존 Qt 설치를 지정할 수도 있습니다.

개발 실행 시 Qt DLL 경로가 필요합니다.

```powershell
$env:PATH="$PWD/.deps/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
./build/Luma.exe
```

### macOS

Qt 6.8.3의 `qtmultimedia` 모듈을 설치하고 Qt 경로를 `CMAKE_PREFIX_PATH`에 지정합니다. FFmpeg를 `tools/ffmpeg/bin/ffmpeg`에 배치하거나 실행 파일 옆에 둡니다.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/macos
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix dist/Luma-macOS
```

macOS 빌드/기본 테스트/배포는 CI에서 통과했습니다. 물리 Mac에서 권한 허용, 실제 녹화, 마이크·앱별 소리, 편집을 검증하기 전에는 macOS 녹화 기능의 동작을 보장하지 않습니다. 개발 빌드는 서명·공증되지 않았습니다.

## 구조와 테스트

공용 코어와 플랫폼 경계, 타임스탬프·복구·믹서 설계는 [ARCHITECTURE.md](docs/ARCHITECTURE.md)를 참고하세요.

```powershell
# 실제 Windows API로 앱 자신의 창 + 두 테스트 프로세스 오디오를 녹화합니다.
# 약한 테스트 톤 두 개가 잠깐 재생됩니다. 다른 앱의 화면이나 마이크를 녹음하지 않습니다.
./build/Luma.exe --capture-test
```

테스트 산출물은 `artifacts/`에만 저장되며 Git에 포함되지 않습니다. 자동 UI 테스트는 네이티브 캡처/청감/성능 검증을 대신하지 않습니다.

## 알려진 한계

현재 CPU 메모리를 거쳐 인코딩하므로 높은 해상도·FPS를 설정할 수 있어도 실제 처리 성능은 하드웨어에 따라 달라집니다. GPU 인코더는 호환 드라이버와 하드웨어가 필요합니다. HDR, 게임 후킹, 일시정지, 예약 종료, 웹캠/PIP, 주석, 노이즈 억제, 자동 업데이트는 아직 없습니다. 브라우저 탭별 소리나 한 프로세스 안의 통화 참여자별 소리 분리는 지원하지 않습니다.

정식 파일 공개는 기능 점검 → 보강 → 실제 테스트/디버깅 → UI 폴리싱 후 진행합니다. 의존성 고지와 배포 정책은 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)를 확인하세요.
