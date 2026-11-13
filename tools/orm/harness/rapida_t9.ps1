# rapida (test9): the six fastest of our two-game scenarios on the test9 stack (protocol 109), one command, one
# summary: <ORM_REPORTS>\pareja\rapida_t9\resumen.txt (one line per scenario, "total: N PASS, M FAIL", DONE).
#   powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File tools/orm/harness\rapida_t9.ps1
#
# ORM_STACK=test9 (protocolo.py) unless -Stack: the emulator, orm_server, bot and instance folder of test9, each one
# kept if already set in the environment. If one is missing, the summary says which and nothing runs. Then it takes
# the game lock (<ORM_LOG_ROOT>\game.lock, a folder, as carga6.ps1 and aguante.py), waiting up to -LockWaitMinutes
# (and while a STOP file is in the repo root), runs secuencia.ps1 with the scenarios' own '#! switches' and
# '#! timeout' (-HeaderSwitches) plus the switches below for all six, and gives the lock back. Doc: docs/ARNES.md.
param(
    [string]$Stack = "test9",
    [string]$Tag = "rapida_t9",
    [string]$Lock = "",    # default <ORM_LOG_ROOT>\game.lock
    [int]$LockWaitMinutes = 60,
    [int]$Timeout = 480,          # historia needs 480 (its header note); the others end well before
    [string[]]$Only = @(),        # a subset of the six (default: all)
    [string[]]$Scenarios = @(),   # other scenarios instead of the six (docs/SONDAS.md: a probe or its confirmation)
    [switch]$NoCommon,            # without the six's common switches below: only each scenario's '#! switches'
    [string]$Join = ""            # A-first | B-first for every scenario (secuencia.ps1 -Join, r03_pareja --join)
)
$ErrorActionPreference = "Continue"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
$Out = Join-Path $Reports "pareja\$Tag"
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not $Lock) { $Lock = Join-Path $LogRoot "game.lock" }
$Sum = Join-Path $Out "resumen.txt"
$Proto = Join-Path $PSScriptRoot "protocolo.py"
$env:ORM_STACK = $Stack

$Scenarios = @($Scenarios | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ })
if ($Scenarios.Count -eq 0) { $Scenarios = @("cartel", "charla", "caer-pareja", "historia", "objetos-uso", "edad") }
$Only = @($Only | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ })
if ($Only.Count -gt 0) { $Scenarios = @($Scenarios | Where-Object { $Only -contains $_ }) }
# What each scenario's header note asks for (cartel: the pose counters; charla: NPC; caer-pareja: downed, life and
# animation logs; historia: our story layer under its test9 name; edad: the room's age, renamed switch). The test9
# hook reads LICHII_ORM_STORY_EVENTS; LICHII_ORM_AGE also reaches it as LICHII_COOP_AGE (protocolo.hook_switches).
$Switches = @("LICHII_COOP_DEBUG_POSE=1", "LICHII_COOP_NPC=1", "LICHII_COOP_DOWNED=1", "LICHII_COOP_DEBUG_LIFE=1",
              "LICHII_COOP_DEBUG_ANIM=1", "LICHII_ORM_STORY_EVENTS=1", "LICHII_ORM_AGE=1")
if ($NoCommon) { $Switches = @() }

# 1. The stack, before taking the lock: nothing runs without all of it.
$stackLines = @(& python -I $Proto env 2>&1 | ForEach-Object { "$_" })
if ($LASTEXITCODE -ne 0) {
    @("rapida $Tag NOT RUN $(Get-Date -Format s): stack $Stack not ready") + $stackLines + @("RESULT: NOT RUN") |
        Set-Content -Encoding utf8 $Sum
    exit 1
}

# 2. The game lock (one game run at a time on this PC).
$StopFile = Join-Path $Repo "STOP"
"rapida $Tag waiting for the game lock $Lock since $(Get-Date -Format s)" | Set-Content -Encoding utf8 $Sum
$deadline = (Get-Date).AddMinutes($LockWaitMinutes)
$haveLock = $false
while ((Get-Date) -lt $deadline) {
    if (-not (Test-Path $StopFile)) {
        try { New-Item -ItemType Directory -Path $Lock -ErrorAction Stop | Out-Null; $haveLock = $true; break } catch { }
    }
    Start-Sleep -Seconds 30
}
if (-not $haveLock) {
    @("rapida $Tag NOT RUN $(Get-Date -Format s): no game lock after $LockWaitMinutes min$(if (Test-Path $StopFile) { ' (STOP file)' })",
      "RESULT: NOT RUN") | Set-Content -Encoding utf8 $Sum
    exit 1
}

# 3. The six, through secuencia.ps1 (it rewrites resumen.txt; the lock lines go after it).
$code = 1
try {
    # an empty -Switches value is dropped by -File and the parameter is then missing: pass it only with something
    $swArgs = if ($Switches.Count -gt 0) { @("-Switches", ($Switches -join ",")) } else { @() }
    if ($Join) { $swArgs += @("-Join", $Join) }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "secuencia.ps1") -Tag $Tag `
        -ScenDir (Join-Path $PSScriptRoot "escenarios") -Only ($Scenarios -join ",") -Transport ws -Timeout $Timeout `
        -Players 2 -Stack $Stack @swArgs -HeaderSwitches
    $code = $LASTEXITCODE
} finally {
    Remove-Item -Force -Recurse $Lock -ErrorAction SilentlyContinue
    "game lock given back $(Get-Date -Format s) (secuencia exit $code)" | Add-Content -Encoding utf8 $Sum
}
exit $code
