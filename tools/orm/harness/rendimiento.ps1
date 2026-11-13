# Where the fps go with six players (docs/RENDIMIENTO.md): the same short load run (one hidden game, 60 s alone, then
# 5 bots for -Minutes) in a few variants that tell the hook's cost from the game's own, one after the other through
# carga6.ps1 (which takes the game lock <ORM_LOG_ROOT>\game.lock for each run, waits for 15 GB of free RAM and stops on a
# STOP file in the repo root). One summary: <ORM_REPORTS>\rendimiento\resumen.txt (a line per variant, DONE).
#
#   powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File tools/orm/harness\rendimiento.ps1
#   ... -Only normal,sin-pose        (a subset)      ... -Place mercado      (kokiri, mercado or deku)
#
# Variants (alone -> with 5 bots, in the same run and place, so each one carries its own baseline):
#   normal         5 puppets 8 m away with 30 poses/s each: what a game of six costs
#   sin-pose       the bots send no PLAYER_POSE: the puppets are there and play their own animation, nothing is
#                  written to their bones -> normal minus this = the pose copy (ours)
#   sin-marioneta  LICHII_COOP_PUPPET=0: the five players are in the room and in the HUD, no actor is made
#                  -> this minus alone = network, presence, markers (ours); normal minus this = five more Links
#   lejos          the bots walk 150 m away: the puppets exist but are out of sight -> is it drawing or animating?
# Every run carries LICHII_ORM_PERF=1: an emulator built from orm/rendimiento or later logs "coop: perf:" lines (the
# hook's own milliseconds per frame, by part); the last one of each run is copied to the summary. Older binaries
# ignore the switch and the summary says "no perf lines".
param(
    [string]$Stack = "test9",
    [string[]]$Only = @(),
    [string]$Place = "kokiri",
    [int]$Minutes = 1,
    [string]$BuildBin = "",   # default: the stack's (protocolo.py)
    [string]$OutDir = ""
)
$ErrorActionPreference = "Continue"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
$Out = if ($OutDir) { $OutDir } else { Join-Path $Reports "rendimiento" }
New-Item -ItemType Directory -Force $Out | Out-Null
$Sum = Join-Path $Out "resumen.txt"
$LogRoot = Join-Path $LogRoot "rendimiento"

# the stack's emulator, server, bot and instance folder (protocolo.py env prints NAME=value lines)
$paths = @{}
$env:ORM_STACK = $Stack
foreach ($line in @(& python -I (Join-Path $PSScriptRoot "protocolo.py") env --stack $Stack 2>&1 | ForEach-Object { "$_" })) {
    if ($line -match '^(?:set )?(ORM_[A-Z_]+)=(.*)$') { $paths[$Matches[1]] = $Matches[2] }
}
if ($BuildBin) { $paths["ORM_BUILD_BIN"] = $BuildBin }
"rendimiento $(Get-Date -Format s): stack $Stack, place $Place, $Minutes min with 5 bots per variant" | Set-Content -Encoding utf8 $Sum
"emulator $($paths['ORM_BUILD_BIN']); server $($paths['ORM_SERVER_EXE']); bot $($paths['ORM_BOT_EXE']); instances $($paths['ORM_INST_DIR'])" | Add-Content -Encoding utf8 $Sum
$missing = @("ORM_BUILD_BIN", "ORM_SERVER_EXE", "ORM_BOT_EXE", "ORM_INST_DIR" | Where-Object { -not $paths[$_] -or -not (Test-Path $paths[$_]) })
if ($missing.Count -gt 0) {
    "FAIL: missing $($missing -join ', ')" | Add-Content -Encoding utf8 $Sum
    "DONE $(Get-Date -Format s)" | Add-Content -Encoding utf8 $Sum
    exit 2
}

$Variants = @(
    @{ name = "normal";        env = @();                        pose = -1; radius = 8 },
    @{ name = "sin-pose";      env = @();                        pose = 0;  radius = 8 },
    @{ name = "sin-marioneta"; env = @("LICHII_COOP_PUPPET=0"); pose = -1; radius = 8 },
    @{ name = "lejos";         env = @();                        pose = -1; radius = 150 }
)
$Only = @($Only | ForEach-Object { $_ -split '[,; ]+' } | Where-Object { $_ })
if ($Only.Count -gt 0) { $Variants = @($Variants | Where-Object { $Only -contains $_.name }) }

"{0,-14} {1,-9} {2,-9} {3,-9} {4,-9} {5,-9} {6}" -f "variant", "fps alone", "fps bots", "min/p5", "game%cpu", "puppets", "harness" | Add-Content -Encoding utf8 $Sum
$perf = @()
foreach ($v in $Variants) {
    if (Test-Path (Join-Path $Repo "STOP")) { "-- STOP file: stopped before $($v.name)" | Add-Content -Encoding utf8 $Sum; break }
    $vOut = Join-Path $Out $v.name
    $vLog = Join-Path $LogRoot $v.name
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "carga6.ps1"),
              "-Runs", $Place, "-Minutes", "$Minutes", "-Radius", "$($v.radius)", "-SampleSeconds", "10",
              "-BuildBin", $paths["ORM_BUILD_BIN"], "-ServerExe", $paths["ORM_SERVER_EXE"], "-BotExe", $paths["ORM_BOT_EXE"],
              "-InstDir", $paths["ORM_INST_DIR"], "-LogDir", $vLog, "-OutDir", $vOut,
              "-HookEnv", (@("LICHII_ORM_PERF=1") + $v.env -join ","))
    if ($v.pose -ge 0) { $argv += @("-PoseRate", "$($v.pose)") }
    & powershell @argv *> (Join-Path $Out "$($v.name)_salida.txt")
    $code = $LASTEXITCODE
    $salida = Join-Path $Out "$($v.name)_salida.txt"  # carga6 prints nothing unless it breaks
    if ((Test-Path $salida) -and (Get-Item $salida).Length -eq 0) { Remove-Item $salida }
    $res = Join-Path $vOut "resumen.txt"
    $row = $null
    if (Test-Path $res) {
        # carga6's table line of this run: run fps1 fps6 min/p5 "RAM (growth)" game%cpu PC% GPU% rel/s KB/s/pl rtt puppets harness
        $line = @(Get-Content $res | Where-Object { $_ -match "^$Place\s" } | Select-Object -Last 1)
        if ($line.Count -gt 0) {
            $t = @($line[0] -split '\s+' | Where-Object { $_ })
            # the RAM column is two tokens ("12345 (1.2/min)")
            $row = "{0,-14} {1,-9} {2,-9} {3,-9} {4,-9} {5,-9} {6}" -f $v.name, $t[1], $t[2], $t[3], $t[6], $t[12], (($t[13..($t.Count - 1)]) -join " ")
        }
    }
    if (-not $row) { $row = "{0,-14} no result (carga6 exit $code; see $($v.name)_salida.txt and $res)" -f $v.name }
    $row | Add-Content -Encoding utf8 $Sum
    $eden = Join-Path $vLog "${Place}_eden_log.txt"
    $last = $null
    if (Test-Path $eden) {
        $text = [IO.File]::ReadAllText($eden)
        $i = $text.IndexOf("coop: test mark carga"); $j = $text.IndexOf("coop: test mark end")
        if ($i -ge 0) {
            $load = if ($j -gt $i) { $text.Substring($i, $j - $i) } else { $text.Substring($i) }
            $m = [regex]::Matches($load, "coop: perf: (\d[^\r\n]*)")
            if ($m.Count -gt 0) { $last = $m[$m.Count - 1].Groups[1].Value }
        }
    }
    $perf += "$($v.name): " + $(if ($last) { $last } else { "no perf lines (an emulator without LICHII_ORM_PERF)" })
}
"" | Add-Content -Encoding utf8 $Sum
"== the hook's own time with the bots in (coop: perf, last 5 s window of each run)" | Add-Content -Encoding utf8 $Sum
$perf | Add-Content -Encoding utf8 $Sum
"DONE $(Get-Date -Format s)" | Add-Content -Encoding utf8 $Sum
