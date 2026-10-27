$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Push-Location $root
try{
    New-Item -ItemType Directory -Path artifacts -Force | Out-Null
    $env:PATH="$root/.deps/Qt/6.8.3/msvc2022_64/bin;$env:PATH"
    $test=Start-Process -FilePath "$root/build/Luma.exe" -ArgumentList '--capture-test' -WorkingDirectory $root -WindowStyle Hidden -PassThru -RedirectStandardError "$root/artifacts/capture-stderr.log" -RedirectStandardOutput "$root/artifacts/capture-stdout.log"
    if(-not $test.WaitForExit(45000)){$test.Kill();throw 'Capture test timed out.'}
    if($test.ExitCode -ne 0){throw "Capture test failed with exit $($test.ExitCode)."}
    python tests/verify-recording.py
    if($LASTEXITCODE -ne 0){throw 'Audio/video verification failed.'}
}finally{Pop-Location}
