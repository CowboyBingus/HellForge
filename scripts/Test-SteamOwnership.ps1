# This bounded host-side check talks to the existing Steam client, separate from game isolation.
# Steam may briefly display the app as active. The game and GameGuard are not launched.
# A HellForge installation passes its own copy's Steam API (-SteamApi) and a work folder inside the installation (-WorkRoot);
# an installed app keeps the probe in app/steamprobe beside app/scripts.
param([string]$SteamRoot = 'C:\Program Files (x86)\Steam', [switch]$PassThru, [switch]$NoBuild, [string]$EvidencePath,
    [string]$SteamApi, [string]$WorkRoot)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$source = if ($SteamApi) { $SteamApi } else { Join-Path $SteamRoot 'steamapps/common/Helldivers 2/bin/steam_api64.dll' }
if (-not (Get-Process -Name steam -ErrorAction SilentlyContinue)) { throw 'Start Steam and sign in before checking ownership.' }
$signature = Get-AuthenticodeSignature -LiteralPath $source
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Valve Corp\.') { throw 'Steam API must have a valid Valve signature.' }
$expectedHash = (Get-FileHash -LiteralPath $source).Hash
$run = Join-Path $(if ($WorkRoot) { $WorkRoot } else { Join-Path $projectRoot 'artifacts/ownership' }) ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $run 'steam_api64.dll')
Set-Content -LiteralPath (Join-Path $run 'steam_appid.txt') -Value '553850' -Encoding Ascii
$installedProbe = Join-Path $PSScriptRoot '../steamprobe/HellForge.SteamProbe.exe'
if (-not $NoBuild -and -not (Test-Path -LiteralPath $installedProbe)) {
    $buildOutput = @(dotnet build (Join-Path $projectRoot 'src/HellForge.SteamProbe/HellForge.SteamProbe.csproj') --nologo -m:1 -nr:false -p:UseSharedCompilation=false)
    if ($LASTEXITCODE -ne 0) { throw ('Steam probe build failed: ' + ($buildOutput -join "`n")) }
    if (-not $PassThru) { $buildOutput | Write-Output }
}
$report = Join-Path $run 'result.json'
$probe = if (Test-Path -LiteralPath $installedProbe) { [IO.Path]::GetFullPath($installedProbe) } else { Join-Path $projectRoot 'src/HellForge.SteamProbe/bin/Debug/net10.0/HellForge.SteamProbe.exe' }
$process = Start-Process -FilePath $probe -WindowStyle Hidden -WorkingDirectory $run -PassThru -ArgumentList @(
    ('"' + (Join-Path $run 'steam_api64.dll') + '"'), $expectedHash, ('"' + $report + '"')) `
    -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
[void]$process.Handle # Retain a process handle so PowerShell can retrieve the exit code after a fast exit.
if (-not $process.WaitForExit(15000)) {
    $process.Kill()
    $process.WaitForExit()
    [ordered]@{ status = 'ownership_unverified'; readyToLaunchGame = $false; failure = 'Steam probe timed out.' } |
        ConvertTo-Json | Set-Content -LiteralPath $report
    throw ('Steam ownership check timed out. Evidence: ' + $run)
}
$process.Refresh()
if ($process.ExitCode -ne 0) { throw ('Steam ownership was not confirmed. Evidence: ' + $run) }
$observation = Get-Content -Raw -LiteralPath $report | ConvertFrom-Json
if ($observation.localClientOwnershipConfirmed -ne $true) { throw 'Live probe returned incomplete ownership evidence.' }
if ($EvidencePath) { Copy-Item -LiteralPath $report -Destination $EvidencePath }
if ($PassThru) { $true } else { Write-Output ('Ownership evidence: ' + $run); Get-Content -LiteralPath $report }
