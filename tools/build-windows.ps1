param([switch]$Deploy, [switch]$Clean)
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
$configureArgs=@()
$compilerDir=Split-Path (Get-Command cl.exe).Source
if((Test-Path (Join-Path $compilerDir '1042/clui.dll')) -and -not (Test-Path (Join-Path $compilerDir '1033/clui.dll'))){
    $configureArgs+='-DFAR_MSVC_INCLUDE_PREFIX:STRING=참고: 포함 파일: '
}else{
    $configureArgs+='-UFAR_MSVC_INCLUDE_PREFIX'
}
& $cmake -S $root -B (Join-Path $root 'build') -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$qt" -DCMAKE_BUILD_TYPE=Release @configureArgs
if($LASTEXITCODE -ne 0){throw 'CMake configure failed.'}
$buildArgs=@('--build', (Join-Path $root 'build'), '--parallel', '4')
if($Clean){$buildArgs+='--clean-first'}
& $cmake @buildArgs
if($LASTEXITCODE -ne 0){throw 'C++ build failed.'}
$env:PATH="$qt/bin;$env:PATH"
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir (Join-Path $root 'build') --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Tests failed.'}
if($Deploy){
    & $cmake --install (Join-Path $root 'build') --prefix (Join-Path $root 'dist/FARecorder-Windows-x64')
    if($LASTEXITCODE -ne 0){throw 'Deployment failed.'}
    $bundle=Join-Path $root 'dist/FARecorder-Windows-x64'
    if(Test-Path -LiteralPath (Join-Path $root 'tools/ffmpeg/bin/ffmpeg.exe')){
        Copy-Item -LiteralPath (Join-Path $root 'tools/ffmpeg/bin/ffmpeg.exe') -Destination (Join-Path $bundle 'bin/ffmpeg.exe') -Force
        Copy-Item -LiteralPath (Join-Path $root 'tools/ffmpeg/LICENSE.txt') -Destination (Join-Path $bundle 'FFmpeg-LICENSE.txt') -Force
    }
    Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $bundle -Force
}
