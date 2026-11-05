param([Parameter(Mandatory)][string]$BundleRoot,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [switch]$TestPackage)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$bundle=[IO.Path]::GetFullPath($BundleRoot)
$output=[IO.Path]::GetFullPath($OutputDirectory)
$version=[regex]::Match([IO.File]::ReadAllText("$project/app/AppInfo.h"),'version = "([^"]+)"').Groups[1].Value
if(-not $version){throw 'Missing version'}
if(-not(Test-Path -LiteralPath "$bundle/bin/FARecorder.exe")){throw 'Deploy the full bundle first'}
& python "$PSScriptRoot/get-nsis.py"
if($LASTEXITCODE -ne 0){throw 'Official NSIS verification failed'}
$nsisLicense=Join-Path $bundle 'licenses/NSIS'
[IO.Directory]::CreateDirectory($nsisLicense)|Out-Null
Copy-Item -LiteralPath "$project/.deps/nsis/nsis-3.13/COPYING" -Destination "$nsisLicense/COPYING.txt" -Force
[IO.Directory]::CreateDirectory($output)|Out-Null
$archive=Join-Path $output "FARecorder-$version-windows-x64.zip"
if(Test-Path -LiteralPath $archive){throw 'Use a fresh package output directory'}
Copy-Item -LiteralPath "$project/README.md","$project/LICENSE","$project/THIRD_PARTY_NOTICES.md" -Destination $bundle -Force
Copy-Item -LiteralPath "$project/docs" -Destination $bundle -Recurse -Force
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$redist=Get-ChildItem -LiteralPath "$vs/VC/Redist/MSVC" -Directory | Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$crt=Join-Path $redist.FullName 'x64/Microsoft.VC143.CRT'
if(-not(Test-Path -LiteralPath "$crt/vcruntime140.dll")){throw 'App-local Microsoft C++ runtime missing'}
Get-ChildItem -LiteralPath $crt -Filter '*.dll' | Copy-Item -Destination "$bundle/bin" -Force
$qt=Join-Path $project '.deps/Qt/6.8.3/msvc2022_64'
if($env:QT_ROOT_DIR){$qt=[IO.Path]::GetFullPath($env:QT_ROOT_DIR)}
$licenses=Join-Path $bundle 'licenses/Qt'
[IO.Directory]::CreateDirectory($licenses)|Out-Null
Copy-Item -LiteralPath "$project/packaging/licenses/Qt/LGPL-3.0.txt","$project/packaging/licenses/Qt/GPL-3.0.txt" -Destination $licenses -Force
if(-not(Test-Path -LiteralPath "$licenses/LGPL-3.0.txt")){throw 'Qt LGPL license is required'}
$source="FARecorder $version source: https://github.com/yanggogi1423/cpp-screen-recorder/tree/v$version`nQt 6.8.3 sources: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/`nFFmpeg source: https://github.com/FFmpeg/FFmpeg/commit/946fcce07b6dcd0331c8cc609192aeff5e1924f8`nD2Coding 1.4.0: https://github.com/naver/d2codingfont/releases/tag/VER1.4.0`nNSIS 3.13: https://sourceforge.net/projects/nsis/files/NSIS%203/3.13/`n"
[IO.File]::WriteAllText("$bundle/SOURCE-LINKS.txt",$source,[Text.UTF8Encoding]::new($false))
& "$bundle/bin/FARecorderUpdater.exe" --write-manifest $bundle
if($LASTEXITCODE -ne 0){throw 'Manifest generation failed'}
$env:FAR_PACKAGE_BUNDLE=$bundle
$env:FAR_PACKAGE_ARCHIVE=$archive
@'
import os,zipfile
from pathlib import Path
b=Path(os.environ['FAR_PACKAGE_BUNDLE']);a=Path(os.environ['FAR_PACKAGE_ARCHIVE'])
files=sorted(p.relative_to(b).as_posix() for p in b.rglob('*') if p.is_file())
assert files==sorted((b/'update-files.txt').read_text(encoding='utf-8').splitlines())
with zipfile.ZipFile(a,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for name in files:z.write(b/name,'FARecorder-Windows-x64/'+name)
with zipfile.ZipFile(a) as z:assert z.testzip() is None
'@ | python -
if($LASTEXITCODE -ne 0){throw 'ZIP packaging failed'}
$digest=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archive.sha256","$digest  $([IO.Path]::GetFileName($archive))`n",[Text.UTF8Encoding]::new($false))
$compiler=Join-Path $project '.deps/nsis/nsis-3.13/makensis.exe'
$suffix=if($TestPackage){'-Setup-test.exe'}else{'-Setup.exe'}
$installer=Join-Path $output "FARecorder-$version-windows-x64$suffix"
$numeric=[regex]::Match($version,'^(\d+\.\d+\.\d+)(?:-[a-z]+\.(\d+))?$')
if(-not $numeric.Success){throw 'Invalid package version'}
$resourceVersion=$numeric.Groups[1].Value+'.'+$(if($numeric.Groups[2].Success){$numeric.Groups[2].Value}else{'0'})
$helperFile=[IO.Path]::GetFullPath((Join-Path $bundle 'bin/FARecorderUpdater.exe'))
$iconFile=[IO.Path]::GetFullPath((Join-Path $project 'app/assets/app.ico'))
$licenseFile=[IO.Path]::GetFullPath((Join-Path $project 'LICENSE'))
$arguments=@('/V2','/INPUTCHARSET','UTF8',"/DVERSION=$version","/DNUMERIC_VERSION=$resourceVersion","/DOUTPUT_FILE=$([IO.Path]::GetFullPath($installer))","/DPAYLOAD_FILE=$([IO.Path]::GetFullPath($archive))","/DPAYLOAD_SHA256=$digest","/DHELPER_FILE=$helperFile","/DAPP_ICON=$iconFile","/DLICENSE_FILE=$licenseFile")
if($TestPackage){$arguments+='/DTEST_PACKAGE'}
& $compiler @arguments "$project/installer/windows.nsi"
if($LASTEXITCODE -ne 0){throw 'Installer compilation failed'}
$hash=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$installer.sha256","$hash  $([IO.Path]::GetFileName($installer))`n",[Text.UTF8Encoding]::new($false))
Get-Item -LiteralPath $installer,$archive | Select-Object Name,Length
