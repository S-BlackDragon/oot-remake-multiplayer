# Round-trip measurement of the remake co-op (docs/LATENCIA.md). Two modes, both on 127.0.0.1 unless -Url says
# otherwise, both leaving <ORM_REPORTS>\latencia\<Etiqueta>_<Modo>.txt and one line in reports\latencia\resumen.txt:
#
#   bots    No games. Starts orm_server (-ServerExe) on a free local port and joins -Bots fake players
#           (remake-bot -ping-ms) that send samples and poses at the game's rates and PING every 100 ms; reports each
#           bot's "rtt median" and the server's CPU over the run. With -Url (ws:// or wss://, room path included) the
#           bots go to that server instead and no server is started (a room of its own: the bots show as puppets).
#   pareja  The two-game "walk" pair by WebSocket through orm_server (r03_pareja.py --transport ws); reports the
#           "coop: rtt median" lines of both games' logs. Needs the instances free (r03_pareja checks it).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/orm/harness\latencia.ps1 -Modo bots -Etiqueta antes -ServerExe <old-build>\orm_server.exe
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/orm/harness\latencia.ps1 -Modo bots -Etiqueta despues -ServerExe <new-build>\orm_server.exe
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/orm/harness\latencia.ps1 -Modo pareja -Etiqueta antes -ServerExe <old-build>\orm_server.exe -BuildBin <emulator bin with the test8 hook> -InstDir <instance folder>
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/orm/harness\latencia.ps1 -Modo pareja -Etiqueta despues -ServerExe <new-build>\orm_server.exe -BuildBin <emulator bin with the latency hook> -InstDir <instance folder>
#
# -BuildBin / -InstDir go to the harness as ORM_BUILD_BIN / ORM_INST_DIR (orm_inst.py); left out, the harness defaults.
param(
    [ValidateSet("bots", "pareja")][string]$Modo = "bots",
    [string]$Etiqueta = "medida",
    [string]$ServerExe = "",  # default ORM_SERVER_EXE
    [string]$BotExe = "",
    [string]$Url = "",
    [int]$Bots = 6,
    [int]$Seconds = 20,
    [int]$PingMs = 100,
    [int]$Port = 27580,
    [string]$Scenario = "walk",
    [string]$ScenDir = "",
    [string]$BuildBin = "",
    [string]$InstDir = "",
    [int]$Timeout = 300
)
$ErrorActionPreference = "Stop"
[System.Diagnostics.Process]::GetCurrentProcess().PriorityClass = "BelowNormal"
$Repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$OrmRoot = if ($env:ORM_ROOT) { $env:ORM_ROOT } else { Join-Path $HOME "orm-work" }  # see orm_paths.py
$Reports = if ($env:ORM_REPORTS) { $env:ORM_REPORTS } else { Join-Path $OrmRoot "reports" }
$LogRoot = if ($env:ORM_LOG_ROOT) { $env:ORM_LOG_ROOT } else { Join-Path $OrmRoot "logs" }
if (-not $ServerExe) { $ServerExe = if ($env:ORM_SERVER_EXE) { $env:ORM_SERVER_EXE } else { Join-Path $OrmRoot "server\orm_server.exe" } }
$Out = Join-Path $Reports "latencia"
New-Item -ItemType Directory -Force $Out | Out-Null
$Report = Join-Path $Out "${Etiqueta}_${Modo}.txt"
$Summary = Join-Path $Out "resumen.txt"
$lines = @("latencia $Modo $Etiqueta $(Get-Date -Format s) server $ServerExe")

function Median([double[]]$values) {
    if (-not $values -or $values.Count -eq 0) { return [double]::NaN }
    $s = @($values | Sort-Object)
    $n = $s.Count
    if ($n % 2 -eq 1) { return $s[($n - 1) / 2] }
    return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

function Finish([string]$result, [string]$oneLine) {
    $script:lines += "RESULT: $result"
    $script:lines | Set-Content -Encoding utf8 $Report
    "$(Get-Date -Format s) $Etiqueta $Modo $result $oneLine" | Add-Content -Encoding utf8 $Summary
    $script:lines | Select-Object -Last 12
}

if ($Modo -eq "bots") {
    if (-not $BotExe) {
        $BotExe = Join-Path $OrmRoot "bot\remake-bot.exe"
        if ($env:ORM_BOT_EXE) { $BotExe = $env:ORM_BOT_EXE }
    }
    if (-not (Test-Path $BotExe)) { $lines += "no bot at $BotExe (set ORM_BOT_EXE or put the bot in <ORM_ROOT>\bot)"; Finish "FAIL" ""; exit 1 }
    $server = $null
    $srvOut = Join-Path $Out "${Etiqueta}_server.log"
    $target = $Url
    $t0 = Get-Date
    if (-not $target) {
        if (-not (Test-Path $ServerExe)) { $lines += "no server at $ServerExe"; Finish "FAIL" ""; exit 1 }
        $runMs = ($Seconds + 8) * 1000
        $server = Start-Process -FilePath $ServerExe -PassThru -WindowStyle Hidden -RedirectStandardOutput $srvOut `
            -RedirectStandardError "$srvOut.err" -ArgumentList @("--bind", "127.0.0.1", "--udp-port", "off",
            "--ws-port", "$Port", "--run-for-ms", "$runMs")
        $null = $server.Handle  # keeps the process handle, so its CPU time can be read after it exits
        $deadline = (Get-Date).AddSeconds(10)
        while (-not ((Test-Path $srvOut) -and (Select-String -Path $srvOut -Pattern "listening" -Quiet))) {
            if ($server.HasExited -or (Get-Date) -gt $deadline) { $lines += "orm_server did not listen on $Port"; Finish "FAIL" ""; exit 1 }
            Start-Sleep -Milliseconds 100
        }
        $target = "ws://127.0.0.1:$Port/"
    }
    $botOut = Join-Path $Out "${Etiqueta}_bots.log"
    # The game's rates: a sample per frame change at 30 fps and 30 poses a second (coop/host sampler).
    $bot = Start-Process -FilePath $BotExe -PassThru -WindowStyle Hidden -RedirectStandardOutput $botOut `
        -RedirectStandardError "$botOut.err" -ArgumentList @("-url", $target, "-count", "$Bots", "-seconds", "$Seconds",
        "-rate", "30", "-pose-rate", "30", "-ping-ms", "$PingMs", "-name", "lat")
    $null = $bot.Handle  # keeps the handle, so ExitCode can be read
    $bot.WaitForExit()
    $lines += "bots: $Bots at $target for $Seconds s, PING every $PingMs ms (exit $($bot.ExitCode))"
    $medians = @()
    foreach ($l in (Get-Content $botOut)) {
        if ($l -match "(lat\d+) rtt median ([0-9.]+) ms, min ([0-9.]+) ms, max ([0-9.]+) ms over (\d+) pings") {
            $medians += [double]$Matches[2]
            $lines += "  $($l -replace '^remake-bot: ', '')"
        } elseif ($l -match "rtt: no PONG") {
            $lines += "  $l"
        }
    }
    $cpuText = ""
    if ($server) {
        $server.WaitForExit()
        $wall = ((Get-Date) - $t0).TotalSeconds
        $cpu = $server.TotalProcessorTime.TotalSeconds
        $cpuText = "orm_server CPU {0:N2} s over {1:N1} s ({2:N1} % of one core)" -f $cpu, $wall, (100 * $cpu / $wall)
        $lines += $cpuText
        $lines += (Get-Content $srvOut | Select-String -Pattern "stats|replaced|flood" | Select-Object -Last 4 | ForEach-Object { "  $_" })
    }
    if ($medians.Count -lt $Bots) { $lines += "only $($medians.Count) of $Bots bots reported a round trip (log $botOut)"; Finish "FAIL" $cpuText; exit 1 }
    $all = Median $medians
    $lines += ("rtt median of the bots {0:N1} ms (each bot: {1})" -f $all, (($medians | ForEach-Object { "{0:N1}" -f $_ }) -join " "))
    # The report has every line worth keeping; the raw logs go (they stay when the run fails).
    Remove-Item $botOut, "$botOut.err", $srvOut, "$srvOut.err" -ErrorAction SilentlyContinue
    Finish "OK" ("rtt {0:N1} ms; {1}" -f $all, $cpuText)
    exit 0
}

# pareja: the two games through orm_server by WebSocket.
if ($BuildBin) { $env:ORM_BUILD_BIN = $BuildBin }
if ($InstDir) { $env:ORM_INST_DIR = $InstDir }
$pyArgs = @("-I", (Join-Path $PSScriptRoot "r03_pareja.py"), "--scenario", $Scenario, "--transport", "ws",
            "--server-exe", $ServerExe, "--port", "$Port", "--timeout", "$Timeout")
if ($ScenDir) { $pyArgs += @("--scen-dir", $ScenDir) }
$lines += "harness: python $($pyArgs -join ' ') (ORM_BUILD_BIN=$env:ORM_BUILD_BIN ORM_INST_DIR=$env:ORM_INST_DIR)"
$previous = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& python @pyArgs 2>&1 | Out-Null
$code = $LASTEXITCODE
$ErrorActionPreference = $previous
$pairSummary = Join-Path $Reports "pareja\${Scenario}_resumen.txt"
$lines += "pair exit $code, summary $pairSummary"
if (-not (Test-Path $pairSummary)) { Finish "FAIL" "no pair summary"; exit 1 }
$lines += (Get-Content $pairSummary | Select-String -Pattern "^RESULT|^ready" | ForEach-Object { "  $_" })
$all = @()
foreach ($m in (Select-String -Path $pairSummary -Pattern "^-- ([AB]): .* log (.+)$")) {
    $name = $m.Matches[0].Groups[1].Value
    $log = $m.Matches[0].Groups[2].Value.Trim()
    if (-not (Test-Path $log)) { $lines += "$name : no log at $log"; continue }
    $text = Get-Content $log
    $first = $text | Select-String -Pattern "coop: rtt first ([0-9.]+) ms" | Select-Object -First 1
    $meds = @($text | Select-String -Pattern "coop: rtt median ([0-9.]+) ms, min ([0-9.]+) ms, max ([0-9.]+) ms" |
              ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
    $all += $meds
    $last = $text | Select-String -Pattern "coop: rtt median" | Select-Object -Last 1
    $lines += ("{0}: {1} rtt lines, median of them {2:N1} ms; first {3}; last: {4}" -f $name, $meds.Count, (Median $meds),
               $(if ($first) { $first.Matches[0].Groups[1].Value + " ms" } else { "none" }),
               $(if ($last) { ($last.Line -split "> ", 2)[-1] } else { "none" }))
}
if ($all.Count -eq 0) { Finish "FAIL" "no 'coop: rtt median' line in the game logs"; exit 1 }
Finish "OK" ("rtt {0:N1} ms (both games, {1} lines)" -f (Median $all), $all.Count)
exit 0
