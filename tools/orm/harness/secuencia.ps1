# Runs every daurosk pair scenario (tools/autotest/scenarios/<name>.A.txt) through r03_pareja.py one after another,
# by WebSocket through orm_server, and leaves reports/R0.3/<Tag>/resumen.txt (one line per scenario) plus each
# scenario's summary, server log and the two game logs. One pair at a time; stops when a STOP file appears at the
# repository root; waits while another game instance runs (an agent's test). Below-normal priority.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/orm/harness\secuencia.ps1 -Tag todos_v57 [-Only walk,pose] [-Transport ws]
# Defaults: the test8 stack (D-R16, docs/CAMBIO_TEST8.md). Another stack: -Stack test9 (or ORM_STACK=test9 in the
# environment) sets ORM_BUILD_BIN, ORM_SERVER_EXE, ORM_BOT_EXE and ORM_INST_DIR where they are not set
# (tools\harness\protocolo.py, docs\ARNES.md) and stops with a clear line in resumen.txt if one of those is missing;
# pass -ScenDir <tree>\tools\autotest\scenarios for that tree's scenarios (and its load.txt).
# Ours: -ScenDir tools/orm/harness\escenarios -Only caer-solo -Players 1 -Switches LICHII_COOP_DOWNED=1,...
# -Switches takes LICHII_COOP_*, LICHII_ORM_* and EDEN_NX2_* (a value may hold commas: LICHII_COOP_DUNGEON_LISTS=a,b);
# a renamed switch reaches the hook under the name its stack reads (LICHII_ORM_STORY_EVENTS=1 on test8 also gives
# LICHII_COOP_STORY=1; LICHII_COOP_STORY=1 on test9 also gives LICHII_ORM_STORY_EVENTS=1).
# -Caliente: hot instances (caliente.py): one pair of games per family of scenarios, each scenario after a warp and the
# undo of what the last one set, a bad state or a hot FAIL run cold; -NoLock when the caller holds the game lock.
param(
    [string]$Tag = ("todos_" + (Get-Date -Format "yyyyMMdd")),
    [string[]]$Only = @(),
    [string]$Transport = "ws",
    [int]$Timeout = 420,
    [string[]]$Switches = @(),    # extra hook switches K=V for every scenario (r03_pareja --env), e.g. -Switches LICHII_COOP_DOWNED=1
    [string]$ScenDir = "",     # default: the escenarios folder next to this script; <name>.A.txt and load.txt (r03_pareja --scen-dir)
    [int]$Players = 2,            # r03_pareja --players (1 for our one-game scenarios)
    [string]$Stack = "",          # test8 | test9: ORM_STACK for this run (protocolo.py)
    [switch]$HeaderSwitches,      # also each scenario's '#! switches' (before -Switches, which win) and '#! timeout'
    [switch]$Caliente,            # hot instances: caliente.py plays the list in one booted pair per family (docs\ARNES.md)
    [switch]$NoLock,              # -Caliente: the game lock is already held by the caller (caliente.py takes it otherwise)
    [switch]$NoRecheck,           # -Caliente: a hot FAIL with healthy games is the result (default: it runs cold too)
    [switch]$Plan,                # -Caliente: write the plan and the hot scripts only (no game started)
    [string]$Join = ""            # A-first | B-first: r03_pareja --join for every scenario (over its '#! join')
)
$ErrorActionPreference = "Continue"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
$Proto = Join-Path $PSScriptRoot "protocolo.py"
if (-not $ScenDir) { $ScenDir = Join-Path $PSScriptRoot "escenarios" }
if ($Stack) { $env:ORM_STACK = $Stack }
$Out = Join-Path $Reports "pareja\$Tag"
New-Item -ItemType Directory -Force $Out | Out-Null
$Sum = Join-Path $Out "resumen.txt"
$Log = Join-Path $Out "secuencia.log"
# The stack: 'set K=V' lines fill the environment (r03_pareja inherits it), 'problem:' lines stop the run here.
$stackLines = @(& python -I $Proto env 2>&1 | ForEach-Object { "$_" })
$stackCode = $LASTEXITCODE
foreach ($l in $stackLines) {
    if ($l -match '^set ([A-Z_]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}
$notes = @($stackLines | Where-Object { $_ -notmatch '^set ' })
if ($stackCode -ne 0) {
    @("secuencia $Tag NOT RUN $(Get-Date -Format s): the stack is not ready") + $notes + @("RESULT: NOT RUN") |
        Set-Content -Encoding utf8 $Sum
    exit 1
}
# The switches as this stack's hook reads them (renamed LICHII_ORM_* / LICHII_COOP_* names, protocolo.hook_switches).
$swLines = @(& python -I $Proto switches @($Switches | Where-Object { $_ }) 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0) {
    @("secuencia $Tag NOT RUN $(Get-Date -Format s): bad -Switches") + $swLines + @("RESULT: NOT RUN") |
        Set-Content -Encoding utf8 $Sum
    exit 1
}
$hookSwitches = @($swLines | Where-Object { $_ })
$Scen = (Resolve-Path $ScenDir).Path
if ($Caliente) {
    # One command: caliente.py groups the scenarios into families, boots a pair once per family and writes
    # resumen.txt itself (these lines, plus the mode of each scenario: caliente, frio, frio tras caliente).
    $calArgs = @("--tag", $Tag, "--scen-dir", $Scen, "--players", $Players, "--transport", $Transport, "--timeout", $Timeout)
    foreach ($o in @($Only | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ })) { $calArgs += @("--only", $o) }
    foreach ($e in @($Switches | Where-Object { $_ })) { $calArgs += @("--switch", $e) }
    if ($HeaderSwitches) { $calArgs += "--header-switches" }
    if ($NoLock) { $calArgs += "--no-lock" }
    if ($NoRecheck) { $calArgs += "--no-recheck" }
    if ($Plan) { $calArgs += "--plan" }
    Set-Location $Repo
    & python -I (Join-Path $PSScriptRoot "caliente.py") @calArgs 2>&1 | ForEach-Object { "$_" } | Add-Content -Encoding utf8 $Log
    exit $LASTEXITCODE
}
$Inst = if ($env:ORM_INST_DIR) { $env:ORM_INST_DIR } else { Join-Path $OrmRoot "inst" }
$stackName = if ($env:ORM_STACK) { $env:ORM_STACK } else { "-" }
"secuencia $Tag started $(Get-Date -Format s) transport $Transport players $Players scenarios $Scen stack $stackName build $env:ORM_BUILD_BIN server $env:ORM_SERVER_EXE instances $Inst switches $($hookSwitches -join ',')$(if ($Join) { " join $Join" })" | Set-Content -Encoding utf8 $Sum
$notes | Add-Content -Encoding utf8 $Sum
"" | Set-Content -Encoding utf8 $Log
$names = Get-ChildItem $Scen -Filter "*.A.txt" | ForEach-Object { $_.Name -replace '\.A\.txt$', '' } | Sort-Object
# -Only walk,pose arrives as one string under -File: split it.
$Only = @($Only | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ })
if ($Only.Count -gt 0) { $names = @($names | Where-Object { $Only -contains $_ }) }
"$($names.Count) scenarios: $($names -join ' ')" | Add-Content -Encoding utf8 $Sum
$pass = 0; $fail = 0; $t0 = Get-Date
foreach ($n in $names) {
    if (Test-Path (Join-Path $Repo "STOP")) { "STOP file: stopped before $n" | Add-Content -Encoding utf8 $Sum; break }
    $waited = 0
    while ((Get-Process -Name eden-cli, lichii -ErrorAction SilentlyContinue) -and $waited -lt 1800) { Start-Sleep -Seconds 15; $waited += 15 }
    $s = Get-Date
    Set-Location $Repo
    $envArgs = @()
    $runSwitches = $hookSwitches
    $runTimeout = $Timeout
    if ($HeaderSwitches) {
        $aFile = Join-Path $Scen "$n.A.txt"
        $swOne = @(& python -I $Proto switches --scenario $aFile @($Switches | Where-Object { $_ }) 2>&1 | ForEach-Object { "$_" })
        if ($LASTEXITCODE -ne 0) {
            $fail++
            ("{0,-16} {1,-28} {2,5:N0} s" -f $n, "RESULT: NO SUMMARY ($(($swOne | Select-Object -Last 1) -replace '^problem: ', ''))", 0) |
                Add-Content -Encoding utf8 $Sum
            continue
        }
        $runSwitches = @($swOne | Where-Object { $_ })
        $tm = Select-String -Path $aFile -Pattern '^#!\s*timeout\s+(\d+)' | Select-Object -First 1
        if ($tm -and [int]$tm.Matches[0].Groups[1].Value -gt $runTimeout) { $runTimeout = [int]$tm.Matches[0].Groups[1].Value }
        "$n switches $($runSwitches -join ',') timeout $runTimeout" | Add-Content -Encoding utf8 $Log
    }
    foreach ($e in $runSwitches) { $envArgs += @("--env", $e) }
    if ($Join) { $envArgs += @("--join", $Join) }
    & python -I tools/orm/harness/r03_pareja.py --scenario $n --scen-dir $Scen --players $Players --transport $Transport --timeout $runTimeout @envArgs 2>&1 |
        ForEach-Object { "$_" } | Add-Content -Encoding utf8 $Log
    $code = $LASTEXITCODE
    $res = Join-Path $Reports "pareja\${n}_resumen.txt"
    $line = if (Test-Path $res) { (Select-String -Path $res -Pattern "^RESULT" | Select-Object -Last 1).Line } else { "RESULT: NO SUMMARY (exit $code)" }
    if ($line -match "RESULT: PASS") { $pass++ } else { $fail++ }
    # the scenario's content checks (#! expect / never / count): r03_pareja writes "expect: n/m ok"
    $exp = if (Test-Path $res) { Select-String -Path $res -Pattern "^expect: (\d+/\d+) ok" | Select-Object -Last 1 } else { $null }
    if ($exp) { $line = $line -replace "^(RESULT: \w+)", ("`$1 (expect " + $exp.Matches[0].Groups[1].Value + ")") }
    ("{0,-16} {1,-28} {2,5:N0} s" -f $n, $line, ((Get-Date) - $s).TotalSeconds) | Add-Content -Encoding utf8 $Sum
    foreach ($f in @("${n}_resumen.txt", "${n}_server.log")) {
        $p = Join-Path $Reports "pareja\$f"
        if (Test-Path $p) { Copy-Item $p (Join-Path $Out $f) -Force }
    }
    foreach ($k in @(1, 2)[0..($Players - 1)]) {
        $lg = Join-Path $Inst "n$k\user\log\eden_log.txt"
        if (Test-Path $lg) { Copy-Item $lg (Join-Path $Out ("{0}_{1}_eden_log.txt" -f $n, @("A", "B")[$k - 1])) -Force }
    }
}
("total: {0} PASS, {1} FAIL, {2:N0} min" -f $pass, $fail, ((Get-Date) - $t0).TotalMinutes) | Add-Content -Encoding utf8 $Sum
"DONE $(Get-Date -Format s)" | Add-Content -Encoding utf8 $Sum
