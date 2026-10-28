# 자동 업데이트와 배포

2026-10-29. 0.5.0-beta.1부터 제공한다. 이전 0.4 버전에는 업데이트 기능이 없으므로 이 버전은 한 번 수동으로 압축 해제해 실행한다.

## 사용자 흐름

- 시작 15초 뒤 자동 확인하며 성공 결과는 24시간 재사용한다. 도움말 → 업데이트에서 직접 확인하거나 시작 시 확인을 끌 수 있다.
- 베타 사용자는 기본적으로 베타 포함 채널이다. 안정 버전 채널을 고르면 prerelease와 alpha/beta/rc 태그를 제외한다.
- 다운로드는 사용자가 선택한다. 완료 후 SHA256을 확인하고 **설치하고 다시 시작**을 누르면 준비한다. 녹화·소리 테스트·미저장 영상이 있으면 설치하지 않는다.
- 준비는 앱이 열린 상태에서 진행한다. ZIP 경로·링크·패키지 ID·버전을 검사한 뒤에만 앱 종료를 요청한다. 준비 중에는 새 녹화를 막고 설치 취소를 제공한다.
- 도우미가 기존 앱 종료를 확인하고 교체한다. 새 앱의 초기 실행이 실패하면 이전 앱을 복원·재실행한다. 기존 버전 전체를 옆의 `.backup-…` 디렉터리에 보존하고, 프로그램 폴더에 추가된 사용자 파일도 새 디렉터리에 보존한다. 충돌이 있으면 원본을 유지한다.
- 설정은 기존 QSettings에, 복구 녹화는 기존 앱 데이터 Recovery에 남는다. 백업·취소된 준비 디렉터리를 자동 재귀 삭제하지 않는다. 전원 중단 뒤에는 보존된 백업으로 수동 복구할 수 있다.

## 비용과 호출 정책

현재 공개 저장소의 GitHub Releases를 사용하므로 별도 업데이트 서버·DB·스토리지 요금이 필요하지 않다. [GitHub 안내](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases)에 따르면 첨부 파일 하나는 2 GiB 미만이며 릴리즈 전체 크기·다운로드 대역폭에는 제한이 없다. 이 앱은 검증할 ZIP을 512 MiB 이하로 제한한다.

[비인증 API](https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api)는 IP당 시간당 60회다. 계정 토큰을 앱에 넣지 않는다. 일일 확인·ETag/304 캐시를 사용하고 403/429 뒤에는 최소 1시간 재요청을 막는다. 공유 IP에서 한도에 도달하면 안내하고 릴리즈 페이지를 열 수 있다. GitHub 서비스 정책·가용성은 외부 조건이며 영구 무료 계약을 보장하는 것은 아니다.

이 기능은 OS 보안 설정을 바꾸거나 관리자 권한을 자동 요청하지 않는다. Windows 베타는 기존처럼 서명되지 않았다. 코드 서명과 [Apple Developer ID/공증](https://developer.apple.com/help/account/basics/about-your-developer-account)은 배포 신뢰를 위한 별도 절차다. SHA256은 서명·공증을 대체하지 않으며 GitHub 계정·HTTPS·공식 릴리즈 자산을 신뢰하는 구조다.

## 배포 계약

공개된 정식 릴리즈와 prerelease를 의미 버전으로 비교한다. 초안·이전 버전·잘못된 태그·다른 OS/아키텍처·해시가 없는 자산은 설치 대상으로 선택하지 않는다.

| 대상 | ZIP 이름 | ZIP의 최상위 디렉터리 |
|---|---|---|
| Windows x64 | FARecorder-VERSION-windows-x64.zip | FARecorder-Windows-x64 |
| macOS Apple Silicon | FARecorder-VERSION-macos-arm64.zip | FARecorder.app |
| macOS Intel | FARecorder-VERSION-macos-x64.zip | FARecorder.app |

릴리즈 태그는 `vVERSION`이다. 자산의 `digest: sha256:…`와 실제 길이를 확인한다. 다운로드는 고정된 저장소의 HTTPS 자산 URL만 선택하고 설치 도우미에서도 해시를 다시 검사한다. 패키지에는 `farecorder-package.txt`와 `update-files.txt`가 필요하다. 패키지 파일명은 ASCII, 사용자 데이터 경로는 Unicode를 지원한다. Windows ZIP의 심볼릭 링크와 절대·상위·ADS 경로를 거부한다. macOS의 번들 내부 상대 링크는 패키지 안으로 제한한다.

도우미는 Qt를 사용하지 않는 독립 실행 파일이다. Windows는 정적 C++ 런타임으로 빌드해 교체할 Qt DLL을 잠그지 않는다. Windows의 시스템 tar, macOS의 시스템 tar를 인자 배열로 실행하며 셸 명령에 사용자 경로를 삽입하지 않는다. 읽기 전용 설치 위치 등 준비 실패 시 앱을 유지하고 수동 업데이트를 안내한다.

## 릴리즈 생성 시

1. Windows는 `tools/build-windows.ps1 -Deploy`, macOS는 CI의 CMake 빌드·설치를 실행한다.
2. README·문서·서드파티 원문·소스 링크를 최종 패키지에 복사한다.
3. **모든 파일 복사 후** `bin/FARecorderUpdater.exe --write-manifest 패키지경로`를 실행한다. Mac은 `FARecorder.app/Contents/MacOS/FARecorderUpdater`를 사용한다. 빌드 중에도 생성하지만 배포 파일을 추가했다면 반드시 다시 생성한다.
4. 표의 루트 구조로 ZIP을 만들고 `.sha256`을 함께 올린다. GitHub가 보고하는 자산 digest와 로컬 해시를 대조한다.
5. 압축 해제 실행 검사와 양 OS CI 통과 후 공개한다. 현재는 물리 Mac 검증 전이므로 Windows 자산만 공개한다. Mac 코드·도우미의 CI 통과와 실제 앱 업데이트/캡처 검증은 구분한다.
