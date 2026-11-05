Unicode true
ManifestDPIAware true
RequestExecutionLevel user
SetCompressor lzma
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!ifdef TEST_PACKAGE
  !define APP_ID "FARecorder-Installer-Test"
  !define APP_LABEL "FARecorder (Installer Test)"
!else
  !define APP_ID "FARecorder"
  !define APP_LABEL "FARecorder"
!endif
!define REG_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_ID}"
Name "${APP_LABEL}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\FARecorder"
InstallDirRegKey HKCU "${REG_KEY}" "InstallLocation"
BrandingText "FARecorder ${VERSION}"
Icon "${APP_ICON}"
UninstallIcon "${APP_ICON}"
VIProductVersion "${NUMERIC_VERSION}"
VIAddVersionKey "ProductName" "FARecorder"
VIAddVersionKey "FileDescription" "FARecorder Setup"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright yanggogi1423"
!define MUI_ABORTWARNING
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_BGCOLOR "191919"
!define MUI_TEXTCOLOR "ECECEC"
!define MUI_INSTFILESPAGE_COLORS "ECECEC 202020"
!define MUI_WELCOMEPAGE_TITLE "FARecorder 설치"
!define MUI_WELCOMEPAGE_TEXT "화면과 소리를 녹화하는 FARecorder를 설치합니다.$\r$\n$\r$\n현재 Windows 계정에 설치하고 시작 메뉴에 추가합니다."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\FARecorder.exe"
!define MUI_FINISHPAGE_RUN_TEXT "FARecorder 실행"
!insertmacro MUI_PAGE_FINISH
!define MUI_UNCONFIRMPAGE_TEXT_TOP "FARecorder 프로그램과 바로가기를 제거합니다.$\r$\n$\r$\n녹화 영상, 복구 원본, 설정과 프로그램 폴더에 추가한 파일은 보존합니다."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"
Var MaintenanceMutex

!macro INIT
  SetShellVarContext current
  SetRegView 64
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Windows x64가 필요합니다." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  System::Call 'kernel32::CreateMutexW(p 0, i 1, w "Local\${APP_ID}-Maintenance") p.r0 ?e'
  Pop $1
  StrCpy $MaintenanceMutex $0
  ${If} $1 == 183
    MessageBox MB_OK|MB_ICONSTOP "다른 설치 또는 제거 작업을 마친 뒤 다시 시도해 주세요." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
!macroend
Function .onInit
  !insertmacro INIT
FunctionEnd
Function un.onInit
  !insertmacro INIT
FunctionEnd

Section "FARecorder" Main
  SectionIn RO
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  SetCompress off
  File /oname=payload.zip "${PAYLOAD_FILE}"
  SetCompress auto
  File /oname=FARecorderUpdater.exe "${HELPER_FILE}"
  DetailPrint "파일 검증 및 설치 중…"
  ExecWait '"$PLUGINSDIR\FARecorderUpdater.exe" --setup "$PLUGINSDIR\payload.zip" "$INSTDIR" "${PAYLOAD_SHA256}" "${VERSION}" --quiet' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "설치하지 못했습니다. 실행 중인 FARecorder를 종료하고 설치 폴더를 확인해 주세요. 기존 파일은 보존합니다." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${REG_KEY}" "DisplayName" "${APP_LABEL}"
  WriteRegStr HKCU "${REG_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${REG_KEY}" "Publisher" "yanggogi1423"
  WriteRegStr HKCU "${REG_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${REG_KEY}" "DisplayIcon" "$INSTDIR\bin\FARecorder.exe,0"
  WriteRegStr HKCU "${REG_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${REG_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKCU "${REG_KEY}" "URLInfoAbout" "https://github.com/yanggogi1423/cpp-screen-recorder"
  WriteRegDWORD HKCU "${REG_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REG_KEY}" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\${APP_LABEL}"
  CreateShortcut "$SMPROGRAMS\${APP_LABEL}\${APP_LABEL}.lnk" "$INSTDIR\bin\FARecorder.exe"
  CreateShortcut "$SMPROGRAMS\${APP_LABEL}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  ${GetParameters} $1
  ${GetOptions} $1 "/DESKTOP" $2
  ${IfNot} ${Errors}
    CreateShortcut "$DESKTOP\${APP_LABEL}.lnk" "$INSTDIR\bin\FARecorder.exe"
  ${EndIf}
SectionEnd

Section "Uninstall"
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=FARecorderUpdater.exe "${HELPER_FILE}"
  ExecWait '"$PLUGINSDIR\FARecorderUpdater.exe" --uninstall "$INSTDIR" --quiet' $0
  ${If} $0 != 0
    MessageBox MB_OK|MB_ICONSTOP "제거하지 못했습니다. FARecorder를 종료한 뒤 다시 시도해 주세요. 사용자 파일은 보존합니다." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  ReadRegStr $1 HKCU "${REG_KEY}" "InstallLocation"
  ${If} $1 == $INSTDIR
    DeleteRegKey HKCU "${REG_KEY}"
    Delete "$SMPROGRAMS\${APP_LABEL}\${APP_LABEL}.lnk"
    Delete "$SMPROGRAMS\${APP_LABEL}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${APP_LABEL}"
    Delete "$DESKTOP\${APP_LABEL}.lnk"
  ${EndIf}
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
