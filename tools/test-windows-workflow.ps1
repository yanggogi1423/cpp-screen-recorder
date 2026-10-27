param([switch]$StopPaused)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try{
    New-Item -ItemType Directory -Path artifacts -Force | Out-Null
    $argsList=@('--workflow-test')
    if($StopPaused){$argsList+='--stop-paused'}
    $test=Start-Process -FilePath "$root/dist/Luma-Windows-x64/bin/Luma.exe" -ArgumentList $argsList -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardError "$root/artifacts/workflow-stderr.log" -RedirectStandardOutput "$root/artifacts/workflow-stdout.log"
    if(-not $test.WaitForExit(50000)){$test.Kill();throw 'Workflow test timed out.'}
    if($test.ExitCode -ne 0){throw "Workflow failed with exit $($test.ExitCode)."}
    if(-not $StopPaused){
        python tests/verify-recording.py artifacts/workflow-result.json
        if($LASTEXITCODE -ne 0){throw 'Workflow media verification failed.'}
    }else{
        $result=Get-Content artifacts/workflow-result.json -Raw | ConvertFrom-Json
        $manifest=Get-Content (Join-Path (Split-Path $result.file) 'session.json') -Raw | ConvertFrom-Json
        if(-not $result.pauseObserved -or $manifest.durationSeconds -lt 1.7 -or $manifest.durationSeconds -gt 2.4){throw 'Paused-stop duration is incorrect.'}
        if($manifest.pausedSeconds -lt 0.5 -or $manifest.pausedSeconds -gt 1.2){throw 'Paused-stop metadata is incorrect.'}
        $result | ConvertTo-Json | Set-Content artifacts/paused-stop-result.json -Encoding utf8
        "PASS: stopped while paused; video=$($manifest.durationSeconds)s, pause=$($manifest.pausedSeconds)s"
    }
}finally{Pop-Location}
