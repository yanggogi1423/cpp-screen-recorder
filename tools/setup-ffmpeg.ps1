param([string]$Destination=(Join-Path $PSScriptRoot 'ffmpeg'))
$ErrorActionPreference='Stop'
$archive=Join-Path $PSScriptRoot 'ffmpeg-release-essentials.zip'
$checksum="$archive.sha256"
if(-not(Test-Path -LiteralPath $archive)){
    Invoke-WebRequest 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' -OutFile $archive -TimeoutSec 600
    Invoke-WebRequest 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip.sha256' -OutFile $checksum -TimeoutSec 60
}
if(-not(Test-Path -LiteralPath $checksum)){throw 'Missing FFmpeg checksum file.'}
$expected=(Get-Content -LiteralPath $checksum -Raw).Trim().Split(' ')[0]
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected){throw 'FFmpeg SHA256 mismatch.'}
$unpacked=Join-Path $PSScriptRoot 'ffmpeg-unpacked'
Expand-Archive -LiteralPath $archive -DestinationPath $unpacked -Force
$bundle=Get-ChildItem -LiteralPath $unpacked -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $bundle.FullName 'bin') -Destination $Destination -Recurse -Force
Copy-Item -LiteralPath (Join-Path $bundle.FullName 'LICENSE') -Destination (Join-Path $Destination 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $bundle.FullName 'README.txt') -Destination $Destination -Force
& (Join-Path $Destination 'bin/ffmpeg.exe') -version | Select-Object -First 1
