param([string]$InputFile, [int]$ExpectedAudioTracks = 3)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $inputPath = if($InputFile){ (Resolve-Path -LiteralPath $InputFile).Path }else{ (Get-Content artifacts/workflow-normal-result.json -Raw | ConvertFrom-Json).file }
    $originalHash = (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash
    function Invoke-Edit([string]$path, [switch]$Overwrite) {
        $arguments = @('--edit-test', ('"{0}"' -f $path))
        if ($Overwrite) { $arguments += '--overwrite' }
        $process = Start-Process -FilePath "$root/dist/FARecorder-Windows-x64/bin/FARecorder.exe" -ArgumentList $arguments -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardError "$root/artifacts/editor-stderr.log"
        if (-not $process.WaitForExit(25000)) { $process.Kill(); throw 'Editor fixture timed out.' }
        if ($process.ExitCode -ne 0) { throw "Editor fixture failed: $($process.ExitCode)" }
        return (Get-Content artifacts/edit-result.json -Raw | ConvertFrom-Json).file
    }
    function Assert-Trim([string]$path) {
        $probe = & "$root/tools/ffmpeg/bin/ffprobe.exe" -v error -show_streams -show_format -of json $path | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0) { throw 'Trimmed video could not be probed.' }
        $audio = @($probe.streams | Where-Object codec_type -eq 'audio')
        $duration = [double]$probe.format.duration
        if ($audio.Count -ne $ExpectedAudioTracks -or $duration -lt 1.95 -or $duration -gt 2.15) { throw 'Trim duration or independent audio tracks were not preserved.' }
        return $duration
    }
    $newFile = Invoke-Edit $inputPath
    $newDuration = Assert-Trim $newFile
    if ($newFile -eq $inputPath -or (Get-FileHash -LiteralPath $inputPath -Algorithm SHA256).Hash -ne $originalHash) { throw 'Save-new changed the original.' }
    $copy = Join-Path $root 'artifacts/overwrite-fixture.mp4'
    Copy-Item -LiteralPath $inputPath -Destination $copy -Force
    $overwritten = Invoke-Edit $copy -Overwrite
    $overwriteDuration = Assert-Trim $overwritten
    if ($overwritten -ne $copy.Replace('\','/') -and $overwritten -ne $copy) { throw 'Overwrite returned another file.' }
    if ((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -eq $originalHash) { throw 'Overwrite did not replace the fixture.' }
    [pscustomobject]@{Passed=$true;NewFile=$newFile;NewDuration=$newDuration;OriginalPreserved=$true;OverwriteDuration=$overwriteDuration;AudioTracks=$ExpectedAudioTracks} | ConvertTo-Json | Set-Content artifacts/editor-verification.json -Encoding utf8
    "PASS: MP4 trim, $ExpectedAudioTracks audio tracks, original preservation and confirmed overwrite."
} finally { Pop-Location }
