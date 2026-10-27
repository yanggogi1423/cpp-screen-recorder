$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    python -m venv .venv
    if($LASTEXITCODE -ne 0){throw 'Python virtual environment setup failed.'}
    & .venv/Scripts/python.exe -m pip install aqtinstall==3.3.0
    if($LASTEXITCODE -ne 0){throw 'aqt install failed.'}
    & .venv/Scripts/python.exe -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .deps/Qt --modules qtmultimedia --archives qtbase qtdeclarative qttools qtsvg qtmultimedia
    if($LASTEXITCODE -ne 0){throw 'Qt install failed.'}
    # A project-local C++/WinRT projection also supports machines with SDK 19041.
    New-Item -ItemType Directory -Path .deps -Force | Out-Null
    Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.cppwinrt/2.0.240405.15/microsoft.windows.cppwinrt.2.0.240405.15.nupkg' -OutFile '.deps/cppwinrt.zip' -TimeoutSec 60
    Expand-Archive -LiteralPath '.deps/cppwinrt.zip' -DestinationPath '.deps/cppwinrt' -Force
    $metadata=Get-ChildItem "${env:ProgramFiles(x86)}/Windows Kits/10/UnionMetadata" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    & .deps/cppwinrt/bin/cppwinrt.exe -input (Join-Path $metadata.FullName 'Windows.winmd') -output '.deps/cppwinrt/include'
    if($LASTEXITCODE -ne 0){throw 'C++/WinRT projection generation failed.'}
    & tools/setup-ffmpeg.ps1
} finally { Pop-Location }
