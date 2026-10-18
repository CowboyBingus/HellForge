# The native build tools, downloaded once into <root>/tools (gitignored), each pinned by SHA-256: portable Zig 0.17.0
# (the C and C++ toolchain) and MinHook 1.3.4 (BSD-2, hooks for the in-game editor). Returns their paths:
#     $tools=& scripts/Get-BuildTools.ps1; & $tools.Zig cc ...; $tools.MinHook (its source folder)
# -Root: whose tools folder (the HellForge-Engine build passes its own).
param([string]$Root)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$ProgressPreference='SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.IO.Compression.FileSystem
if(-not $Root){$Root=Split-Path -Parent $PSScriptRoot}
$tools=Join-Path $Root 'tools'
$downloads=Join-Path $tools 'downloads'

function Get-Pinned([string]$Url,[string]$Sha256,[string]$Path){
    if((Test-Path -LiteralPath $Path) -and (Get-FileHash -LiteralPath $Path).Hash -eq $Sha256){return}
    New-Item -ItemType Directory (Split-Path -Parent $Path) -Force|Out-Null
    $partial=$Path+'.partial'
    Invoke-WebRequest -Uri $Url -OutFile $partial -UseBasicParsing
    if((Get-FileHash -LiteralPath $partial).Hash -ne $Sha256){Remove-Item -LiteralPath $partial;throw ('Checksum mismatch: '+$Url)}
    Move-Item -LiteralPath $partial -Destination $Path -Force
}
# Archives are extracted into a fresh, empty folder, which records the archive it came from so an unchanged
# package is not extracted again.
function Expand-Fresh([string]$Archive,[string]$Destination){
    $marker=Join-Path $Destination '.hellforge-archive-sha256'
    $sha=(Get-FileHash -LiteralPath $Archive).Hash
    if((Test-Path -LiteralPath $marker) -and [IO.File]::ReadAllText($marker) -ceq $sha){return}
    if(Test-Path -LiteralPath $Destination){Remove-Item -LiteralPath $Destination -Recurse -Force}
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive,$Destination)
    [IO.File]::WriteAllText($marker,$sha)
}

$zigDirectory=Join-Path $tools 'zig'
$zig=Join-Path $zigDirectory 'zig.exe'
if(-not(Test-Path -LiteralPath $zig) -or (& $zig version) -ne '0.17.0'){
    $archive=Join-Path $downloads 'zig-x86_64-windows-0.17.0.zip'
    Get-Pinned 'https://ziglang.org/download/0.17.0/zig-x86_64-windows-0.17.0.zip' 'B5663F69581DCF391293FBF16C06CB80D81D806545CE618B4D0BAB7F0EB8C428' $archive
    $staging=Join-Path $tools 'zig-staging'
    Expand-Fresh $archive $staging
    if(Test-Path -LiteralPath $zigDirectory){Remove-Item -LiteralPath $zigDirectory -Recurse -Force}
    Move-Item -LiteralPath (Join-Path $staging 'zig-x86_64-windows-0.17.0') -Destination $zigDirectory
    Remove-Item -LiteralPath $staging -Recurse -Force
}
$minHook=Join-Path $tools 'minhook-1.3.4'
$archive=Join-Path $downloads 'minhook-1.3.4.zip'
Get-Pinned 'https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip' '172708123DAA0C98D20D3A980B16A50BE14AF243DC95DEE6F79C24193AD010E4' $archive
Expand-Fresh $archive $minHook
[pscustomobject]@{Zig=$zig;MinHook=(Join-Path $minHook 'minhook-1.3.4');Tools=$tools;Downloads=$downloads}
