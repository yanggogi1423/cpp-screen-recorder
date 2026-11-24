param([switch]$IncludeSdk, [switch]$Apply)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent)).TrimEnd('\','/')
$prefix=$root+[IO.Path]::DirectorySeparatorChar
$media=@('.mp4','.mkv','.wav','.mov','.webm','.flac','.mp3','.aac','.m4a','.m4v','.avi','.ogg','.opus','.wma','.wmv','.flv','.ts','.m2ts')
$payload=@('.exe','.dll','.pdb','.lib','.obj','.zip','.nupkg','.msi')
$tracked=@(& git -C $root -c core.quotepath=false ls-files)
if($LASTEXITCODE -ne 0){throw 'Cannot read source ownership from Git'}
function Assert-WorkspacePath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if(-not $full.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Path outside workspace: $full"}
    for($cursor=$full; $cursor -and $cursor -ine $root; $cursor=Split-Path $cursor -Parent){
        try {$attributes=[IO.File]::GetAttributes($cursor)}
        catch [IO.FileNotFoundException] {continue}
        catch [IO.DirectoryNotFoundException] {continue}
        if($attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked cleanup path refused: $cursor"}
    }
    $relative=$full.Substring($prefix.Length).Replace('\','/')
    if($relative -match '(^|/)(\.git|\.agents|\.codex)(/|$)'){throw 'Protected metadata path refused'}
    foreach($name in $tracked){if($name -ieq $relative -or $name.StartsWith($relative+'/',[StringComparison]::OrdinalIgnoreCase)){throw "Tracked source cannot be cleaned: $relative"}}
    return $full
}
function Get-SafeFiles([string]$Directory) {
    if(-not(Test-Path -LiteralPath $Directory)){return}
    $pending=[Collections.Generic.Stack[string]]::new()
    $pending.Push($Directory)
    while($pending.Count){
        foreach($item in (Get-ChildItem -LiteralPath $pending.Pop() -Force)){
            if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked file in cleanup tree refused: $($item.FullName)"}
            if($item.PSIsContainer){$pending.Push($item.FullName)}else{$item}
        }
    }
}
function Zip-ContainsMedia([string]$Path) {
    if([IO.Path]::GetExtension($Path) -ine '.zip'){return $false}
    $archive=$null
    try {
        $archive=[IO.Compression.ZipFile]::OpenRead($Path)
        foreach($entry in $archive.Entries){if($media -contains [IO.Path]::GetExtension($entry.FullName).ToLowerInvariant()){return $true}}
        return $false
    } catch { return $true } finally {if($archive){$archive.Dispose()}}
}
$directoryPlans=[Collections.Generic.List[object]]::new()
$filePlans=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$preserved=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
function Protect-File($File) {
    $preserved[$File.FullName]=@{path=$File.FullName;bytes=$File.Length;sha256=(Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash}
}
function Plan-File($File) {
    $null=Assert-WorkspacePath $File.FullName
    if(($media -contains $File.Extension.ToLowerInvariant()) -or (Zip-ContainsMedia $File.FullName)){Protect-File $File;return}
    $filePlans[$File.FullName]=@{path=$File.FullName;bytes=$File.Length}
}
$generated=@('build','.packages','.venv','tools/ffmpeg','tools/ffmpeg-unpacked')
if($IncludeSdk){$generated+='.deps'}
foreach($relative in $generated){
    $path=Join-Path $root $relative
    if(-not(Test-Path -LiteralPath $path)){continue}
    $null=Assert-WorkspacePath $path
    $files=@(Get-SafeFiles $path)
    $recordings=@($files | Where-Object { ($media -contains $_.Extension.ToLowerInvariant()) -or ($_.Extension -ieq '.zip' -and (Zip-ContainsMedia $_.FullName)) })
    if($recordings.Count){
        foreach($file in $files){if(($media -contains $file.Extension.ToLowerInvariant()) -or ($file.Extension -ieq '.zip' -and (Zip-ContainsMedia $file.FullName))){Protect-File $file}elseif($payload -contains $file.Extension.ToLowerInvariant()){Plan-File $file}}
    } else {
        $directoryPlans.Add(@{path=$path;bytes=($files|Measure-Object Length -Sum).Sum})
    }
}
foreach($file in (Get-SafeFiles (Join-Path $root 'artifacts'))){
    if($media -contains $file.Extension.ToLowerInvariant()){Protect-File $file}
    elseif($payload -contains $file.Extension.ToLowerInvariant()){Plan-File $file}
}
foreach($name in @('ffmpeg-release-essentials.zip')){
    $path=Join-Path $root "tools/$name"
    if(Test-Path -LiteralPath $path){Plan-File (Get-Item -LiteralPath $path)}
}
$version=[regex]::Match([IO.File]::ReadAllText("$root/app/AppInfo.h"),'version = "([^"]+)"').Groups[1].Value
$release=Join-Path $root "dist/releases/$version"
$setup=Join-Path $release "FARecorder-$version-windows-x64-Setup.exe"
if(-not(Test-Path -LiteralPath $setup)){throw 'Keep a current installer locally before cleaning development tools'}
$expected=([IO.File]::ReadAllText("$setup.sha256").Trim() -split '\s+')[0]
if((Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash -ine $expected){throw 'Current installer SHA256 mismatch'}
Protect-File (Get-Item -LiteralPath $setup)
foreach($file in (Get-SafeFiles (Join-Path $root 'dist/releases'))){
    if($file.Name -like 'FARecorder-*-windows-*.zip'){
        Plan-File $file
        if(Test-Path -LiteralPath ($file.FullName+'.sha256')){Plan-File (Get-Item -LiteralPath ($file.FullName+'.sha256'))}
    }
}
$receipt=Join-Path $root ('artifacts/cleanup/'+[DateTime]::Now.ToString('yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($receipt)|Out-Null
$bytes=($directoryPlans|Measure-Object bytes -Sum).Sum+($filePlans.Values|Measure-Object bytes -Sum).Sum
$plan=@{mode=$(if($Apply){'apply'}else{'preview'});workspace=$root;bytes=$bytes;includeSdk=[bool]$IncludeSdk;directories=@($directoryPlans);files=@($filePlans.Values);preserved=@($preserved.Values)}
[IO.File]::WriteAllText("$receipt/plan.json",($plan|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
if($Apply){
    foreach($entry in $directoryPlans){
        $null=Assert-WorkspacePath $entry.path
        # Inspect for links again immediately before recursive removal.
        $null=@(Get-SafeFiles $entry.path)
        Remove-Item -LiteralPath $entry.path -Recurse -Force
    }
    foreach($entry in $filePlans.Values){
        $null=Assert-WorkspacePath $entry.path
        if((Get-Item -LiteralPath $entry.path -Force).Length -ne $entry.bytes){throw 'Cleanup candidate changed; stopping'}
        Remove-Item -LiteralPath $entry.path -Force
    }
    $artifactRoot=Join-Path $root 'artifacts'
    foreach($directory in (Get-ChildItem -LiteralPath $artifactRoot -Directory -Recurse -Force | Sort-Object { $_.FullName.Length } -Descending)){
        $null=Assert-WorkspacePath $directory.FullName
        if([IO.Directory]::GetFileSystemEntries($directory.FullName).Length -eq 0){
            Remove-Item -LiteralPath $directory.FullName
        }
    }
    foreach($entry in $preserved.Values){
        if(-not(Test-Path -LiteralPath $entry.path) -or (Get-FileHash -LiteralPath $entry.path -Algorithm SHA256).Hash -cne $entry.sha256){throw 'Preserved file verification failed'}
    }
}
@{mode=$plan.mode;plannedGiB=[Math]::Round($bytes/1GB,3);directories=$directoryPlans.Count;files=$filePlans.Count;preservedMediaAndInstaller=$preserved.Count;receipt=$receipt}|ConvertTo-Json
