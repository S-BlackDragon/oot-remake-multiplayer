# Every daurosk scenario (each <name>.A.txt) through r03_pareja, one after the other; one summary line each.
#   powershell -ExecutionPolicy Bypass -File tools/orm/harness\r03_todos.ps1 [-Then "<command>"] [-Only a,b] [-Repeat N] [-Out folder]
# Stops early if a STOP file appears in the repo root. -Then runs one more command at the end (e.g. the model queue).
# A failing run keeps both games' logs next to its summary (<name>_A_eden_log.txt, <name>_B_eden_log.txt).
param([string]$Then = "", [string[]]$Only = @(), [int]$Repeat = 1, [string]$Out = "todos")
$ErrorActionPreference = "Continue"
$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })  # -File passes "a,b" as one string
$Repo = Resolve-Path (Join-Path $PSScriptRoot "..\..\..")
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
Set-Location $Repo
$rep = "$Reports\pareja\$Out"
New-Item -ItemType Directory -Force $rep | Out-Null
$sum = "$rep\resumen.txt"
"daurosk scenarios through r03_pareja $(Get-Date -Format s)" | Set-Content -Encoding utf8 $sum
$names = Get-ChildItem (Join-Path $(if ($env:ORM_TREE) { $env:ORM_TREE } else { Join-Path $OrmRoot "tree" }) "tools\autotest\scenarios") -Filter *.A.txt |
    ForEach-Object { $_.Name -replace '\.A\.txt$', '' } |
    Where-Object { $Only.Count -eq 0 -or $Only -contains $_ }
$runs = foreach ($n in $names) { foreach ($i in 1..$Repeat) { [pscustomobject]@{ n = $n; i = $i } } }
foreach ($run in $runs) {
    $n = $run.n
    $tag = if ($Repeat -gt 1) { "$($n)_$($run.i)" } else { $n }
    if (Test-Path (Join-Path $Repo "STOP")) { "stopped: STOP file" | Add-Content -Encoding utf8 $sum; break }
    $t0 = Get-Date
    python -I tools/orm/harness\r03_pareja.py --scenario $n *> $null
    $rc = $LASTEXITCODE
    $f = "$Reports\pareja\$($n)_resumen.txt"
    $res = "CRASH(exit $rc)"; $why = "no summary written"
    if (Test-Path $f) {
        $res = (Select-String -Path $f -Pattern '^RESULT' | ForEach-Object Line) -join ""
        $why = (Select-String -Path $f -Pattern 'FAIL|FROZEN|SLOW|fps after' | Select-Object -First 3 |
            ForEach-Object { ($_.Line.Trim() -split ', critical')[0] }) -join " / "
        Move-Item -Force $f "$rep\$($tag)_resumen.txt"
    }
    Move-Item -Force "$Reports\pareja\$($n)_server.log" "$rep\$($tag)_server.log" -ErrorAction SilentlyContinue
    foreach ($p in "A", "B") {
        $l = "$Reports\pareja\$($n)_$($p)_eden_log.txt"
        if (Test-Path $l) { Move-Item -Force $l "$rep\$($tag)_$($p)_eden_log.txt" }
    }
    ("{0,-16} {1,-26} {2,4:N0} s  {3}" -f $tag, $res, ((Get-Date) - $t0).TotalSeconds, $why) | Add-Content -Encoding utf8 $sum
}
"done $(Get-Date -Format s)" | Add-Content -Encoding utf8 $sum
if ($Then) { Invoke-Expression $Then }
