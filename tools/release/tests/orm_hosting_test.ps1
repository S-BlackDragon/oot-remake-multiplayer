# Tests for the ORM hosting helpers (tools/release/kit/scripts/orm-hosting.ps1): the WebSocket
# switch, network kinds (ZeroTier), the port check before the server starts, the firewall lines,
# the server's log lines in Spanish and the host's self-check. No network and no firewall: every
# input is given as data.
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\tests\orm_hosting_test.ps1
# Exits 1 when a check fails.
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'kit\scripts\orm-hosting.ps1')

$failed = 0
function Check([bool]$Ok, [string]$What) {
    if ($Ok) { Write-Host "ok   $What" } else { Write-Host "FAIL $What"; $script:failed++ }
}
function Addr([string]$A, [string]$Alias, [int]$Prefix = 24) { [pscustomobject]@{ Address = $A; Alias = $Alias; Prefix = $Prefix } }
function Rule([string]$Name, [bool]$Allow, [string]$Program, [string]$Protocol, [string]$Port, [string]$Remote = 'Any', [string]$Profile = 'Private') {
    [pscustomobject]@{ Name = $Name; Enabled = $true; Inbound = $true; Allow = $Allow; Program = $Program; Protocol = $Protocol
        LocalPort = $Port; RemoteAddress = $Remote; Profile = $Profile }
}
function Text($Findings) { (@($Findings) | ForEach-Object { $_.Lines }) -join "`n" }
function Levels($Findings, [string]$Level) { @(@($Findings) | Where-Object { $_.Level -eq $Level }).Count }

# The placeholders have defaults when the file is used as it is in the tree.
Check ($OrmMaxPlayers -eq '6') 'most players: 6 without make_kit'
Check ($OrmDefaultPort -eq '27510') 'port: 27510 without make_kit'

# The WebSocket switch.
$on = ConvertFrom-OrmServerOptions @('# server_websocket = no', 'LICHII_COOP_PUPPET=1')
Check ($on.WebSocket -and -not $on.Problem) 'server_websocket: on unless a line without # says no'
$off = ConvertFrom-OrmServerOptions @('  server_websocket = No  ')
Check (-not $off.WebSocket) 'server_websocket = No: off'
Check ((ConvertFrom-OrmServerOptions @('server_websocket=yes')).WebSocket) 'server_websocket=yes: on'
$bad = ConvertFrom-OrmServerOptions @('server_websocket = maybe')
Check ($bad.Problem -match "server_websocket = 'maybe' no vale") 'server_websocket = maybe: a Spanish problem line'
Check ((Get-OrmWsPort 27510 $on) -eq 27511) 'WebSocket port: UDP port + 1'
Check ((Get-OrmWsPort 27510 $off) -eq 0) 'WebSocket off: port 0'
Check ((Get-OrmWsPort 65535 $on) -eq 0) 'no port after 65535: WebSocket off'
Check (((Get-OrmWsArgs 27510 $on) -join ' ') -eq '--ws-port 27511') 'orm_server arguments: --ws-port 27511'
Check (((Get-OrmWsArgs 27510 $off) -join ' ') -eq '--ws-port off') 'orm_server arguments: --ws-port off'
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("orm-hosting-test-{0}.ini" -f [guid]::NewGuid())
[System.IO.File]::WriteAllLines($tmp, [string[]]@('server_websocket = off'))
try { Check (-not (Read-OrmServerOptions $tmp).WebSocket) 'Read-OrmServerOptions reads the file' } finally { Remove-Item -LiteralPath $tmp -Force }
Check ((Read-OrmServerOptions 'Z:\no\such\file.ini').WebSocket) 'no file: WebSocket on'

# Network kinds and subnets.
Check ((Get-OrmNetworkKind '10.147.17.5' 'ZeroTier One [8056c2e21c000001]') -eq 'zerotier') 'ZeroTier by the adapter name'
Check ((Get-OrmNetworkKind '172.20.1.20' 'Wi-Fi') -eq 'lan') 'home network'
Check ((Get-OrmNetworkKind '25.1.2.3' 'Hamachi') -eq 'hamachi') 'Hamachi'
Check ((Get-OrmNetworkKind '26.1.2.3' 'Radmin VPN') -eq 'radmin') 'Radmin VPN'
Check ((Get-OrmNetworkKind '127.0.0.1' 'ZeroTier') -eq 'loopback') 'loopback stays loopback'
Check ((Get-OrmSubnet '10.147.17.5' 24) -eq '10.147.17.0/24') 'subnet /24'
Check ((Get-OrmSubnet '172.22.200.9' 16) -eq '172.22.0.0/16') 'subnet /16'
Check ((Get-OrmSubnet '10.10.191.130' 25) -eq '10.10.191.128/25') 'subnet /25'
Check ((Get-OrmSubnet 'abc' 24) -eq '') 'no subnet for a bad address'
Check ((Get-OrmScope 'zerotier' '10.147.17.5' 24).Remote -eq '10.147.17.0/24') 'ZeroTier rule: only its network'
Check ((Get-OrmScope 'zerotier' '10.147.17.5' 0).Remote -eq 'localsubnet') 'ZeroTier without a prefix: localsubnet'
Check ((Get-OrmScope 'hamachi').Profile -eq 'any') 'VPN rules: profile any'
Check ((Get-OrmScope 'lan').Profile -eq 'private' -and (Get-OrmScope 'lan').Remote -eq 'localsubnet') 'home rule: private, localsubnet'
Check ((Get-OrmScope 'public' '' 0 '').Problem -match 'server_allow') 'internet rule without server_allow: no line'
Check ((Get-OrmScope 'public' '' 0 '81.2.3.4, 90.5.6.7').Remote -eq '81.2.3.4,90.5.6.7') 'internet rule: only server_allow'

$local = @((Addr '172.20.1.20' 'Wi-Fi'), (Addr '127.0.0.1' 'Loopback' 8), (Addr '10.147.17.5' 'ZeroTier One [x]'), (Addr '169.254.1.1' 'Ethernet 2' 16))
$list = @(Get-OrmJoinList $local)
Check ((($list | ForEach-Object { $_.Kind }) -join ',') -eq 'zerotier,lan') 'join list: virtual network first, then home; nothing else'
Check ($list[0].Label -match 'ZeroTier') 'join list: the ZeroTier label'
Check (@(Get-OrmJoinList $local '172.20.1.20').Count -eq 1) 'join list: server_bind keeps that address only'
$vm = @(Get-OrmJoinList @((Addr '172.28.208.1' 'vEthernet (WSL)'), (Addr '172.20.56.1' 'VirtualBox Host-Only Network'), (Addr '172.20.1.20' 'Ethernet')))
Check ($vm.Count -eq 1 -and $vm[0].Address -eq '172.20.1.20') 'join list: virtual machine adapters are not the home network'

# The firewall lines.
$exe = 'C:\kit\server\coop_server.exe'
$lines = @(Format-OrmRuleLines $exe 27510 27511 (Get-OrmScope 'radmin'))
Check ($lines.Count -eq 2) 'rule lines: UDP and TCP'
Check ($lines[0] -match 'protocol=UDP localport=27510 profile=any remoteip=26\.0\.0\.0/8$' -and $lines[0] -match 'program="C:\\kit\\server\\coop_server\.exe"') 'UDP line: only the program, the port and the network'
Check ($lines[1] -match 'protocol=TCP localport=27511') 'TCP line: the WebSocket port'
Check (@(Format-OrmRuleLines $exe 27510 0 (Get-OrmScope 'lan')).Count -eq 1) 'WebSocket off: UDP line only'
$tcpOnly = @(Format-OrmRuleLines $exe 27510 27511 (Get-OrmScope 'lan') 'TCP')
Check ($tcpOnly.Count -eq 1 -and $tcpOnly[0] -match 'protocol=TCP') 'TCP only'
Check (Test-OrmRuleCovers (Rule 'r' $true $exe 'UDP' '27500-27520') 27510 'UDP') 'a port range covers the port'
Check (-not (Test-OrmRuleCovers (Rule 'r' $true $exe 'TCP' '27510') 27510 'UDP')) 'a TCP rule does not cover UDP'
Check (Test-OrmRuleCovers (Rule 'r' $true $exe 'Any' 'Any') 27511 'TCP') 'Any covers everything'

# The port check.
$ranges = ConvertFrom-OrmExcludedRanges "`r`nProtocol tcp Port Exclusion Ranges`r`n`r`nStart Port    End Port`r`n----------    --------`r`n     27500       27599`r`n     50000       50059     *`r`n`r`n* - Administered port exclusions."
Check ($ranges.Count -eq 2 -and $ranges[0].Start -eq 27500 -and $ranges[1].End -eq 50059) 'netsh reserved ranges read'
Check (Test-OrmPortInRanges 27511 $ranges) 'a port in a reserved range'
Check (-not (Test-OrmPortInRanges 27600 $ranges)) 'a port outside the ranges'
$free = Get-OrmPortCheck $exe 27510 27511 @() @() @() @()
Check ($free.State -eq 'free' -and -not $free.Lines.Count) 'ports free: nothing to say'
$ours = Get-OrmPortCheck $exe 27510 27511 @([pscustomobject]@{ Id = 7; Name = 'coop_server'; Path = $exe }) @() @() @()
Check ($ours.State -eq 'ours') 'held by this kit''s server: ours'
$other = Get-OrmPortCheck $exe 27510 27511 @([pscustomobject]@{ Id = 9; Name = 'Parsec'; Path = 'C:\p\parsec.exe' }) @() @() @()
$t = $other.Lines -join "`n"
Check ($other.State -eq 'busy' -and $t -match 'UDP 27510' -and $t -match 'Parsec \(PID 9\)') 'another program: named with its PID'
Check ($t -match "port = 27520" -and $t -match ':27520') 'another program: says how to change the port'
Check ($t -notmatch 'server_websocket') 'UDP busy only: no WebSocket advice'
$otherKit = Get-OrmPortCheck $exe 27510 27511 @([pscustomobject]@{ Id = 4; Name = 'coop_server'; Path = 'D:\old-kit\server\coop_server.exe' }) @() @() @()
Check (($otherKit.Lines -join "`n") -match 'otro servidor del cooperativo, de otra carpeta') 'another kit''s server: said so'
$reserved = Get-OrmPortCheck $exe 27510 27511 @() @() @() $ranges
$t = $reserved.Lines -join "`n"
Check ($reserved.State -eq 'busy' -and $t -match 'Windows tiene reservado el puerto TCP 27511' -and $t -match 'server_websocket = no') 'reserved TCP port: busy, and the WebSocket switch is offered'
Check ((Get-OrmPortCheck $exe 27510 0 @() @() @() $ranges).State -eq 'free') 'WebSocket off: the TCP port is not checked'
$unknown = Get-OrmPortCheck $exe 27510 0 @([pscustomobject]@{ Id = 3; Name = ''; Path = '' }) @() @() @()
Check (($unknown.Lines -join "`n") -match 'PID 3; Windows no dice') 'a program Windows does not name'

# The server's lines in Spanish.
Check ((Get-OrmServerLineHelp 'orm_server: cannot listen on udp port 27510') -match 'puerto UDP 27510') 'cannot listen on udp'
Check ((Get-OrmServerLineHelp 'orm_server: cannot listen on ws port 27511') -match 'server_websocket = no') 'cannot listen on ws'
Check ((Get-OrmServerLineHelp 'orm_server: player 2 joined: Saria') -eq '  -> Entró Saria (jugador 2).') 'joined'
Check ((Get-OrmServerLineHelp 'orm_server: player 2 left: Saria (timed out)') -eq '  -> Salió Saria (se cortó su conexión).') 'left, timed out'
Check ((Get-OrmServerLineHelp 'orm_server: player 3 left: Mido') -eq '  -> Salió Mido.') 'left'
# docs/RIESGOS_RESUELTOS.md: the link notices, and the firewall before it asks.
Check ((Get-OrmServerLineHelp 'orm_server: ws: player 4 cannot keep up (frames dropped): 412 of 1830 frames for it dropped in the last 5 s, 380 KiB waiting; it sees its mates jump; told the player') -match 'jugador 4 no da abasto') 'cannot keep up (ws)'
Check ((Get-OrmServerLineHelp 'orm_server: udp: player 2 cannot keep up (ENet throttling): ENet drops about 40% of the frames for it, 12 KiB waiting') -match 'jugador 2 no da abasto') 'cannot keep up (udp)'
Check ((Get-OrmServerLineHelp 'orm_server: player 3 (Mido) is over its message rate: 520 samples or poses and 0 reliable messages dropped (limits 240 and 60 a second, 1048576 bytes); told the player') -match 'El juego de Mido \(jugador 3\)') 'over its message rate'
Check ((Get-OrmServerLineHelp 'orm_server: ws: player 4 keeps up again (behind for 25 s)') -eq '  -> La conexión del jugador 4 vuelve a ir bien.') 'keeps up again'
Check (@(Get-OrmFirewallPreflight @() '').Count -eq 4 -and ((Get-OrmFirewallPreflight @() '') -join ' ') -match 'Permitir acceso') 'firewall: no rule yet, the prompt explained'
Check (@(Get-OrmFirewallPreflight @() '127.0.0.1').Count -eq 0) 'firewall: bound to 127.0.0.1, nothing to say'
Check (@(Get-OrmFirewallPreflight @((Rule 'coop' $true 'C:\kit\server\coop_server.exe' 'UDP' '27510')) '').Count -eq 0) 'firewall: already allowed, nothing to say'
Check (((Get-OrmFirewallPreflight @((Rule 'coop' $false 'C:\kit\server\coop_server.exe' 'UDP' 'Any')) '') -join ' ') -match 'BLOQUEA') 'firewall: a block rule is named before the friends try'
Check (((Get-OrmFirewallPreflight $null '') -join ' ') -match 'no se pudieron leer') 'firewall: rules unreadable'
Check ((Get-OrmServerLineHelp 'orm_server: refused a connection, reason 3 (server full)') -match 'partida llena \(como mucho 6') 'server full'
Check ((Get-OrmServerLineHelp 'orm_server: refused a connection, reason 6') -match 'contraseña') 'wrong join password'
Check ((Get-OrmServerLineHelp 'orm_server: refused a ws connection from 8.8.8.8: not in server_allow (--allow); refused so far: 1') -eq '  -> Rechazado 8.8.8.8: no está en server_allow (coop-options.ini).') 'not in server_allow'
Check ((Get-OrmServerLineHelp 'orm_server: listening udp 27510 ws 27511') -eq '') 'other lines: nothing added'

# The banner and the join block.
$b = (Format-OrmServerBanner 27510 27511) -join "`n"
Check ($b -match 'hasta 6 jugadores' -and $b -match 'TCP 27511' -and $b -match 'ws://') 'banner: six players and the WebSocket port'
Check ((Format-OrmServerBanner 27510 0) -join "`n" -match 'WebSocket: apagado') 'banner: WebSocket off'
$j = (Format-OrmJoinBlock (Get-OrmJoinList $local) 27510 27511) -join "`n"
Check ($j -match '10\.147\.17\.5\s+por ZeroTier' -and $j -match '172\.20\.1\.20\s+misma casa' -and $j -match '127\.0\.0\.1\s+otra ventana') 'join block: each address says when to use it'
Check ($j -match 'ws://10\.147\.17\.5:27511/') 'join block: the WebSocket form'
Check ($j -match 'comprobar-alojamiento\.cmd') 'join block: names the self-check'
$j = (Format-OrmJoinBlock (Get-OrmJoinList @((Addr '172.20.1.20' 'Wi-Fi'))) 27600 0) -join "`n"
Check ($j -match '172\.20\.1\.20:27600' -and $j -notmatch 'ws://' -and $j -match 'Radmin VPN, Hamachi o ZeroTier') 'join block: another port, no WebSocket, no VPN'

# The self-check.
$access = [ordered]@{ Allow = ''; Bind = '' }
$profiles = @([pscustomobject]@{ Alias = 'Wi-Fi'; Category = 'Private' }, [pscustomobject]@{ Alias = 'ZeroTier One [x]'; Category = 'Public' })
$udpListen = @([pscustomobject]@{ LocalAddress = '0.0.0.0' })
$tcpListen = @([pscustomobject]@{ LocalAddress = '0.0.0.0' })

$f = Get-OrmHostingFindings $exe 27510 27511 $false @() @() $free @() $local $profiles $access
$t = Text $f
Check ($t -match 'El servidor no está abierto' -and $t -match 'UDP 27510 y TCP 27511 están libres') 'not running, ports free'
Check ($t -match 'Sin comandos' -and $t -match 'Permitir acceso' -and $t -match 'Redes públicas') 'no rule: the way without commands first, VPN note'
Check ($t -match 'remoteip=10\.147\.17\.0/24' -and $t -match 'protocol=TCP localport=27511') 'no rule: ZeroTier lines for UDP and TCP'
Check ($t -match 'Primero pon en server_allow') 'no rule: the internet line needs server_allow'
$two = @((Addr '172.20.1.20' 'Wi-Fi'), (Addr '10.0.0.7' 'Ethernet'))
$t2 = Text (Get-OrmHostingFindings $exe 27510 0 $false @() @() $free @() $two @() $access)
Check (([regex]::Matches($t2, 'Misma casa')).Count -eq 1) 'two home addresses: one home rule'
Check ($t -notmatch 'contraseña de entrada') 'a kit without the join password: nothing said about it'

$f = Get-OrmHostingFindings $exe 27510 27511 $false @() @() $other @() $local $profiles $access
Check ((Text $f) -match 'Parsec') 'not running, port busy: the program is named'

$rules = @((Rule 'OoT co-op server UDP 27510' $true $exe 'UDP' '27510' '10.147.17.0/24' 'Any'), (Rule 'OoT co-op server TCP 27511' $true $exe 'TCP' '27511' '10.147.17.0/24' 'Any'))
$f = Get-OrmHostingFindings $exe 27510 27511 $true $udpListen $tcpListen $null $rules $local $profiles $access
$t = Text $f
Check ((Levels $f 'problem') -eq 0) 'running, both rules: no problem'
Check ($t -match 'en todas las redes' -and $t -match 'También entra gente por WebSocket') 'running: UDP and WebSocket'
Check ($t -match "Regla 'OoT co-op server TCP 27511'") 'running: the rules are named'

$f = Get-OrmHostingFindings $exe 27510 27511 $true $udpListen @() $null @($rules[0]) $local $profiles $access
$t = Text $f
Check ((Levels $f 'problem') -eq 0 -and $t -match 'Falta la regla para WebSocket \(TCP 27511\)') 'UDP rule only: a note for the TCP rule'
Check ($t -match 'no escucha WebSocket en TCP 27511') 'WebSocket not listening: a note'

$privateOnly = @((Rule 'r' $true $exe 'UDP' '27510' 'LocalSubnet' 'Private'))
$f = Get-OrmHostingFindings $exe 27510 0 $true $udpListen @() $null $privateOnly $local $profiles $access
$t = Text $f
Check ($t -match 'Windows llama "pública" a tu red de ZeroTier') 'VPN adapter Public, rule Private only: a problem'
Check ($t -match 'WebSocket apagado') 'WebSocket off: said'

$homePublic = @([pscustomobject]@{ Alias = 'Wi-Fi'; Category = 'Public' })
$f = Get-OrmHostingFindings $exe 27510 0 $true $udpListen @() $null $privateOnly @((Addr '172.20.1.20' 'Wi-Fi')) $homePublic $access
$t = Text $f
Check ($t -match "Tipo de perfil de red: Red privada" -and $t -match 'No hay Hamachi, Radmin VPN, ZeroTier ni Tailscale') 'home network Public: make it Private; no VPN note'

$blocked = @((Rule 'coop_server.exe' $false $exe 'UDP' 'Any'))
$f = Get-OrmHostingFindings $exe 27510 27511 $true $udpListen $tcpListen $null $blocked $local $profiles $access
$t = Text $f
Check ($t -match 'BLOQUEA' -and $t -match 'delete rule name=all dir=in program=') 'block rule: delete, then add'
Check ($t -notmatch 'Sin comandos') 'block rule: no way without commands is offered'

$wide = @((Rule 'OoT co-op' $true '' 'UDP' '27510'))
$f = Get-OrmHostingFindings $exe 27510 0 $true $udpListen @() $null $wide $local $profiles $access
Check ((Text $f) -match "La regla 'OoT co-op' deja entrar a cualquier programa") 'a rule with no program: a problem'

$f = Get-OrmHostingFindings $exe 27510 0 $true $udpListen @() $null $null $local $profiles $access
Check ((Text $f) -match 'No se pudieron leer las reglas') 'rules unreadable: a note'

$withPass = [ordered]@{ Allow = '81.2.3.4'; Bind = ''; Password = 'tres-palabras-largas' }
$f = Get-OrmHostingFindings $exe 27510 27511 $true $udpListen $tcpListen $null $rules $local $profiles $withPass
$t = Text $f
Check ($t -match 'Hay contraseña de entrada' -and $t -match 'Solo pueden entrar estas direcciones \(server_allow\): 81\.2\.3\.4') 'password and server_allow: said'
Check ($t -notmatch 'tres-palabras-largas') 'the password itself is never printed'
$noPass = [ordered]@{ Allow = ''; Bind = ''; Password = $null }
Check ((Text (Get-OrmHostingFindings $exe 27510 27511 $true $udpListen $tcpListen $null $rules $local $profiles $noPass)) -match 'Sin contraseña de entrada') 'a kit with the join password, none set: a note'

$bound = [ordered]@{ Allow = ''; Bind = '10.147.17.5' }
$f = Get-OrmHostingFindings $exe 27510 0 $true @([pscustomobject]@{ LocalAddress = '10.147.17.5' }) @() $null $rules $local $profiles $bound
$t = Text $f
Check ($t -match 'solo en 10\.147\.17\.5 \(server_bind\)' -and $t -match 'desde tu red de casa no se llega') 'server_bind: said'

if ($failed) { Write-Host "$failed check(s) failed."; exit 1 }
Write-Host 'All checks passed.'
exit 0
