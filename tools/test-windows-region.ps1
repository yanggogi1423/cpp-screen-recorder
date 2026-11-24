param([switch]$NativeGpu, [string]$Executable)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Path artifacts -Force | Out-Null
    $regionArgs=@('--region-test')
    if($NativeGpu){$regionArgs+='--native-gpu'}
    if(-not $Executable){
        $Executable="$root/dist/FARecorder-Windows-x64/bin/FARecorder.exe"
        if(-not(Test-Path -LiteralPath $Executable)){$Executable=Join-Path $env:LOCALAPPDATA 'Programs/FARecorder/bin/FARecorder.exe'}
    }
    $proc=Start-Process -FilePath $Executable -ArgumentList $regionArgs -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardError "$root/artifacts/region-stderr.log"
    if(-not $proc.WaitForExit(30000)){$proc.Kill();throw 'Silent region test timed out.'}
    if($proc.ExitCode -ne 0){throw "Silent region test failed: $($proc.ExitCode)"}
    python tests/verify-region.py
    if($LASTEXITCODE -ne 0){throw 'Region media verification failed.'}
} finally { Pop-Location }
