# Publishes HellForge into an installation's app folder: the launcher, command-line tool and Steam ownership probe as
# self-contained Windows builds (no .NET installation needed), the HellForge engine (its session runner, local backend,
# mod loader and stand-ins, and the network block and ownership scripts: a package built from the closed HellForge-Engine
# repository, pinned in engine.lock.json), the mod SDK, the bundled mods and the profile templates. Everything else in the
# installation (game, user data, profiles, logs) is left alone. The new app is staged in the installation's temp folder
# and swapped in at the end.
# -PayloadFolder instead writes the same files (app\ and the top-level program) into a folder for scripts/Package-HellForge.ps1.
# -EnginePackage: the engine package to use (a HellForge-Engine build); without it the pinned one, from
# artifacts/engine-packages or downloaded from engine.lock.json's url. Either must match the pin's SHA-256 unless
# -UnpinnedEngine is given (a local engine build under test).
param([string]$InstallRoot,[string]$PayloadFolder,[string]$EnginePackage,[switch]$UnpinnedEngine)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=Split-Path -Parent $PSScriptRoot
if([bool]$InstallRoot -eq [bool]$PayloadFolder){throw 'Pass -InstallRoot (update an installation) or -PayloadFolder (stage for packaging).'}
if($InstallRoot){
    $InstallRoot=[IO.Path]::GetFullPath($InstallRoot)
    # An installation from before the rename (HD2 Beyond The Stars) has bts-install.json; it gets the new name.
    $legacyMarker=Join-Path $InstallRoot 'bts-install.json'
    if(-not(Test-Path -LiteralPath (Join-Path $InstallRoot 'hellforge-install.json')) -and (Test-Path -LiteralPath $legacyMarker)){
        Move-Item -LiteralPath $legacyMarker -Destination (Join-Path $InstallRoot 'hellforge-install.json')
    }
    if(-not(Test-Path -LiteralPath (Join-Path $InstallRoot 'hellforge-install.json'))){throw ('Not a HellForge installation: '+$InstallRoot)}
    $running=@(Get-Process HellForge.Launcher,HellForge.LocalBackend,HellForge.Cli,HellForge.Session -ErrorAction SilentlyContinue|Where-Object {$_.Path -and $_.Path.StartsWith($InstallRoot+'\',[StringComparison]::OrdinalIgnoreCase)})
    if($running.Count){throw 'Close the installation''s launcher and sessions first.'}
    $staging=Join-Path $InstallRoot 'temp/app-staging'
}else{
    $PayloadFolder=[IO.Path]::GetFullPath($PayloadFolder)
    $staging=Join-Path $project 'artifacts/package/app-staging'
}
# The engine package: pinned, every file checked against its engine.json.
$lock=Get-Content -LiteralPath (Join-Path $project 'engine.lock.json') -Raw|ConvertFrom-Json
if(-not $EnginePackage){
    $EnginePackage=Join-Path $project ('artifacts/engine-packages/'+$lock.file)
    if(-not(Test-Path -LiteralPath $EnginePackage)){
        if(-not $lock.url){throw 'No engine package: build one in HellForge-Engine (scripts/Build-Engine.ps1) and pass -EnginePackage.'}
        New-Item -ItemType Directory (Split-Path -Parent $EnginePackage) -Force|Out-Null
        Invoke-WebRequest -Uri $lock.url -OutFile ($EnginePackage+'.partial') -UseBasicParsing
        Move-Item -LiteralPath ($EnginePackage+'.partial') -Destination $EnginePackage -Force
    }
}
$EnginePackage=[IO.Path]::GetFullPath($EnginePackage)
$engineHash=(Get-FileHash -LiteralPath $EnginePackage).Hash
if($engineHash -cne $lock.sha256 -and -not $UnpinnedEngine){throw ('The engine package does not match engine.lock.json ('+$engineHash+'); pass -UnpinnedEngine to use it anyway.')}

if(Test-Path -LiteralPath $staging){Remove-Item -LiteralPath $staging -Recurse -Force}
New-Item -ItemType Directory $staging|Out-Null
foreach($app in @(
    @{folder='launcher';project='src/HellForge.Launcher/HellForge.Launcher.csproj'},
    @{folder='cli';project='src/HellForge.Cli/HellForge.Cli.csproj'},
    @{folder='steamprobe';project='src/HellForge.SteamProbe/HellForge.SteamProbe.csproj'})){
    Write-Output ('Publishing '+$app.folder)
    dotnet publish (Join-Path $project $app.project) -c Release -r win-x64 --self-contained true -p:PublishSingleFile=false -p:DebugType=none -p:DebugSymbols=false --nologo -v q -o (Join-Path $staging $app.folder)
    if($LASTEXITCODE){throw ('Publishing '+$app.folder+' failed.')}
}

# The engine: session runner, backend, mod loader and stand-ins, and its scripts, as its package has them.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$engineFolder=Join-Path $staging 'engine-unpacked'
[IO.Compression.ZipFile]::ExtractToDirectory($EnginePackage,$engineFolder)
$engine=Get-Content -LiteralPath (Join-Path $engineFolder 'engine.json') -Raw|ConvertFrom-Json
if($engine.build -cne (Get-Content -LiteralPath (Join-Path $project 'src/HellForge.Core/supported-build.json') -Raw|ConvertFrom-Json).buildId){
    throw ('The engine package is for game build '+$engine.build+', not this HellForge''s.')
}
foreach($entry in $engine.files.PSObject.Properties){
    $file=Join-Path $engineFolder $entry.Name
    if(-not(Test-Path -LiteralPath $file) -or (Get-FileHash -LiteralPath $file).Hash -cne $entry.Value){throw ('The engine package is damaged: '+$entry.Name)}
}
foreach($folder in @('session','backend','sdk-shims','scripts')){Move-Item -LiteralPath (Join-Path $engineFolder $folder) -Destination $staging}
Copy-Item -LiteralPath (Join-Path $engineFolder 'LICENSE-ENGINE.txt') -Destination $staging
Copy-Item -LiteralPath (Join-Path $engineFolder 'engine.json') -Destination $staging
Remove-Item -LiteralPath $engineFolder -Recurse -Force
# The mod SDK (docs/first-mod.md): the headers, CMake helpers and tool pins, the mod template, and the docs a modder needs.
# Its tools (Zig, CMake, Ninja) are fetched into <installation>/tools on request (HellForge.Cli mod-tools), outside app/.
$sdkTarget=New-Item -ItemType Directory (Join-Path $staging 'sdk')
Copy-Item -Path (Join-Path $project 'native/mod-sdk/*') -Destination $sdkTarget -Recurse
# The licences: HellForge's beside the app, the SDK's (MIT-0: mods owe nothing) beside the SDK too.
foreach($name in @('LICENSE','LICENSE-SDK')){Copy-Item -LiteralPath (Join-Path $project $name) -Destination $staging}
Copy-Item -LiteralPath (Join-Path $project 'LICENSE-SDK') -Destination $sdkTarget
Copy-Item -LiteralPath (Join-Path $project 'templates') -Destination $sdkTarget -Recurse
# app/sdk/template-mod: the template filled in as "My Mod", to copy into <installation>/mods and rename (docs/first-mod.md).
# Its paths are the ones a mod folder in mods/ has: ../../app/sdk and ../../tools.
$filled=@{'{{id}}'='yourname.my-mod';'{{name}}'='My Mod';'{{dll}}'='my_mod';'{{sdk}}'='../../app/sdk';'{{tools}}'='../../tools';'{{install}}'=''}
$template=Join-Path $project 'templates/c-mod'
foreach($file in Get-ChildItem -LiteralPath $template -Recurse -File){
    $relative=$file.FullName.Substring($template.Length+1);$text=[IO.File]::ReadAllText($file.FullName)
    foreach($key in $filled.Keys){$relative=$relative.Replace($key,$filled[$key]);$text=$text.Replace($key,$filled[$key])}
    $destination=Join-Path $sdkTarget ('template-mod/'+$relative)
    New-Item -ItemType Directory (Split-Path -Parent $destination) -Force|Out-Null
    [IO.File]::WriteAllText($destination,$text.Replace("`r`n","`n"))
}
$sdkDocs=New-Item -ItemType Directory (Join-Path $sdkTarget 'docs')
foreach($name in @('first-mod.md','sdk-reference.md','mods.md','modding.md','stratagem-categories.md')){Copy-Item -LiteralPath (Join-Path $project ('docs/'+$name)) -Destination $sdkDocs}
# Example mods to read and copy into mods/ (docs/first-mod.md): their tracked files only (no local builds), with their
# CMake include pointed at the SDK's own cmake folder. more-voices is left out: it waits for wrapped game functions.
foreach($name in @('stratagem-compendium','stratagem-categories','blue-sentry','cube-gun','machine-gun-10k-code','talon-liberator','golden-liberator','hell-forge','hell-swarm','stats-example','machine-gun-10k','vanilla-plus')){
    foreach($file in @(git -C $project ls-files ('examples/mods/'+$name))){
        $destination=Join-Path $sdkTarget $file # app/sdk/examples/mods/<name>/...
        New-Item -ItemType Directory (Split-Path -Parent $destination) -Force|Out-Null
        Copy-Item -LiteralPath (Join-Path $project $file) -Destination $destination
        if((Split-Path -Leaf $file) -eq 'CMakeLists.txt'){
            $text=[IO.File]::ReadAllText($destination).Replace('include(../../../native/mod-sdk/cmake/','include(../../../cmake/')
            [IO.File]::WriteAllText($destination,$text)
        }
    }
}
if(-not(Test-Path -LiteralPath (Join-Path $sdkTarget 'examples/mods/stratagem-compendium/hellforge-mod.json'))){throw 'The example mods were not copied.'}
# The in-game editor, as a bundled mod (src/HellForge.Core/BundledMods.cs puts it into <installation>/mods before a session
# and makes its catalogue, icons and fonts there). Only these four files ship: the icons and fonts Build-Editor.ps1 may
# have extracted into its folder are this PC's game files.
& (Join-Path $PSScriptRoot 'Build-Editor.ps1')|Out-Null
if($LASTEXITCODE){throw 'Building the in-game editor failed.'}
$editorTarget=New-Item -ItemType Directory (Join-Path $staging 'bundled-mods/hellforge-editor')
foreach($name in @('hellforge_editor.dll','hellforge-mod.json','icon.png','imgui-LICENSE.txt')){Copy-Item -LiteralPath (Join-Path $project ('artifacts/editor/hellforge-editor/'+$name)) -Destination $editorTarget}
# Difficulty 11 (Hell Forge), bundled built so players need no compiler: its tracked files but src/ (the source ships in
# app/sdk/examples/mods/hell-forge), and the DLL. Its icon is made by its data spec from each player's game copy.
$hellForge=Join-Path $project 'examples/mods/hell-forge'
& (Join-Path $PSScriptRoot 'Build-Mod.ps1') $hellForge|Out-Null
$hellForgeTarget=Join-Path $staging 'bundled-mods/hell-forge'
foreach($file in @(git -C $project ls-files 'examples/mods/hell-forge')|Where-Object {$_ -notlike 'examples/mods/hell-forge/src/*'}){
    $destination=Join-Path $hellForgeTarget $file.Substring('examples/mods/hell-forge/'.Length)
    New-Item -ItemType Directory (Split-Path -Parent $destination) -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $project $file) -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $hellForge 'hell_forge.dll') -Destination $hellForgeTarget
Copy-Item -LiteralPath (Join-Path $project 'data/profile-templates.json') -Destination $staging
Copy-Item -LiteralPath (Join-Path $project 'data/progression-data.json') -Destination $staging
# "HellForge.exe" at the top of the installation starts app\launcher\HellForge.Launcher.exe (native/launcher-stub).
$zig=(& (Join-Path $PSScriptRoot 'Get-BuildTools.ps1')).Zig
$stub=Join-Path $staging 'HellForge.exe'
& $zig cc -target x86_64-windows-gnu -O2 -Wall -Werror -Wno-unused-parameter -municode '-Wl,--subsystem,windows' -s (Join-Path $project 'native/launcher-stub/launcher_stub.c') `
    (Join-Path $project 'native/launcher-stub/launcher_stub.rc') -o $stub # the .rc: its icon, HellForge's
if($LASTEXITCODE){throw 'Building the launcher program failed.'}
Get-ChildItem -LiteralPath $staging -File|Where-Object {$_.Extension -in @('.pdb','.lib')}|Remove-Item
$commit=(git -C $project rev-parse --short HEAD 2>$null)
$dirty=[bool](git -C $project status --porcelain 2>$null)
[ordered]@{builtUtc=[DateTime]::UtcNow.ToString('o');commit=$commit;uncommittedChanges=$dirty;engine=$engine.version;enginePackageSha256=$engineHash}|ConvertTo-Json|
    Set-Content -LiteralPath (Join-Path $staging 'app-build.json') -Encoding UTF8

$top=if($InstallRoot){$InstallRoot}else{
    if(Test-Path -LiteralPath $PayloadFolder){Remove-Item -LiteralPath $PayloadFolder -Recurse -Force}
    (New-Item -ItemType Directory $PayloadFolder).FullName
}
$app=Join-Path $top 'app'
if(Test-Path -LiteralPath $app){Remove-Item -LiteralPath $app -Recurse -Force}
Move-Item -LiteralPath $staging -Destination $app
$start=Join-Path $top 'HellForge.exe'
Move-Item -LiteralPath (Join-Path $app 'HellForge.exe') -Destination $start -Force
# The starter's name from before the rename.
$legacyStart=Join-Path $top 'HD2 Beyond The Stars.exe'
if(Test-Path -LiteralPath $legacyStart){Remove-Item -LiteralPath $legacyStart -Force}
$size=(Get-ChildItem -LiteralPath $app -File -Recurse|Measure-Object Length -Sum).Sum
Write-Output ('Published HellForge into {0} ({1:N0} MB). Start {2}' -f $app,($size/1MB),$start)
