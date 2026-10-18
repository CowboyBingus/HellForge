# Builds a HellForge mod (docs/mods.md). A code mod: compiles the C or C++ sources in <mod folder>/src against the HellForge
# mod SDK (native/mod-sdk/hellforge_mod.h) with the project's pinned Zig, into the DLL the mod's hellforge-mod.json names
# ("code": {"dll"}), next to the manifest. A mod with a data spec (hellforge-data.json): checks it and, given an installation
# (-Install, or HELLFORGE_INSTALL), builds its data from that installation's game copy into the installation's cache, as a
# session would (HellForge.Cli build-data); generated data never goes into the mod folder. Copy or link the mod folder into
# <installation>/mods to use it. A stats mod (hellforge-stats.json) is validated against the pinned schema without game files.
# Usage: scripts/Build-Mod.ps1 <mod folder> [-Install <installation>]
param([Parameter(Mandatory=$true)][string]$ModFolder,[string]$Install=$env:HELLFORGE_INSTALL)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=Split-Path -Parent $PSScriptRoot
$ModFolder=[IO.Path]::GetFullPath($ModFolder)
$manifestPath=Join-Path $ModFolder 'hellforge-mod.json'
if(-not(Test-Path -LiteralPath $manifestPath)){$manifestPath=Join-Path $ModFolder 'bts-mod.json'}  # a mod from before the rename
$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$hasCode=[bool]$manifest.PSObject.Properties['code']
$hasData=Test-Path -LiteralPath (Join-Path $ModFolder 'hellforge-data.json')
$hasStats=Test-Path -LiteralPath (Join-Path $ModFolder 'hellforge-stats.json')
if(-not $hasCode -and -not $hasData -and -not $hasStats){throw 'Nothing to build: the mod has no code, data spec or stats file.'}
if($hasCode){
    $zig=(& (Join-Path $PSScriptRoot 'Get-BuildTools.ps1')).Zig
    if($manifest.code.dll -notmatch '^[A-Za-z0-9_.-]{1,64}\.dll$'){throw 'hellforge-mod.json needs "code": {"dll": "<name>.dll", "sdk": 1}.'}
    $sources=@(Get-ChildItem -LiteralPath (Join-Path $ModFolder 'src') -Recurse -File|Where-Object {$_.Extension -in @('.c','.cpp')}|ForEach-Object FullName)
    if(-not $sources.Count){throw 'No C or C++ sources in the mod''s src folder.'}
    $cpp=[bool]@($sources|Where-Object {$_ -like '*.cpp'}).Count
    $output=Join-Path $ModFolder $manifest.code.dll
    $language=if($cpp){@('c++','-std=c++17')}else{@('cc','-std=c11')}
    & $zig @language -target x86_64-windows-gnu -shared -O2 -Wall -Werror -Wno-unused-parameter -s -I (Join-Path $project 'native/mod-sdk') @sources -o $output
    if($LASTEXITCODE){throw 'The mod did not compile.'}
    Get-ChildItem -LiteralPath $ModFolder -File|Where-Object {$_.Extension -in @('.lib','.pdb')}|Remove-Item
    Write-Output ('Built {0} ({1:N0} KB)' -f $output,((Get-Item -LiteralPath $output).Length/1KB))
}
if($hasData){
    $cliArgs=@('build-data','--mod',$ModFolder)
    if($Install){$cliArgs+=@('--install',[IO.Path]::GetFullPath($Install))}
    # Its progress goes to stderr, which Windows PowerShell's 'Stop' would turn into a failure: shown, not fatal.
    $preference=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{& dotnet run --project (Join-Path $project 'src/HellForge.Cli') -v q -- @cliArgs 2>&1|ForEach-Object {"$_"}}finally{$ErrorActionPreference=$preference}
    if($LASTEXITCODE){throw 'The mod''s data spec did not build; see the message above.'}
    if(-not $Install){Write-Output 'Data spec checked. Pass -Install <installation> (or set HELLFORGE_INSTALL) to build its data now; otherwise the next session builds it.'}
}
if($hasStats){
    & dotnet run --project (Join-Path $project 'src/HellForge.Cli') -v q -- check-stats --mod $ModFolder
    if($LASTEXITCODE){throw 'The mod''s stats did not validate; see the message above.'}
    Write-Output 'Stats checked against the supported schema. No DLL or game files are required for stats-only mods.'
}
