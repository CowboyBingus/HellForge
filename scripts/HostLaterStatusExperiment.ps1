# Optional host-only source adaptation. Original pinned VM implementation is unchanged.
function ConvertTo-HellForgeHostLaterStatusExperiment([string]$Source){
    function Replace-ExactlyOnce([string]$Text,[string]$Old,[string]$New){
        if(([regex]::Matches($Text,[regex]::Escape($Old))).Count -ne 1){throw 'Unexpected host experiment source; adaptation declined.'}
        return $Text.Replace($Old,$New)
    }
    $Source=Replace-ExactlyOnce $Source 'public bool MainCheckWriteAttempted, MainCheckWritten;' @'
public bool MainCheckWriteAttempted, MainCheckWritten;
    public bool LaterStatusWriteAttempted, LaterStatusWritten, LaterStatusRolledBack;
    public string LoopStatusBefore, LoopStatusAfter, FinalStatusBefore, FinalStatusAfter;
'@
    $Source=Replace-ExactlyOnce $Source 'int mainCheck=fixture?138:0x5a53e;' @'
int mainCheck=fixture?138:0x5a53e;
        if(fixture)throw new InvalidOperationException("Later-status adaptation is host-only.");
        byte[] loopStatusBefore=Hex("e80e908e003d550700000f84b4000000");
        byte[] finalStatusBefore=Hex("e8628591003d550700007570");
        byte[] loopStatusPatch=Hex("e9b500000090"), finalStatusPatch=Hex("9090");
'@
    $Source=Replace-ExactlyOnce $Source 'long rip=Marshal.ReadInt64(context,248);' @'
long rip=Marshal.ReadInt64(context,248);
                        if((rip>=imageBase+0x89b55&&rip<imageBase+0x89b65)||(rip>=imageBase+0x5a601&&rip<imageBase+0x5a60d))throw new InvalidOperationException("Thread executing later status-check site.");
'@
    $Source=Replace-ExactlyOnce $Source 'r.ConditionsMatched=true;r.WriteAttempted=true;' @'
r.LoopStatusBefore=Hex(Read(target,imageBase+0x89b55,loopStatusBefore.Length));
            r.FinalStatusBefore=Hex(Read(target,imageBase+0x5a601,finalStatusBefore.Length));
            if(r.LoopStatusBefore!=Hex(loopStatusBefore)||r.FinalStatusBefore!=Hex(finalStatusBefore)){r.Outcome="later_status_bytes_mismatch";return r;}
            r.ConditionsMatched=true;r.WriteAttempted=true;
'@
    $Source=Replace-ExactlyOnce $Source 'r.ReadbackMatched=true;r.Outcome="applied_at_candidate_boundary";' @'
r.LaterStatusWriteAttempted=true;
            WriteBytes(target,imageBase+0x89b5f,loopStatusPatch);
            WriteBytes(target,imageBase+0x5a60b,finalStatusPatch);
            r.LoopStatusAfter=Hex(Read(target,imageBase+0x89b55,loopStatusBefore.Length));
            r.FinalStatusAfter=Hex(Read(target,imageBase+0x5a601,finalStatusBefore.Length));
            if(r.LoopStatusAfter!="e80e908e003d55070000e9b500000090"||r.FinalStatusAfter!="e8628591003d550700009090")throw new InvalidOperationException("Later status full-window readback mismatch.");
            r.LaterStatusWritten=true;
            r.ReadbackMatched=true;r.Outcome="applied_at_candidate_boundary";
'@
    $Source=Replace-ExactlyOnce $Source 'if(r.WriteAttempted)try{if(r.MainCheckWriteAttempted)' @'
if(r.WriteAttempted)try{if(r.LaterStatusWriteAttempted){WriteBytes(target,imageBase+0x89b5f,Hex("0f84b4000000"));WriteBytes(target,imageBase+0x5a60b,Hex("7570"));r.LaterStatusRolledBack=true;}if(r.MainCheckWriteAttempted)
'@
    return $Source
}
