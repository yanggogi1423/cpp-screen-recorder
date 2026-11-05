param([Parameter(Mandatory)][string]$Installer)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$setup=[IO.Path]::GetFullPath($Installer)
if(-not $setup.EndsWith('-Setup-test.exe',[StringComparison]::Ordinal)){throw 'Use an isolated test-namespace installer only'}
$registry='HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/FARecorder-Installer-Test'
if(Test-Path -LiteralPath $registry){throw 'A previous installer test registration exists; inspect it first'}
$run=Join-Path $project ('artifacts/installer-tests/'+[guid]::NewGuid().ToString('N'))
$target=Join-Path $run 'FARecorder 설치 & test'
$version=[regex]::Match([IO.File]::ReadAllText("$project/app/AppInfo.h"),'version = "([^"]+)"').Groups[1].Value
$profile=Join-Path $run 'synthetic-profile'
[IO.Directory]::CreateDirectory($profile)|Out-Null
[IO.File]::WriteAllText("$profile/settings.txt",'unchanged user settings',[Text.UTF8Encoding]::new($false))
function Install-Fixture {
    $p=Start-Process -FilePath $setup -ArgumentList @('/S',('/D='+$target)) -WindowStyle Hidden -PassThru
    if(-not $p.WaitForExit(120000) -or $p.ExitCode -ne 0){throw 'Fixture installation failed'}
    $entry=Get-ItemProperty -LiteralPath $registry
    if($entry.DisplayVersion -cne $version -or $entry.InstallLocation -ine $target){throw 'Registration mismatch'}
    if(-not(Test-Path -LiteralPath "$target/bin/FARecorder.exe") -or -not(Test-Path -LiteralPath "$target/Uninstall.exe")){throw 'Program or uninstaller missing'}
    $shortcut=Join-Path ([Environment]::GetFolderPath('StartMenu')) 'Programs/FARecorder (Installer Test)/FARecorder (Installer Test).lnk'
    if(-not(Test-Path -LiteralPath $shortcut)){throw 'Start menu shortcut missing'}
}
Install-Fixture
$user=Join-Path $target 'My recordings/keep.mp4'
[IO.Directory]::CreateDirectory((Split-Path $user))|Out-Null
[IO.File]::WriteAllText($user,'preserve user video',[Text.UTF8Encoding]::new($false))
Install-Fixture
if([IO.File]::ReadAllText($user) -cne 'preserve user video'){throw 'Repair changed user data'}
$p=Start-Process -FilePath "$target/bin/FARecorder.exe" -ArgumentList '--ui-smoke','--small' -WindowStyle Hidden -PassThru -RedirectStandardError "$run/ui.log"
if(-not $p.WaitForExit(20000) -or $p.ExitCode -ne 0){throw 'Installed UI smoke failed'}
function Uninstall-Fixture {
    $p=Start-Process -FilePath "$target/Uninstall.exe" -ArgumentList '/S' -WindowStyle Hidden -PassThru
    if(-not $p.WaitForExit(45000)){throw 'Uninstaller timed out'}
    $deadline=[DateTime]::UtcNow.AddSeconds(45)
    while((Test-Path -LiteralPath $registry) -or (Test-Path -LiteralPath "$target/bin/FARecorder.exe")){
        if([DateTime]::UtcNow -ge $deadline){throw 'Program or registration remained after uninstall'}
        Start-Sleep -Milliseconds 100
    }
    if([IO.File]::ReadAllText($user) -cne 'preserve user video' -or [IO.File]::ReadAllText("$profile/settings.txt") -cne 'unchanged user settings'){throw 'Uninstall changed user data'}
}
Uninstall-Fixture
Install-Fixture
if([IO.File]::ReadAllText($user) -cne 'preserve user video'){throw 'Reinstall changed user data'}
Uninstall-Fixture
$proof=@{result='PASS';version=$version;firstInstall=$true;repair=$true;nativeUi=$true;uninstall=$true;reinstall=$true;userFilesPreserved=$true;profilePreserved=$true;registrationAndShortcut=$true;root=$target}
[IO.File]::WriteAllText("$run/result.json",($proof|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$proof|ConvertTo-Json
