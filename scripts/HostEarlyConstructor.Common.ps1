# Host adaptation of the pinned VM initialization experiment. Original VM source is unchanged.
# The research copy stays pinned to the research host; an installation (-AnyHost) runs on any player's PC, where the
# copied exe's path (and the session's hash verification of the game files) identify the target instead.
function New-HellForgeHostEarlyConstructorSource([string]$Exe,[switch]$SkipLaterStatusFailures,[switch]$AnyHost){
    $original=Join-Path $PSScriptRoot 'EarlyConstructorExperiment.cs'
    if((Get-FileHash $original).Hash -ne '3C30CA3C6C6C4371E0153A565077896E48E6A2C3C61A34293809B4CFFC644D1B'){throw 'Reviewed experiment changed.'}
    $text=Get-Content $original -Raw
    $machine='Environment.MachineName!=(steamGuest?"BTS-STEAM-SETUP":"BTS-WIN-LAB")'
    if(-not $text.Contains($machine)){throw 'Expected VM identity expression missing.'}
    $text=$text.Replace($machine,$(if($AnyHost){'false'}else{'Environment.GetEnvironmentVariable("HELLFORGE_RESEARCH")!="1"'}))
    $wanted=@($text -split "`n"|Where-Object {$_ -match '^\s+string wanted=fixture\?'})
    if($wanted.Count -ne 1){throw 'Unexpected process identity declaration.'}
    $text=$text.Replace($wanted[0],('            if(fixture)throw new InvalidOperationException("Host fixture entry disabled."); string wanted=@"'+$Exe+'";'))
    $text=$text.Replace('BtsEarlyConstructorExperiment','HellForgeHostEarlyConstructorExperiment').Replace('BtsEarlyResult','HellForgeHostEarlyResult').Replace('BtsEarlyThread','HellForgeHostEarlyThread')
    # A newly resumed process can briefly have no MainModule. Retry within the existing deadline.
    $moduleRead='process.Refresh();imageBase=process.MainModule.BaseAddress.ToInt64();'
    if(-not $text.Contains($moduleRead)){throw 'Expected module readiness expression missing.'}
    $text=$text.Replace($moduleRead,'try{process.Refresh();var mainModule=process.MainModule;if(mainModule!=null)imageBase=mainModule.BaseAddress.ToInt64();}catch(NullReferenceException moduleError){r.ReadFailures++;r.Failure="Transient module query: "+moduleError.ToString();}')
    $text=$text.Replace('r.Failure=e.GetType().Name+": "+e.Message;','r.Failure=e.ToString();')
    $text=$text.Replace('// Guest-only, fixed-build timing experiment. Never edits game files or hides a debugger.','// Host copy adaptation of the pinned VM experiment. Memory edits only; fixed host and copied image.')
    if($SkipLaterStatusFailures){
        . (Join-Path $PSScriptRoot 'HostLaterStatusExperiment.ps1')
        $text=ConvertTo-HellForgeHostLaterStatusExperiment $text
    }
    return $text
}
