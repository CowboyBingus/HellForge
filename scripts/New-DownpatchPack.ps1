# Builds a downpatch pack: the patch from the HELLDIVERS 2 build Steam has now to HellForge's supported build
# (src/HellForge.Core/supported-build.json), for setup to fetch through downpatch/index.json (Downpatcher.cs).
# A pack holds Arrowhead-derived bytes, so it never goes into this repository: upload the zip and the updated index.json
# to files.hf-mod.com/downpatch/ by hand. Released setups find a new pack there; no HellForge release is needed.
#
# -Source  HELLDIVERS 2 as Steam has it (default: Steam's install). Steam must not be updating it and the game must be closed.
# -Target  a folder with the supported build's files (default: Documents\HellForge\game); every file is checked against the list.
# -Out     where the pack, its index entry and the merged index.json go (default: artifacts/downpatch).
# -Index   the index.json now published, to merge the new pack into (default: downloaded from files.hf-mod.com).
# -Verify  applies every patch to a temporary file and checks it against the list before the pack is written.
# -Revision a pack rebuilt for the same builds gets its own file name (-r2, -r3...), so a setup still downloading the
#          one listed before is not cut off when index.json changes.
#
# Ops per file of the supported build: copy (Steam's file is already right), patch (hdiffz from Steam's file, however
# small), add (the file itself, only when Steam's build has no such file, or for a file players change, like
# data/settings.ini). A pack ships as little of the game whole as it can.
param([string]$Source,[string]$Target=(Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'HellForge/game'),
    [string]$Out,[string]$Index,[switch]$Verify,[int]$Revision=1)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$project=Split-Path -Parent $PSScriptRoot
if(-not $Out){$Out=Join-Path $project 'artifacts/downpatch'}
$Out=[IO.Path]::GetFullPath($Out)
$BaseUrl='https://files.hf-mod.com/downpatch/'
# HDiffPatch v5.1.3, as Downpatcher.cs pins it (the setup applies with its hpatchz).
$ToolsUrl='https://github.com/sisong/HDiffPatch/releases/download/v5.1.3/hdiffpatch_v5.1.3_bin_windows64.zip'
$ToolsSha256='77F141386E5D8F785C1C846E10FBBC19B6C05AA00E3F59CC44670FB3F0E2AE94'
# Files players change (or the game rewrites): shipped as they are in the build, never patched from Steam's copy. A
# played installation has its own, so the build's copy comes from files.hf-mod.com when the target's has changed.
$AlwaysAdd=@('data/settings.ini')

$build=Get-Content -Raw -LiteralPath (Join-Path $project 'src/HellForge.Core/supported-build.json')|ConvertFrom-Json

# Steam's install and the build it has, from the app manifest of the library holding it.
$steamRoot=(Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
if(-not $Source){
    if(-not $steamRoot){throw 'Steam is not installed: pass -Source.'}
    $libraries=@($steamRoot)+@([regex]::Matches([IO.File]::ReadAllText((Join-Path $steamRoot 'steamapps/libraryfolders.vdf')),'"path"\s+"([^"]+)"')|ForEach-Object {$_.Groups[1].Value.Replace('\\','\')})
    $Source=@($libraries|ForEach-Object {Join-Path $_ 'steamapps/common/Helldivers 2'}|Where-Object {Test-Path -LiteralPath (Join-Path $_ 'bin/helldivers2.exe')})[0]
    if(-not $Source){throw 'HELLDIVERS 2 was not found in a Steam library: pass -Source.'}
}
$Source=[IO.Path]::GetFullPath($Source);$Target=[IO.Path]::GetFullPath($Target)
$manifest=Join-Path (Split-Path -Parent (Split-Path -Parent $Source)) 'appmanifest_553850.acf'
if(-not (Test-Path -LiteralPath $manifest)){throw 'No appmanifest_553850.acf next to '+$Source}
$acf=[IO.File]::ReadAllText($manifest)
$sourceBuild=[regex]::Match($acf,'"buildid"\s+"(\d+)"').Groups[1].Value
$flags=[uint32][regex]::Match($acf,'"StateFlags"\s+"(\d+)"').Groups[1].Value
if(-not $sourceBuild){throw 'The app manifest names no build.'}
if(($flags -band 4) -eq 0 -or ($flags -band (32+64+128+1024+131072+1048576)) -ne 0){throw "Steam is not done with HELLDIVERS 2 (StateFlags $flags): let it finish, then run this again."}
if($sourceBuild -eq $build.buildId){throw "Steam already has the supported build $sourceBuild."}
if(Get-Process helldivers2 -ErrorAction SilentlyContinue){throw 'Close HELLDIVERS 2 first.'}
$name="downpatch-$sourceBuild-to-$($build.buildId)"+$(if($Revision -gt 1){"-r$Revision"}else{''})
Write-Output "Source: build $sourceBuild in $Source"
Write-Output "Target: build $($build.buildId) from $Target ($($build.files.Count) files)"

# The tools, pinned.
$tools=Join-Path $project 'artifacts/tools/hdiffpatch-5.1.3'
$hdiffz=Join-Path $tools 'windows64/hdiffz.exe';$hpatchz=Join-Path $tools 'windows64/hpatchz.exe'
if(-not (Test-Path -LiteralPath $hdiffz)){
    New-Item -ItemType Directory -Force $tools|Out-Null
    $zip=Join-Path $tools 'hdiffpatch.zip'
    Invoke-WebRequest -UseBasicParsing -Uri $ToolsUrl -OutFile $zip
    if((Get-FileHash -LiteralPath $zip).Hash -ne $ToolsSha256){Remove-Item -LiteralPath $zip;throw 'HDiffPatch does not match its pin.'}
    [IO.Compression.ZipFile]::ExtractToDirectory($zip,$tools)
}

$work=Join-Path $Out $name
if(Test-Path -LiteralPath $work){Remove-Item -LiteralPath $work -Recurse -Force}
New-Item -ItemType Directory -Force (Join-Path $work 'patches'),(Join-Path $work 'add')|Out-Null
function Sha1([string]$Path){(Get-FileHash -Algorithm SHA1 -LiteralPath $Path).Hash}

$clock=[Diagnostics.Stopwatch]::StartNew()
# Every target file first, before the long diffs: the build's file for each path.
$good=@{}
foreach($file in $build.files){
    $to=Join-Path $Target $file.path
    if((Test-Path -LiteralPath $to) -and (Get-Item -LiteralPath $to).Length -eq $file.size -and (Sha1 $to) -eq $file.sha1){$good[$file.path]=$to;continue}
    if($AlwaysAdd -contains $file.path){
        $known=Join-Path $work ('known-'+[IO.Path]::GetFileName($file.path))
        $url=$BaseUrl+[IO.Path]::GetFileNameWithoutExtension($file.path)+'-'+$build.buildId+[IO.Path]::GetExtension($file.path)
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $known
        if((Sha1 $known) -eq $file.sha1){$good[$file.path]=$known;Write-Output "$($file.path): the target's has changed; the build's from $url";continue}
    }
    throw "The target's $($file.path) is not the supported build's: run Check and repair on that installation first."
}
Write-Output ('Target checked in {0:0}s' -f $clock.Elapsed.TotalSeconds)

$entries=New-Object System.Collections.Generic.List[object]
$n=0
foreach($file in $build.files){
    $n++
    $to=$good[$file.path];$from=Join-Path $Source $file.path
    $haveSource=Test-Path -LiteralPath $from
    $sourceSize=if($haveSource){(Get-Item -LiteralPath $from).Length}else{0}
    $sourceSha1=if($haveSource){Sha1 $from}else{$null}
    if($haveSource -and $sourceSize -eq $file.size -and $sourceSha1 -eq $file.sha1 -and $AlwaysAdd -notcontains $file.path){
        $entries.Add([ordered]@{path=$file.path;op='copy';size=$file.size;sha1=$file.sha1});$what='copy'
    }elseif(-not $haveSource -or $AlwaysAdd -contains $file.path){
        $add=Join-Path (Join-Path $work 'add') $file.path
        New-Item -ItemType Directory -Force (Split-Path -Parent $add)|Out-Null
        Copy-Item -LiteralPath $to -Destination $add
        $entries.Add([ordered]@{path=$file.path;op='add';size=$file.size;sha1=$file.sha1});$what='add'
    }else{
        $patchFile=('{0:D3}-' -f $n)+($file.path -replace '[\\/]','_')+'.hdiff'
        $patch=Join-Path (Join-Path $work 'patches') $patchFile
        & $hdiffz -s-64 -c-zstd-21-24 -f $from $to $patch|Out-Null
        if($LASTEXITCODE){throw "hdiffz failed on $($file.path) (exit $LASTEXITCODE)."}
        if($Verify){
            $check=Join-Path $work 'verify.tmp'
            & $hpatchz -f $from $patch $check|Out-Null
            if($LASTEXITCODE -or (Sha1 $check) -ne $file.sha1){throw "The patch for $($file.path) does not reproduce it."}
            Remove-Item -LiteralPath $check
        }
        $entries.Add([ordered]@{path=$file.path;op='patch';size=$sourceSize;sha1=$sourceSha1;patchFile=$patchFile;targetSize=$file.size;targetSha1=$file.sha1})
        $what='patch '+[math]::Round((Get-Item -LiteralPath $patch).Length/1MB,1)+' MB'
    }
    Write-Output ('[{0,3}/{1}] {2,6:0}s  {3}: {4}' -f $n,$build.files.Count,$clock.Elapsed.TotalSeconds,$file.path,$what)
}

[IO.File]::WriteAllText((Join-Path $work 'downpatch.json'),([ordered]@{sourceBuild=$sourceBuild;targetBuild=$build.buildId;files=$entries}|ConvertTo-Json -Depth 4))
# Stored, not compressed: the patches are zstd already, and setup reads entries straight out of the zip.
$pack=Join-Path $Out ($name+'.zip')
if(Test-Path -LiteralPath $pack){Remove-Item -LiteralPath $pack}
$archive=[IO.Compression.ZipFile]::Open($pack,[IO.Compression.ZipArchiveMode]::Create)
try{
    foreach($item in Get-ChildItem -LiteralPath $work -Recurse -File|Where-Object {$_.Name -eq 'downpatch.json' -or $_.Directory.FullName.StartsWith((Join-Path $work 'patches')) -or $_.Directory.FullName.StartsWith((Join-Path $work 'add'))}){
        $entry=$item.FullName.Substring($work.Length+1).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$item.FullName,$entry,[IO.Compression.CompressionLevel]::NoCompression)|Out-Null
    }
}finally{$archive.Dispose()}

$entry=[ordered]@{
    sourceBuild=$sourceBuild;targetBuild=$build.buildId;url=$BaseUrl+$name+'.zip'
    sha256=(Get-FileHash -LiteralPath $pack).Hash;size=(Get-Item -LiteralPath $pack).Length
    sourceExeSha256=(Get-FileHash -LiteralPath (Join-Path $Source 'bin/helldivers2.exe')).Hash
    sourceGameDllSha256=(Get-FileHash -LiteralPath (Join-Path $Source 'data/game/game.dll')).Hash
}
[IO.File]::WriteAllText((Join-Path $Out ($name+'.index-entry.json')),($entry|ConvertTo-Json))

# The published index with this pack added (replacing one for the same source and target build).
$published=$null
if($Index){$published=Get-Content -Raw -LiteralPath $Index|ConvertFrom-Json}
else{try{$published=Invoke-RestMethod -UseBasicParsing -Uri ($BaseUrl+'index.json')}catch{Write-Output 'No published index.json yet: starting a new one.'}}
$packs=@(if($published){@($published.packs)|Where-Object {-not ($_.sourceBuild -eq $sourceBuild -and $_.targetBuild -eq $build.buildId)}})+@([pscustomobject]$entry)
[IO.File]::WriteAllText((Join-Path $Out 'index.json'),([ordered]@{schemaVersion=1;packs=@($packs|Sort-Object targetBuild,sourceBuild)}|ConvertTo-Json -Depth 4))
Remove-Item -LiteralPath $work -Recurse -Force
Write-Output ("Pack: {0} ({1:0.00} GB, SHA-256 {2}) in {3:0} min" -f $pack,($entry.size/1GB),$entry.sha256,$clock.Elapsed.TotalMinutes)
Write-Output ('Upload it and '+(Join-Path $Out 'index.json')+' to '+$BaseUrl)
