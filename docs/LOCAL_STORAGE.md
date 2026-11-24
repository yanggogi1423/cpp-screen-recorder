# 로컬 저장 공간 관리

2026-11-25. 프로그램 사용에 필요한 파일은 설치 위치인 `%LOCALAPPDATA%/Programs/FARecorder`에 있다. 프로젝트의 SDK·빌드·다운로드·설치 시험 복사본은 앱 실행에 필요하지 않다.

`tools/clean-workspace.ps1`은 기본적으로 삭제 계획만 표시한다. `-Apply`를 주면 프로젝트 안의 재생성 가능한 빌드/구형 패키지/시험 바이너리와 다운로드 캐시를 정리한다. `-IncludeSdk`는 로컬 Qt·C++/WinRT·NSIS 등의 개발 의존성도 정리한다.

```powershell
./tools/clean-workspace.ps1 -IncludeSdk
./tools/clean-workspace.ps1 -IncludeSdk -Apply
```

Git이 추적하는 소스, `.git` 등 관리 디렉터리와 연결된 경로는 삭제할 수 없다. 영상·음성 파일 및 미디어가 든 ZIP은 보존한다. 현재 버전의 설치 EXE와 SHA256 파일을 남기며, 정리 전후에 보존 파일의 해시를 확인한다. 계획과 보존 파일 해시는 `artifacts/cleanup/`에 기록한다. 이 스크립트는 설치 앱·사용자 설정·동영상 폴더·Recovery를 건드리지 않는다.

개발을 다시 시작할 때만 필요한 도구를 복원한다.

```powershell
./tools/setup-windows.ps1
./tools/build-windows.ps1 -Deploy
```

설치 파일을 만들 때 NSIS는 `tools/package-windows.ps1`이 고정 크기/SHA256을 검증해 다시 준비한다. 기존 `.packages`는 과거 C# 실험의 NuGet 캐시이며 현재 C++ CMake 빌드에는 쓰지 않는다. 글꼴 원본과 라이선스는 Git에 포함된 `app/assets/fonts`에 유지한다.

예전 설치 백업과 설치본 내부의 별도 CRT 설치 EXE는 이번에 현재 앱·앱 로컬 CRT·최신 설치 파일과 사용자 파일 보존을 확인한 뒤 별도로 정리했다. DLL이나 코덱은 삭제하지 않았다. GitHub의 공개 릴리즈 파일은 유지한다.
