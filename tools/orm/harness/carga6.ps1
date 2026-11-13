# Six-player load test of the remake co-op on this PC (docs/CARGA_6.md): one hidden game + 5 fake players
# (remake-bot) in one orm_server room by WebSocket, in three places for a few minutes each, then a long soak in the
# Kokiri Forest. Runs one place after the other (one game at a time) through r03_pareja.py --players 1 and writes
# <ORM_REPORTS>\carga6\resumen.txt (one block per run and a table at the end) and muestras.csv (a sample every
# -SampleSeconds: the game's RAM and CPU, the relay's CPU and I/O bytes, the bots' CPU, the PC's CPU, free RAM, GPU).
#
#   powershell -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File tools/orm/harness\carga6.ps1
#   ... -Runs kokiri -Minutes 1       (a short check of one place)
#
# Each run's scenario (written to <InstDir>\escenarios): the world loads (kokiri: the save, through daurosk's
# load.txt; mercado/deku: the test warp before the save loads, as daurosk's -NoLoad scenarios), mark 'start',
# 60 s of Link walking small loops ALONE (the baseline), mark 'bots' (this script then starts the bots around the
# player's own position, read from LICHII_COOP_DEBUG_CAMERA's 'you at' line, on a circle of -Radius metres: every bot
# is that far from the player and keeps walking), 10 s for them to join and spawn, mark 'carga', -Minutes of the same
# loops with the bots, mark 'end'. The bots leave by themselves (-seconds) so they print their round trips.
#
# Defaults: the emulator build in ORM_BUILD_BIN (else <ORM_ROOT>\build\bin; orm_paths.py). The game runs in
# Fritchii's 60 fps mode (the kit's default since 0.6) unless -Fps30. Raw logs: <ORM_LOG_ROOT>\carga6\.
# -Reanalizar: no games, the summary again from those logs and muestras.csv (after a change to the analysis).
# Run 'pareja': TWO hidden games (A and B, both walking the loops, in the Kokiri Forest) + -Bots minus one bots, six
# players again. Run 'larga': the soak, Kokiri, -SoakMinutes.
# Game lock shared with the other agents (-Lock, a folder made with mkdir): taken before the first run, given back in
# a finally; a STOP file in the repo root holds the start and stops before the next run.
param(
    [string]$Runs = "kokiri,mercado,deku,pareja,larga",
    [string]$Lock = "",    # default <ORM_LOG_ROOT>\game.lock
    [int]$Minutes = 5,
    [int]$SoakMinutes = 30,
    [int]$Bots = 5,
    [double]$Radius = 8,
    [int]$Port = 27590,
    [int]$SampleSeconds = 30,
    [switch]$Fps30,
    [string]$BuildBin = "",
    [string]$ServerExe = "",  # default ORM_SERVER_EXE
    [string]$BotExe = "",  # default ORM_BOT_EXE
    [string]$InstDir = "",  # default <ORM_ROOT>\inst-carga
    [string]$LogDir = "",  # default <ORM_LOG_ROOT>\carga6
    [string]$OutDir = "",  # resumen.txt and muestras.csv; default <ORM_REPORTS>\carga6
    [string[]]$HookEnv = @(),   # extra hook switches K=V for the game (docs/RENDIMIENTO.md: LICHII_COOP_PUPPET=0, LICHII_ORM_PERF=1)
    [double]$PoseRate = -1,     # the bots' poses per second (0: no PLAYER_POSE, puppets with no pose copy); -1: the bot's default (30)
    [switch]$Reanalizar  # no games: rewrite resumen.txt from the logs in -LogDir and muestras.csv of an earlier run
)
$ErrorActionPreference = "Stop"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
$Inv = [Globalization.CultureInfo]::InvariantCulture
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
$Out = if ($OutDir) { $OutDir } else { Join-Path $Reports "carga6" }
$R03 = Join-Path $Reports "pareja"
if (-not $Lock) { $Lock = Join-Path $LogRoot "game.lock" }
if (-not $ServerExe) { $ServerExe = if ($env:ORM_SERVER_EXE) { $env:ORM_SERVER_EXE } else { Join-Path $OrmRoot "server\orm_server.exe" } }
if (-not $BotExe) { $BotExe = if ($env:ORM_BOT_EXE) { $env:ORM_BOT_EXE } else { Join-Path $OrmRoot "bot\remake-bot.exe" } }
if (-not $InstDir) { $InstDir = Join-Path $OrmRoot "inst-carga" }
if (-not $LogDir) { $LogDir = Join-Path $LogRoot "carga6" }
New-Item -ItemType Directory -Force $Out, $LogDir, $InstDir | Out-Null
$Summary = Join-Path $Out "resumen.txt"
$Csv = Join-Path $Out "muestras.csv"
$ScenDir = Join-Path $InstDir "escenarios"
New-Item -ItemType Directory -Force $ScenDir | Out-Null
$Cores = [Environment]::ProcessorCount

if (-not $BuildBin) { $BuildBin = if ($env:ORM_BUILD_BIN) { $env:ORM_BUILD_BIN } else { Join-Path $OrmRoot "build\bin" } }
$env:ORM_BUILD_BIN = $BuildBin
$env:ORM_INST_DIR = $InstDir
$env:ORM_SERVER_EXE = $ServerExe
$env:ORM_BOT_EXE = $BotExe
$env:ORM_TEST_FPS30 = $(if ($Fps30) { "1" } else { "0" })
$env:ORM_TEST_HIGH_REFRESH = $(if ($Fps30) { "0" } else { "1" })
$env:ORM_TEST_MIN_FPS = "25"  # the verdict here is ours; the harness only must not stop a slow world load

# place -> scene the bots claim, warp (scene start) or "" for the save's own place, fallback centre if the game
# never logs its position (the StartPos of that start in the romfs Banc, or where the save puts Link)
$Places = @{
    kokiri  = @{ scene = "KokiriForest_Child"; warp = ""; center = "-76,122,38"; name = "Bosque Kokiri (inicio)" }
    mercado = @{ scene = "Market_Child"; warp = "Market_Child Market_Entrance"; center = "-1.7,102.2,65.8"; name = "Ciudadela (mercado, entrada)" }
    deku    = @{ scene = "InsideTheDekuTree"; warp = "InsideTheDekuTree Entrance"; center = "0.2,101.0,23.8"; name = "Arbol Deku (sala de entrada)" }
}

function F([double]$v, [int]$d = 1) {
    if ([double]::IsNaN($v)) { return "-" }
    return [math]::Round($v, $d).ToString($Inv)
}

function Median([double[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return [double]::NaN }
    $s = @($values | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return $s[($n - 1) / 2] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

function Pct([double[]]$values, [double]$p) {
    if (-not $values -or $values.Count -eq 0) { return [double]::NaN }
    $s = @($values | Sort-Object)
    return $s[[math]::Min($s.Count - 1, [math]::Floor($p * $s.Count))]
}

function Min([double[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return [double]::NaN }
    return ($values | Measure-Object -Minimum).Minimum
}

function Max([double[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return [double]::NaN }
    return ($values | Measure-Object -Maximum).Maximum
}

function Read-Shared([string]$path) {
    if (-not (Test-Path $path)) { return "" }
    $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]"ReadWrite, Delete")
    try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
}

function Window([string]$text, [string]$from, [string]$to) {
    $i = $text.IndexOf($from)
    if ($i -lt 0) { return "" }
    $j = if ($to) { $text.IndexOf($to, $i) } else { -1 }
    if ($j -lt 0) { return $text.Substring($i) }
    return $text.Substring($i, $j - $i)
}

function Numbers([string]$text, [string]$pattern) {
    return [double[]]@([regex]::Matches($text, $pattern) | ForEach-Object { [double]::Parse($_.Groups[1].Value, $Inv) })
}

# Link walks a small loop (camera relative): forward, back, right, left, a rest; 480 ticks = 8 s a loop
function Moves([int]$seconds) {
    $loop = @("stick 0 0.6 120", "stick 0 -0.6 120", "stick 0.6 0 90", "stick -0.6 0 90", "wait 60")
    $n = [math]::Ceiling($seconds / 8)
    return @(1..$n | ForEach-Object { $loop }) | ForEach-Object { $_ }
}

function Write-Scenario([string]$run, [string]$warp, [int]$minutes, [int]$players = 1, [int]$bots = $Bots) {
    $l = @("# carga-${run}: written by carga6.ps1 (docs/CARGA_6.md); $players game(s) 60 s, then $bots bots, $minutes min")
    if ($warp) {
        # daurosk's -NoLoad flow (deku-map.A.txt): the warp is asked before the save loads, the title is pressed through
        $l += @("#! noload", "#! start coop: test warp done", "mark boot", "warp $warp", "wait 1200", "mark title",
                "press A", "wait 180", "press A", "wait 180", "press A", "wait 300", "press A", "wait 2400")
    }
    $l += "mark start"
    $l += Moves 60
    $l += @("mark bots", "wait 600", "mark carga")
    $l += Moves ($minutes * 60)
    $l += @("mark end", "wait 120")
    $l | Set-Content -Encoding ascii (Join-Path $ScenDir "carga-$run.A.txt")
    $b = Join-Path $ScenDir "carga-$run.B.txt"
    if ($players -gt 1) { $l | Set-Content -Encoding ascii $b } elseif (Test-Path $b) { Remove-Item $b }
}

$CsvHead = "time,run,phase,eden_ws_mb,eden_priv_mb,eden_cpu_s,srv_ws_mb,srv_cpu_s,srv_io_bytes,bot_cpu_s,pc_cpu_pct,free_ram_mb,gpu_pct,gpu_mem_mb,eden2_priv_mb,eden2_cpu_s"
if (-not $Reanalizar -or -not (Test-Path $Csv)) { $CsvHead | Set-Content -Encoding ascii $Csv }

function Sample([string]$run, [string]$phase, [int]$pyPid) {
    $procs = @(Get-CimInstance Win32_Process -Filter "Name='eden-cli.exe' OR Name='orm_server.exe' OR Name LIKE 'remake-bot%'")
    $games = @($procs | Where-Object { $_.Name -eq "eden-cli.exe" -and $_.ParentProcessId -eq $pyPid })
    $eden = $games | Where-Object { $_.ExecutablePath -like "*\n1\*" } | Select-Object -First 1
    $eden2 = $games | Where-Object { $_.ExecutablePath -like "*\n2\*" } | Select-Object -First 1
    $srv = $procs | Where-Object { $_.Name -eq "orm_server.exe" -and $_.ParentProcessId -eq $pyPid } | Select-Object -First 1
    $bot = $procs | Where-Object { $_.Name -like "remake-bot*" -and $_.ParentProcessId -eq $PID } | Select-Object -First 1
    $cpu = { param($p) if ($p) { ($p.KernelModeTime + $p.UserModeTime) / 1e7 } else { [double]::NaN } }
    $pc = (Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'").PercentProcessorTime
    $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1024
    $gpu = @("-", "-")
    try {
        $g = (& nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>$null | Select-Object -First 1)
        if ($g) { $gpu = @($g -split ",\s*") }
    } catch { }
    $row = @((Get-Date -Format s), $run, $phase,
        $(if ($eden) { F ($eden.WorkingSetSize / 1MB) 0 } else { "-" }),
        $(if ($eden) { F ($eden.PrivatePageCount / 1MB) 0 } else { "-" }),
        (F (& $cpu $eden) 1),
        $(if ($srv) { F ($srv.WorkingSetSize / 1MB) 1 } else { "-" }),
        (F (& $cpu $srv) 2),
        $(if ($srv) { [string]($srv.ReadTransferCount + $srv.WriteTransferCount + $srv.OtherTransferCount) } else { "-" }),
        (F (& $cpu $bot) 2), [string]$pc, (F $free 0), $gpu[0], $gpu[1],
        $(if ($eden2) { F ($eden2.PrivatePageCount / 1MB) 0 } else { "-" }), (F (& $cpu $eden2) 1))
    ($row -join ",") | Add-Content -Encoding ascii $Csv
}

$PyExe = (Get-Command python).Source  # not $Py: PowerShell names ignore case and $py is the harness process
# -ping-ms came after the protocol 108 bot was built (an older bot build has none): pass it only if it is there
$BotPing = $false
if (Test-Path $BotExe) {
    $help = & cmd /c "`"$BotExe`" -h 2>&1"
    $BotPing = [bool]($help | Select-String -SimpleMatch "-ping-ms")
}
$table = @()
$head = @("carga6 $(Get-Date -Format s): $Bots bots + 1 game, radius $Radius m, $Minutes min per place, soak $SoakMinutes min",
          "emulator $BuildBin; server $ServerExe; bot $BotExe; instances $InstDir",
          ("game: hidden window, half resolution, " + $(if ($Fps30) { "30 fps mode" } else { "60 fps mode (oot_high_refresh)" }) +
           ", transport ws; bots: samples 20/s, poses 30/s, " + $(if ($BotPing) { "ping every 1000 ms" } else { "no ping of their own (bot without -ping-ms)" }) +
           "; PC: $Cores logical CPUs"))
$head | Set-Content -Encoding utf8 $Summary

$StopFile = Join-Path $Repo "STOP"
# r03_pareja wants 14 GB free (two games: 2 x 14 GB of RAM is too much, but it checks before the first load only);
# the lock is taken only with this much free, so it is not held while another agent's build fills the RAM
function FreeGB { (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB }
$NeedGB = 15
$haveLock = $false
if (-not $Reanalizar -and $Lock) {
    while ($true) {
        if (-not (Test-Path $StopFile) -and (FreeGB) -ge $NeedGB) {
            try { New-Item -ItemType Directory -Path $Lock -ErrorAction Stop | Out-Null; $haveLock = $true; break } catch { }
        }
        Start-Sleep -Seconds 60
    }
    "game lock $Lock taken $(Get-Date -Format s)" | Add-Content -Encoding utf8 $Summary
}
try {
foreach ($run in ($Runs -split ",")) {
    $run = $run.Trim()
    if (-not $Reanalizar -and (Test-Path $StopFile)) { "-- STOP file: stopped before $run" | Add-Content -Encoding utf8 $Summary; break }
    $place = if ($run -eq "larga" -or $run -eq "pareja") { "kokiri" } else { $run }
    $P = $Places[$place]
    if (-not $P) { "-- ${run}: unknown place" | Add-Content -Encoding utf8 $Summary; continue }
    $minutes = if ($run -eq "larga") { $SoakMinutes } else { $Minutes }
    $players = if ($run -eq "pareja") { 2 } else { 1 }
    if (-not $Reanalizar) {
        $w0 = Get-Date  # up to 20 min for the RAM to come back (another agent's build); r03 then judges
        while ((FreeGB) -lt $NeedGB -and ((Get-Date) - $w0).TotalMinutes -lt 20) { Start-Sleep -Seconds 30 }
        "-- $run starts with $(F (FreeGB) 1) GB RAM free (waited $(F ((Get-Date) - $w0).TotalMinutes 1) min)" | Add-Content -Encoding utf8 $Summary
    }
    $nBots = $Bots - ($players - 1)
    $nPlayers = $players + $nBots
    $scen = "carga-$run"
    $edenLog = Join-Path $InstDir "n1\user\log\eden_log.txt"
    $edenLogB = Join-Path $InstDir "n2\user\log\eden_log.txt"
    $botLog = Join-Path $LogDir "${run}_bots.log"
    $centerFile = Join-Path $LogDir "${run}_centro.txt"
    $center = $P.center; $centerFrom = "fallback"; $dur = [double]::NaN
    if ($Reanalizar) {
        if (Test-Path $centerFile) { $center, $centerFrom, $dur = (Get-Content $centerFile -Raw).Trim() -split ";"; $dur = [double]::Parse($dur, $Inv) }
    } else {
    Write-Scenario $run $P.warp $minutes $players $nBots
    $timeout = 60 + 10 + $minutes * 60 + 400
    $pyOut = Join-Path $LogDir "${run}_harness.txt"
    $pyArgs = @("-I", (Join-Path $PSScriptRoot "r03_pareja.py"), "--scenario", $scen, "--scen-dir", $ScenDir,
              "--players", "$players", "--transport", "ws", "--port", "$Port", "--timeout", "$timeout",
              "--server-exe", $ServerExe, "--server-arg=--log-stats-every-ms", "--server-arg=$($SampleSeconds * 1000)",
              "--env", "LICHII_COOP_DEBUG_CAMERA=1")
    foreach ($kv in @($HookEnv | ForEach-Object { $_ -split ',' } | Where-Object { $_ })) { $pyArgs += @("--env", $kv) }
    $t0 = Get-Date
    $py = Start-Process -FilePath $PyExe -ArgumentList $pyArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $pyOut -RedirectStandardError "$pyOut.err"
    $null = $py.Handle
    $phase = "boot"; $bot = $null
    $lastSample = [datetime]::MinValue
    while (-not $py.HasExited) {
        if ($phase -eq "boot" -or $phase -eq "solo") {
            $text = Read-Shared $edenLog
            if ($phase -eq "boot" -and $text.Contains("coop: test mark start")) { $phase = "solo" }
            if ($phase -eq "solo" -and $text.Contains("coop: test mark bots")) {
                $solo = Window $text "coop: test mark start" "coop: test mark bots"
                $you = [regex]::Matches($solo, "camera debug: you at \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\)")
                if ($you.Count -gt 0) {
                    $m = $you[$you.Count - 1]
                    $center = "$($m.Groups[1].Value),$($m.Groups[2].Value),$($m.Groups[3].Value)"; $centerFrom = "you at"
                }
                $botArgs = @("-url", "ws://127.0.0.1:$($Port + 1)/", "-name", "bot", "-count", "$nBots",
                             "-seconds", "$(10 + $minutes * 60 + 15)", "-scene", $P.scene, "-center", $center,
                             "-radius", (F $Radius 1))
                if ($BotPing) { $botArgs += @("-ping-ms", "1000") }
                if ($PoseRate -ge 0) { $botArgs += @("-pose-rate", $PoseRate.ToString($Inv)) }
                $bot = Start-Process -FilePath $BotExe -ArgumentList $botArgs -PassThru -WindowStyle Hidden `
                    -RedirectStandardOutput $botLog -RedirectStandardError "$botLog.err"
                $null = $bot.Handle
                $phase = "carga"
            }
        } elseif ($phase -eq "carga" -and $bot.HasExited) {
            $phase = "after"
        }
        if (((Get-Date) - $lastSample).TotalSeconds -ge $SampleSeconds) {
            $lastSample = Get-Date
            try { Sample $run $phase $py.Id } catch { "$(Get-Date -Format s),$run,$phase,sample error $($_.Exception.Message -replace ',', ';')" | Add-Content -Encoding ascii $Csv }
        }
        Start-Sleep -Seconds 2
    }
    if ($bot -and -not $bot.HasExited) { Stop-Process -Id $bot.Id -Force -ErrorAction SilentlyContinue }
    $dur = ((Get-Date) - $t0).TotalMinutes

    # keep the raw logs out of the repo; R0.3 stays as it was
    $edenCopy = Join-Path $LogDir "${run}_eden_log.txt"
    if (Test-Path $edenLog) { Copy-Item -Force $edenLog $edenCopy }
    if ($players -gt 1 -and (Test-Path $edenLogB)) { Copy-Item -Force $edenLogB (Join-Path $LogDir "${run}_B_eden_log.txt") }
    foreach ($f in @(Get-ChildItem $R03 -Filter "$scen*" -ErrorAction SilentlyContinue)) {
        Move-Item -Force $f.FullName (Join-Path $LogDir ($f.Name -replace "^$scen", $run))
    }
    "$center;$centerFrom;$(F $dur 2)" | Set-Content -Encoding ascii $centerFile
    }  # end of the run (skipped with -Reanalizar)
    $edenCopy = Join-Path $LogDir "${run}_eden_log.txt"
    $harness = Read-Shared (Join-Path $LogDir "${run}_resumen.txt")
    $srvText = Read-Shared (Join-Path $LogDir "${run}_server.log")
    $botText = Read-Shared $botLog
    $text = Read-Shared $edenCopy
    $result = ([regex]::Match($harness, 'RESULT: [^\r\n]*')).Value
    if (-not $result) { $result = "RESULT: ? (no harness summary)" }

    $warpText = if ($P.warp) { ", warp $($P.warp)" } else { "" }
    $lines = @("", "== ${run}: $($P.name), scene $($P.scene)$warpText, $players game(s) + $nBots bots, $minutes min with bots, $(F $dur 1) min in all",
               "harness $result")
    $lines += @($harness -split "`r?`n" | Where-Object { $_ -match "FAIL|NEVER|FROZEN|SLOW|WARP|ready:" } | ForEach-Object { "   harness: $_" })
    $warped = $text.Contains("coop: test warp done")
    if ($P.warp) { $lines += "warp: " + $(if ($warped) { "done" } else { "NOT done (asked $(([regex]::Matches($text, 'coop: test warp to ')).Count) times)" }) }
    $lines += "bots: centre ($center) from $centerFrom, radius $Radius m, scene $($P.scene)"

    # fps: alone (start..bots) and with the bots (carga..end)
    $solo = Window $text "coop: test mark start" "coop: test mark bots"
    $load = Window $text "coop: test mark carga" "coop: test mark end"
    $fSolo = Numbers $solo "FPS: ([0-9.]+) speed"
    $fLoad = Numbers $load "FPS: ([0-9.]+) speed"
    $lines += "fps alone: median $(F (Median $fSolo)) (n=$($fSolo.Count)); with bots: median $(F (Median $fLoad)), " +
              "min $(F (Min $fLoad)), 5th percentile $(F (Pct $fLoad 0.05)) (n=$($fLoad.Count))"
    $low = @($fLoad | Where-Object { $_ -lt 45 }).Count
    $lines += "fps samples under 45 with bots: $low of $($fLoad.Count)"

    # bots seen, puppets, distances (LICHII_COOP_DEBUG_CAMERA: 'you at' and 'P<n> at' every 120 frames)
    $joined = @([regex]::Matches($text, "coop: player (\d+) 'bot\d+' joined") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $spawnLines = [regex]::Matches($text, "coop: puppet: P(\d+) CoopPuppet_\w+ spawned")
    $spawned = @($spawnLines | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $ret0 = ([regex]::Matches($text, "requested \(returned 0\)")).Count
    $lines += "puppets: bots joined as players [$($joined -join ' ')], puppets spawned for [$($spawned -join ' ')], " +
              "$($spawnLines.Count) spawn lines, $ret0 creates refused (returned 0)"
    $dist = @(); $drawn = 0; $notDrawn = 0; $me = $null
    foreach ($m in [regex]::Matches($load, "camera debug: (you|P\d+) at \(([-0-9.]+), ([-0-9.]+), ([-0-9.]+)\)( [^\r\n]*)?")) {
        $v = @(2, 3, 4 | ForEach-Object { [double]::Parse($m.Groups[$_].Value, $Inv) })
        if ($m.Groups[1].Value -eq "you") { $me = $v; continue }
        if ($me) { $dist += [math]::Sqrt([math]::Pow($v[0] - $me[0], 2) + [math]::Pow($v[1] - $me[1], 2) + [math]::Pow($v[2] - $me[2], 2)) }
        if ($m.Groups[5].Value -match "^ at \d") { $drawn++ } else { $notDrawn++ }
    }
    $dd = [double[]]$dist
    $lines += "distance other-player (A's view): median $(F (Median $dd)) m, min $(F (Min $dd)) m, " +
              "max $(F (Max $dd)) m (n=$($dd.Count)); name tags drawn $drawn, not drawn/skipped $notDrawn"
    $fB = [double[]]@()
    if ($players -gt 1) {
        $textB = Read-Shared (Join-Path $LogDir "${run}_B_eden_log.txt")
        $fB = Numbers (Window $textB "coop: test mark carga" "coop: test mark end") "FPS: ([0-9.]+) speed"
        $fBs = Numbers (Window $textB "coop: test mark start" "coop: test mark bots") "FPS: ([0-9.]+) speed"
        $rB = Numbers (Window $textB "coop: test mark carga" "coop: test mark end") "coop: rtt median ([0-9.]+) ms"
        $spB = @([regex]::Matches($textB, "coop: puppet: P(\d+) CoopPuppet_\w+ spawned") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
        $lines += "B: fps alone (with A) median $(F (Median $fBs)) (n=$($fBs.Count)); with bots median $(F (Median $fB)), min $(F (Min $fB)), " +
                  "5th percentile $(F (Pct $fB 0.05)) (n=$($fB.Count)); rtt median $(F (Median $rB)) ms; puppets spawned for [$($spB -join ' ')]; " +
                  "Critical $(([regex]::Matches($textB, '<Critical>')).Count), Assertion $(([regex]::Matches($textB, 'Assertion')).Count)"
    }

    # round trips: the game's 'coop: rtt median' lines (every 10 s) and the bots' own at the end
    $rMed = Numbers $load "coop: rtt median ([0-9.]+) ms"
    $rMax = Numbers $load "coop: rtt median [0-9.]+ ms, min [0-9.]+ ms, max ([0-9.]+) ms"
    $lines += "game rtt (with bots): median of the 10 s medians $(F (Median $rMed)) ms, worst 10 s median $(F (Max $rMed)) ms, " +
              "max $(F (Max $rMax)) ms (n=$($rMed.Count))"
    $bMed = Numbers $botText "rtt median ([0-9.]+) ms"
    $lines += "bots rtt: medians $(($bMed | ForEach-Object { F $_ }) -join ' ') ms; bot errors: " +
              ([regex]::Matches($botText, "(?im)^.*(refused|error|failed|gave up|closed).*$")).Count

    # relay counters (--log-stats-every-ms): the lines with every player in
    $st = @([regex]::Matches($srvText, "players (\d+), relayed (\d+), dropped (\d+), ws replaced (\d+), ws flooded (\d+)") |
            Where-Object { [int]$_.Groups[1].Value -eq $nPlayers })
    if ($st.Count -ge 2) {
        $a = $st[0]; $b = $st[$st.Count - 1]; $secs = ($st.Count - 1) * $SampleSeconds
        $rel = ([double]$b.Groups[2].Value - [double]$a.Groups[2].Value) / $secs
        $lines += "relay with $nPlayers players: $(F $rel 0) relayed/s over $secs s; at the end relayed $($b.Groups[2].Value), " +
                  "dropped $($b.Groups[3].Value), ws replaced $($b.Groups[4].Value), ws flooded $($b.Groups[5].Value)"
    } else {
        $rel = [double]::NaN
        $lines += "relay: fewer than two stats lines with $nPlayers players ($(([regex]::Matches($srvText, 'orm_server: stats')).Count) stats lines)"
    }
    $lines += @($srvText -split "`r?`n" | Where-Object { $_ -notmatch "orm_server: stats;" -and $_ -match "refus|flood|error|full" } | Select-Object -First 5 | ForEach-Object { "   server: $_" })

    # samples of this run (muestras.csv)
    $rows = @(Import-Csv $Csv | Where-Object { $_.run -eq $run -and $_.phase -eq "carga" -and $_.eden_priv_mb -ne "-" })
    $soloRows = @(Import-Csv $Csv | Where-Object { $_.run -eq $run -and $_.phase -eq "solo" -and $_.eden_priv_mb -ne "-" })
    $ram = "-"; $grow = [double]::NaN; $gameCpu = [double]::NaN; $srvCpu = [double]::NaN; $botCpu = [double]::NaN
    $io = [double]::NaN; $pcCpu = [double]::NaN; $gpuPct = [double]::NaN; $privMax = [double]::NaN
    if ($rows.Count -ge 2) {
        $r0 = $rows[0]; $r1 = $rows[$rows.Count - 1]
        $dt = ([datetime]$r1.time - [datetime]$r0.time).TotalSeconds
        $priv = [double[]]@($rows | ForEach-Object { [double]$_.eden_priv_mb })
        $privMax = ($priv | Measure-Object -Maximum).Maximum
        $grow = ($priv[$priv.Count - 1] - $priv[0]) / ($dt / 60)
        $ram = "private $(F $priv[0] 0) -> max $(F $privMax 0) -> end $(F $priv[$priv.Count - 1] 0) MB ($(F $grow 1) MB/min), " +
               "working set end $($r1.eden_ws_mb) MB" + $(if ($soloRows.Count) { "; alone $($soloRows[$soloRows.Count - 1].eden_priv_mb) MB" } else { "" })
        $gameCpu = ([double]$r1.eden_cpu_s - [double]$r0.eden_cpu_s) / $dt * 100
        if ($players -gt 1 -and $r1.eden2_priv_mb -and $r1.eden2_priv_mb -ne "-" -and $r0.eden2_cpu_s -ne "-") {
            $privB = [double[]]@($rows | Where-Object { $_.eden2_priv_mb -ne "-" } | ForEach-Object { [double]$_.eden2_priv_mb })
            $lines += "B eden-cli: private $(F $privB[0] 0) -> max $(F (Max $privB) 0) -> end $(F $privB[$privB.Count - 1] 0) MB; " +
                      "CPU $(F (([double]$r1.eden2_cpu_s - [double]$r0.eden2_cpu_s) / $dt * 100) 0) % of one core"
        }
        if ($r1.srv_cpu_s -ne "-" -and $r0.srv_cpu_s -ne "-") { $srvCpu = ([double]$r1.srv_cpu_s - [double]$r0.srv_cpu_s) / $dt * 100 }
        if ($r1.bot_cpu_s -ne "-" -and $r0.bot_cpu_s -ne "-") { $botCpu = ([double]$r1.bot_cpu_s - [double]$r0.bot_cpu_s) / $dt * 100 }
        if ($r1.srv_io_bytes -ne "-" -and $r0.srv_io_bytes -ne "-") { $io = ([double]$r1.srv_io_bytes - [double]$r0.srv_io_bytes) / $dt / 1024 }
        $pcCpu = Median ([double[]]@($rows | ForEach-Object { [double]$_.pc_cpu_pct }))
        $gpuPct = Median ([double[]]@($rows | Where-Object { $_.gpu_pct -ne "-" } | ForEach-Object { [double]$_.gpu_pct }))
        $lines += "RAM eden-cli (with bots, $($rows.Count) samples over $(F ($dt / 60) 1) min): $ram"
        $lines += "free RAM min $(F (($rows | ForEach-Object { [double]$_.free_ram_mb } | Measure-Object -Minimum).Minimum) 0) MB; " +
                  "GPU median $(F $gpuPct 0) %, GPU memory max $(F (($rows | Where-Object { $_.gpu_mem_mb -ne '-' } | ForEach-Object { [double]$_.gpu_mem_mb } | Measure-Object -Maximum).Maximum) 0) MB"
        $lines += "CPU (% of one core): game $(F $gameCpu 0), relay $(F $srvCpu 1), $nBots bots $(F $botCpu 1); PC median $(F $pcCpu 0) % of $Cores CPUs"
        $lines += "relay I/O (read+write+other bytes, in and out together): $(F $io 1) KB/s, $(F ($io / $nPlayers) 1) KB/s per player"
    } else {
        $lines += "samples: fewer than two with the bots in ($($rows.Count))"
    }

    # what went wrong in the game's log
    foreach ($k in @(@("<Critical>", "Critical"), @("Assertion", "Assertion"), @("refused", "refused"))) {
        $hits = @($text -split "`r?`n" | Where-Object { $_.Contains($k[0]) })
        $kinds = @($hits | ForEach-Object { ($_ -split "> ", 2)[-1] -replace "0x[0-9a-fA-F]+|[0-9.]+", "#" } | Select-Object -Unique)
        $lines += "$($k[1]) lines: $($hits.Count) ($($kinds.Count) kinds)"
        $lines += @($kinds | Select-Object -First 4 | ForEach-Object { "   " + $_.Substring(0, [math]::Min(180, $_.Length)) })
    }
    $lines += "logs: $LogDir\${run}_*"
    $lines | Add-Content -Encoding utf8 $Summary
    $table += "{0,-8} {1,-7} {2,-6} {3,-14} {4,-18} {5,-9} {6,-6} {7,-6} {8,-6} {9,-9} {10,-10} {11,-9} {12}" -f $run,
        (F (Median $fSolo) 0), ((F (Median $fLoad) 0) + $(if ($players -gt 1) { "/B" + (F (Median $fB) 0) } else { "" })),
        "$(F (Min $fLoad) 0)/$(F (Pct $fLoad 0.05) 0)",
        "$(F $privMax 0) ($(F $grow 1)/min)", (F $gameCpu 0), (F $pcCpu 0), (F $gpuPct 0), (F $rel 0), (F ($io / $nPlayers) 1),
        "$(F (Median $rMed) 1)/$(F (Max $rMax) 0)", "$($spawned.Count)/$($nPlayers - 1)", ($result -replace "RESULT: ", "")
}
} finally {
    if ($haveLock) {
        Remove-Item -Force -Recurse $Lock -ErrorAction SilentlyContinue
        "game lock given back $(Get-Date -Format s)" | Add-Content -Encoding utf8 $Summary
    }
}

$tab = @("", "== table", ("{0,-8} {1,-7} {2,-6} {3,-14} {4,-18} {5,-9} {6,-6} {7,-6} {8,-6} {9,-9} {10,-10} {11,-9} {12}" -f
    "run", "fps1", "fps6", "min/p5", "RAM MB (growth)", "game%cpu", "PC%", "GPU%", "rel/s", "KB/s/pl", "rtt med/max", "puppets", "harness")) + $table
$tab += "done $(Get-Date -Format s)"
$tab | Add-Content -Encoding utf8 $Summary
