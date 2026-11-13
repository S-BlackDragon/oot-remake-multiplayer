# ORM: the host's self-check in Spanish (comprobar-alojamiento.cmd), beside upstream's English
# check-hosting.cmd. Before Host.bat: are the server's ports free. With the server window open:
# is it listening (UDP, and TCP for WebSocket), which addresses to give, does a firewall rule let
# it in (only coop_server.exe, only its ports), who may join. It reads only; for each problem it
# prints what to do, the way without commands first. UTF-8 with a BOM (orm-hosting.ps1 says why).
# Usage: comprobar-alojamiento.cmd [port]   (default: the port in emulator\user\config\coop.ini)
param([int]$Port = 0)
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'coop-ini.ps1')
. (Join-Path $PSScriptRoot 'kit-paths.ps1')
. (Join-Path $PSScriptRoot 'hosting.ps1')
. (Join-Path $PSScriptRoot 'orm-hosting.ps1')
$kit = Split-Path -Parent $PSScriptRoot
$serverExe = [System.IO.Path]::GetFullPath((Join-Path $kit 'server\coop_server.exe'))
if (-not $Port) {
    $ini = Read-CoopIni (Join-Path $kit 'emulator\user\config\coop.ini')
    $Port = if ($ini.port -match '^\d{1,5}$' -and [int]$ini.port -ge 1 -and [int]$ini.port -le 65535) { [int]$ini.port } else { [int]$OrmDefaultPort }
}
$optionsFile = Join-Path $kit 'coop-options.ini'
$options = Read-OrmServerOptions $optionsFile
$ws = Get-OrmWsPort $Port $options
$what = if ($ws) { "UDP $Port y TCP $ws" } else { "UDP $Port" }
Write-Host "Comprobando este PC para hospedar (kit: $kit; puertos $what)..."
Write-Host ''
if ($options.Problem) { Write-Host "PROBLEMA $($options.Problem)"; Write-Host '' }

$pids = @(Get-Process coop_server -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and (Get-OrmFullPath $_.Path) -ieq $serverExe } | ForEach-Object { $_.Id })
$running = [bool]$pids.Count
$udpListen = @(); $tcpListen = @(); $portCheck = $null
if ($running) {
    try { $udpListen = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Where-Object { $_.OwningProcess -in $pids }) } catch { }
    if ($ws) {
        try { $tcpListen = @(Get-NetTCPConnection -State Listen -LocalPort $ws -ErrorAction SilentlyContinue | Where-Object { $_.OwningProcess -in $pids }) } catch { }
    }
}
else {
    $udpUsers = @(Get-OrmPortUsers $Port 'UDP')
    $tcpUsers = @()
    if ($ws) { $tcpUsers = @(Get-OrmPortUsers $ws 'TCP') }
    $udpReserved = @(); $tcpReserved = @()
    if (-not $udpUsers.Count) { $udpReserved = @(Get-OrmExcludedRanges 'UDP') }
    if ($ws -and -not $tcpUsers.Count) { $tcpReserved = @(Get-OrmExcludedRanges 'TCP') }
    $portCheck = Get-OrmPortCheck $serverExe $Port $ws $udpUsers $tcpUsers $udpReserved $tcpReserved
}
$profiles = @()
try { $profiles = @(Get-NetConnectionProfile -ErrorAction Stop | ForEach-Object { [pscustomobject]@{ Alias = $_.InterfaceAlias; Category = "$($_.NetworkCategory)" } }) } catch { }
$local = Get-OrmLocalAddresses
$access = Read-KitServerAccess $optionsFile
$findings = Get-OrmHostingFindings $serverExe $Port $ws $running $udpListen $tcpListen $portCheck (Get-ServerFirewallRules $serverExe) $local $profiles $access

$problems = 0
foreach ($f in $findings) {
    $tag = switch ($f.Level) { 'ok' { 'BIEN    ' } 'problem' { 'PROBLEMA' } default { 'NOTA    ' } }
    if ($f.Level -eq 'problem') { $problems++ }
    $first = $true
    foreach ($line in $f.Lines) {
        if ($first) { Write-Host "$tag $line"; $first = $false } else { Write-Host "         $line" }
    }
}
foreach ($line in (Format-OrmJoinBlock (Get-OrmJoinList $local "$($access.Bind)") $Port $ws "$($access.Bind)")) { Write-Host $line }
if ($problems) { Write-Host 'Arregla las líneas PROBLEMA (los comandos van en el "Símbolo del sistema" como administrador) y vuelve a ejecutar esto.' }
else { Write-Host 'No se ve nada mal en este PC. Si aun así no entran: revisa la dirección que escribieron, la VPN (todos conectados, en verde) o el router.' }
Write-Host 'Esta comprobación no ha cambiado nada en tu PC.'
