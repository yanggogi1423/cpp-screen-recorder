param([switch]$Deploy)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$qt=Join-Path $root '.deps/Qt/6.8.3/msvc2022_64'
if($env:QT_ROOT_DIR){$qt=$env:QT_ROOT_DIR}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'Visual Studio C++ tools not found.'}
$devShell=Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1'
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$env:VSLANG='1033'
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
& $cmake -S $root -B (Join-Path $root 'build') -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE -ne 0){throw 'CMake configure failed.'}
& $cmake --build (Join-Path $root 'build') --parallel 4
if($LASTEXITCODE -ne 0){throw 'C++ build failed.'}
$env:PATH="$qt/bin;$env:PATH"
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir (Join-Path $root 'build') --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Tests failed.'}
if($Deploy){
    & $cmake --install (Join-Path $root 'build') --prefix (Join-Path $root 'dist/Luma-Windows-x64')
    if($LASTEXITCODE -ne 0){throw 'Deployment failed.'}
}
