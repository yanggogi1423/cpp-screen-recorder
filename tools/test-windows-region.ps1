$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    New-Item -ItemType Directory -Path artifacts -Force | Out-Null
    $proc=Start-Process -FilePath "$root/dist/FARecorder-Windows-x64/bin/FARecorder.exe" -ArgumentList '--region-test' -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardError "$root/artifacts/region-stderr.log"
    if(-not $proc.WaitForExit(30000)){$proc.Kill();throw 'Silent region test timed out.'}
    if($proc.ExitCode -ne 0){throw "Silent region test failed: $($proc.ExitCode)"}
    python tests/verify-region.py
    if($LASTEXITCODE -ne 0){throw 'Region media verification failed.'}
} finally { Pop-Location }
