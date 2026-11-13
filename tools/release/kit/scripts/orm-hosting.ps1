# ORM (alojamiento; docs/ALOJAMIENTO.md of the ORM repository): the host's checks in Spanish, for
# Host.bat (play.ps1 host), start-server.ps1 and comprobar-alojamiento.cmd. Dot-source it; it loads
# hosting.ps1 itself when that is not loaded yet.
#
# What it adds to upstream's hosting.ps1 and check-hosting.ps1:
#   - the WebSocket switch (coop-options.ini server_websocket = no) and its port (the UDP port + 1);
#   - the ports checked BEFORE the server starts: free, held by this kit's server, held by another
#     program (named), or reserved by Windows (Hyper-V, WSL and Docker reserve port ranges);
#   - ZeroTier (by the adapter's name) next to Hamachi, Radmin VPN and Tailscale;
#   - the firewall: first the way without commands (Windows' own question), then the narrow netsh
#     lines for an administrator Command Prompt (only coop_server.exe, only its ports, only from
#     the network in use);
#   - the server's log lines that matter, explained in Spanish.
# It reads only: no port is opened, no firewall rule or router setting is changed. The functions
# that decide take their facts as arguments, so tools\release\tests\orm_hosting_test.ps1 checks
# them without this PC's network. Saved as UTF-8 with a BOM: Windows PowerShell 5.1 reads a file
# without one as ANSI, and the messages have accents. The ORM make_kit.ps1 copies it as it is,
# writing the port (@PORT@) and the most players of the tree (@ORM_MAX_PLAYERS@).

if (-not (Get-Command Get-AddressKind -ErrorAction SilentlyContinue)) { . (Join-Path $PSScriptRoot 'hosting.ps1') }

# Most players in one game: kMaxPlayers of the co-op tree the kit was made from.
$OrmMaxPlayers = '@ORM_MAX_PLAYERS@'
if ($OrmMaxPlayers -notmatch '^\d+$') { $OrmMaxPlayers = '6' }
# The kit's usual port, when coop.ini does not say.
$OrmDefaultPort = '@PORT@'
if ($OrmDefaultPort -notmatch '^\d+$') { $OrmDefaultPort = '27510' }

# --- Options ---

# coop-options.ini's ORM server lines, from the file's lines. server_websocket: yes (the default)
# or no (also on/off, true/false, 1/0). Returns WebSocket ($true/$false) and Problem ('' or the
# Spanish line that says what is wrong).
function ConvertFrom-OrmServerOptions([string[]]$Lines) {
    $o = [ordered]@{ WebSocket = $true; Problem = '' }
    foreach ($line in @($Lines)) {
        if ("$line" -match '^\s*server_websocket\s*=\s*(.*?)\s*$') {
            $v = $Matches[1].ToLowerInvariant()
            if ($v -in @('no', 'off', 'false', '0')) { $o.WebSocket = $false }
            elseif ($v -in @('yes', 'on', 'true', '1', '')) { $o.WebSocket = $true }
            else { $o.Problem = "coop-options.ini: server_websocket = '$($Matches[1])' no vale; pon yes o no." }
        }
    }
    return $o
}

function Read-OrmServerOptions([string]$Path) {
    $lines = @()
    if ($Path -and (Test-Path -LiteralPath $Path)) { $lines = [System.IO.File]::ReadAllLines($Path) }
    return ConvertFrom-OrmServerOptions $lines
}

# The WebSocket loop's TCP port: the UDP port + 1, or 0 when it is off (or there is no next port).
function Get-OrmWsPort([int]$Port, $Options) {
    if (-not $Options.WebSocket -or $Port -lt 1 -or $Port -ge 65535) { return 0 }
    return $Port + 1
}

# orm_server's WebSocket arguments for start-server.ps1.
function Get-OrmWsArgs([int]$Port, $Options) {
    $ws = Get-OrmWsPort $Port $Options
    if ($ws) { return @('--ws-port', "$ws") }
    return @('--ws-port', 'off')
}

# --- Networks ---

function Get-OrmFullPath([string]$Path) {
    if (-not $Path) { return '' }
    try { return [System.IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($Path)) } catch { return $Path }
}

# What kind of network an address of this PC is on: hosting.ps1's kinds, plus 'zerotier' (any
# private range; known by the adapter's name, "ZeroTier One [...]"). The adapter's name also
# names Hamachi, Radmin VPN and Tailscale when their address is not the usual one.
function Get-OrmNetworkKind([string]$Address, [string]$Alias) {
    $kind = Get-AddressKind $Address
    if ($kind -in @('', 'loopback', 'link-local')) { return $kind }
    if ($Alias -match '(?i)zerotier') { return 'zerotier' }
    if ($Alias -match '(?i)hamachi') { return 'hamachi' }
    if ($Alias -match '(?i)radmin') { return 'radmin' }
    if ($Alias -match '(?i)tailscale') { return 'tailscale' }
    return $kind
}

function Get-OrmVpnName([string]$Kind) {
    switch ($Kind) {
        'hamachi' { 'Hamachi' }
        'radmin' { 'Radmin VPN' }
        'tailscale' { 'Tailscale' }
        'zerotier' { 'ZeroTier' }
        default { '' }
    }
}

function Test-OrmVpnKind([string]$Kind) { return ($Kind -in @('hamachi', 'radmin', 'tailscale', 'zerotier')) }

# The network an address belongs to, as a.b.c.d/prefix ('' when it is not an IPv4 address or
# the prefix is not 8 to 32).
function Get-OrmSubnet([string]$Address, [int]$Prefix) {
    if (-not (Get-AddressKind $Address) -or $Prefix -lt 8 -or $Prefix -gt 32) { return '' }
    $b = [System.Net.IPAddress]::Parse($Address).GetAddressBytes()
    $v = [long]$b[0] * 16777216 + [long]$b[1] * 65536 + [long]$b[2] * 256 + [long]$b[3]
    $size = [long][math]::Pow(2, 32 - $Prefix)
    $n = $v - ($v % $size)
    # Each part in its own parentheses: the comma binds tighter than %.
    $parts = @(([long][math]::Floor($n / 16777216) % 256), ([long][math]::Floor($n / 65536) % 256),
        ([long][math]::Floor($n / 256) % 256), ($n % 256))
    return ('{0}.{1}.{2}.{3}/{4}' -f $parts[0], $parts[1], $parts[2], $parts[3], $Prefix)
}

# Who may reach the server through one kind of network, for a firewall rule: Remote (remoteip)
# and Profile. VPN adapters are Public networks to Windows, so their rules use profile=any, kept
# narrow by program= and remoteip=. 'public' (port forwarding) takes only server_allow; without
# it there is no line, only Problem.
function Get-OrmScope([string]$Kind, [string]$Address = '', [int]$Prefix = 0, [string]$Allow = '') {
    switch ($Kind) {
        'hamachi' { return @{ Remote = '25.0.0.0/8'; Profile = 'any'; Problem = '' } }
        'radmin' { return @{ Remote = '26.0.0.0/8'; Profile = 'any'; Problem = '' } }
        # Tailscale's addresses are the shared CGNAT block of RFC 6598 (first octet 100, prefix /10),
        # the same remote range as hosting.ps1's rule for Tailscale.
        'tailscale' { return @{ Remote = ('{0}.{1}.0.0/10' -f 100, 64); Profile = 'any'; Problem = '' } }
        'zerotier' {
            $net = Get-OrmSubnet $Address $Prefix
            if (-not $net) { $net = 'localsubnet' }
            return @{ Remote = $net; Profile = 'any'; Problem = '' }
        }
        'public' {
            if (-not $Allow) {
                return @{ Remote = ''; Profile = 'private'; Problem = 'Primero pon en server_allow (coop-options.ini) las direcciones de tus amigos; después esta comprobación da la línea.' }
            }
            if ($Allow -notmatch '^[0-9./, ]+$') {
                return @{ Remote = ''; Profile = 'private'; Problem = "server_allow = '$Allow' no es una lista de direcciones (25.1.2.3,26.0.0.0/8): corrígelo primero." }
            }
            return @{ Remote = ($Allow -replace '\s', ''); Profile = 'private'; Problem = '' }
        }
        default { return @{ Remote = 'localsubnet'; Profile = 'private'; Problem = '' } }
    }
}

# The netsh lines (Command Prompt as administrator) that let in only this kit's coop_server.exe,
# only on its ports ($WsPort 0: UDP only; $Only 'TCP': the WebSocket port only), only from $Scope.
function Format-OrmRuleLines([string]$ServerExe, [int]$Port, [int]$WsPort, $Scope, [string]$Only = '') {
    $remote = if ($Scope.Remote) { " remoteip=$($Scope.Remote)" } else { '' }
    $out = @()
    if ($Only -ne 'TCP') {
        $out += "netsh advfirewall firewall add rule name=`"OoT co-op server UDP $Port`" dir=in action=allow program=`"$ServerExe`" protocol=UDP localport=$Port profile=$($Scope.Profile)$remote"
    }
    if ($WsPort) {
        $out += "netsh advfirewall firewall add rule name=`"OoT co-op server TCP $WsPort`" dir=in action=allow program=`"$ServerExe`" protocol=TCP localport=$WsPort profile=$($Scope.Profile)$remote"
    }
    return $out
}

# This PC's IPv4 addresses with the adapter's name and the prefix length.
function Get-OrmLocalAddresses {
    try {
        @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop | ForEach-Object {
            [pscustomobject]@{ Address = $_.IPAddress; Alias = $_.InterfaceAlias; Prefix = [int]$_.PrefixLength }
        })
    } catch { @() }
}

# Adapters of virtual machines on this PC (Hyper-V, WSL, Docker, VirtualBox, VMware): their
# private addresses are not the home network, so they are not offered to the others.
function Test-OrmVirtualMachineAdapter([string]$Alias) {
    return ($Alias -match '(?i)vEthernet|WSL|Hyper-V|Default Switch|VirtualBox|VMware|Docker|vboxnet')
}

# The addresses to give the others, virtual networks first, then the home network: objects with
# Address, Kind, Alias, Prefix and Label (Spanish). $Bind: server_bind (then only that address).
function Get-OrmJoinList($Local, [string]$Bind = '') {
    $vpn = @(); $lan = @()
    foreach ($a in @($Local | Where-Object { $_ })) {
        if ($Bind -and $a.Address -ne $Bind) { continue }
        $kind = Get-OrmNetworkKind $a.Address $a.Alias
        $prefix = 0
        if ($a.PSObject.Properties['Prefix']) { $prefix = [int]$a.Prefix }
        if (Test-OrmVpnKind $kind) {
            $name = Get-OrmVpnName $kind
            $vpn += [pscustomobject]@{ Address = $a.Address; Kind = $kind; Alias = $a.Alias; Prefix = $prefix; Label = "por $name (amigos en tu misma red de $name)" }
        }
        elseif ($kind -eq 'lan' -and -not (Test-OrmVirtualMachineAdapter $a.Alias)) {
            $lan += [pscustomobject]@{ Address = $a.Address; Kind = 'lan'; Alias = $a.Alias; Prefix = $prefix; Label = 'misma casa (amigos en tu misma red o Wi-Fi)' }
        }
    }
    return @($vpn + $lan)
}

# --- Messages of the server window ---

# The Spanish lines the server window prints when it starts.
function Format-OrmServerBanner([int]$Port, [int]$WsPort, [string]$MaxPlayers = $OrmMaxPlayers) {
    $lines = @('', "Servidor del cooperativo: hasta $MaxPlayers jugadores en la partida.")
    $lines += "  UDP ${Port}: lo normal (tus amigos abren Join.bat y escriben tu dirección)."
    if ($WsPort) { $lines += "  WebSocket, TCP ${WsPort}: para quien entre con ws://<tu dirección>:${WsPort}/" }
    else { $lines += '  WebSocket: apagado (server_websocket = no en coop-options.ini).' }
    $lines += 'Deja esta ventana abierta mientras jugáis: al cerrarla se acaba la partida para todos.'
    return $lines
}

# The Spanish "give your friends" block: $List from Get-OrmJoinList.
function Format-OrmJoinBlock($List, [int]$Port, [int]$WsPort, [string]$Bind = '') {
    $list = @($List | Where-Object { $_ })
    $lines = @('', 'Pásales UNA de estas direcciones (la escriben en Join.bat):')
    foreach ($a in $list) { $lines += ('  {0,-21} {1}' -f (Format-JoinAddress $a.Address $Port), $a.Label) }
    if (-not $list.Count) { $lines += '  (no se encontró ninguna red: enciende tu VPN, o mira tu dirección con ipconfig)' }
    $self = if ($Bind) { $Bind } else { '127.0.0.1' }
    $lines += ('  {0,-21} {1}' -f (Format-JoinAddress $self $Port), 'otra ventana del juego en este mismo PC')
    if ($WsPort -and $list.Count) { $lines += "  Por WebSocket: ws://$($list[0].Address):${WsPort}/ (o la misma forma con otra dirección de arriba)." }
    if (-not @($list | Where-Object { Test-OrmVpnKind $_.Kind }).Count) {
        $lines += 'Amigos de fuera de casa: lo más fácil y seguro es una red de Radmin VPN, Hamachi o ZeroTier para todos.'
    }
    $lines += 'Por internet sin VPN: solo con el puerto abierto en el router y server_allow puesto (ALOJAMIENTO.md).'
    $lines += '¿No pueden entrar? Con esta ventana abierta, doble clic en comprobar-alojamiento.cmd.'
    $lines += ''
    return $lines
}

# One line of the server's output explained in Spanish ('' when there is nothing to add).
function Get-OrmServerLineHelp([string]$Text, [string]$MaxPlayers = $OrmMaxPlayers) {
    if ($Text -match 'cannot listen on udp port (\d+)') {
        return "  -> No se pudo abrir el puerto UDP $($Matches[1]): otro programa lo usa o Windows lo tiene reservado. Doble clic en comprobar-alojamiento.cmd."
    }
    if ($Text -match 'cannot listen on ws port (\d+)') {
        return "  -> No se pudo abrir el puerto TCP $($Matches[1]) (WebSocket): otro programa lo usa o Windows lo tiene reservado. Si nadie entra por ws://, pon server_websocket = no en coop-options.ini."
    }
    if ($Text -match 'player (\d+) joined: (.+?)\s*$') { return "  -> Entró $($Matches[2]) (jugador $($Matches[1]))." }
    if ($Text -match 'player (\d+) rejoins: (.+?)\s*$') { return "  -> Volvió $($Matches[2]) (jugador $($Matches[1]))." }
    if ($Text -match 'player (\d+) left: (.*?)( \(timed out\))?\s*$') {
        $why = if ($Matches[3]) { ' (se cortó su conexión)' } else { '' }
        return "  -> Salió $($Matches[2])$why."
    }
    if ($Text -match 'refused a (\S+) connection from (\S+): not in server_allow') {
        return "  -> Rechazado $($Matches[2]): no está en server_allow (coop-options.ini)."
    }
    # docs/RIESGOS_RESUELTOS.md: a player whose line cannot take everyone's moves (his game tells him too).
    if ($Text -match '(ws|udp): player (\d+) cannot keep up') {
        return "  -> La conexión del jugador $($Matches[2]) no da abasto para recibir a todos: verá a los demás a saltos (su juego ya se lo dice). Que cierre descargas o vídeos, o que juegue por cable."
    }
    if ($Text -match 'player (\d+) \((.*)\) is over its message rate') {
        return "  -> El juego de $($Matches[2]) (jugador $($Matches[1])) manda más de lo que el servidor deja: los demás le ven a saltos (ya se le ha avisado). Suele ser por muchos enemigos suyos moviéndose a la vez."
    }
    if ($Text -match '(ws|udp): player (\d+) keeps up again') { return "  -> La conexión del jugador $($Matches[2]) vuelve a ir bien." }
    if ($Text -match 'refused a connection, reason (\d+)') {
        switch ([int]$Matches[1]) {
            1 { return '  -> Alguien intentó entrar con otra versión del kit: todos necesitáis el mismo kit (mira VERSION.txt).' }
            2 { return '  -> Alguien intentó entrar con otra versión del juego: todos necesitáis la misma actualización del juego.' }
            3 { return "  -> Alguien intentó entrar con la partida llena (como mucho $MaxPlayers jugadores)." }
            4 { return '  -> Alguien intentó entrar con un nombre que ya está en la partida: que elija otro (play.cmd change).' }
            5 { return '  -> Alguien intentó entrar sin nombre.' }
            6 { return '  -> Alguien intentó entrar con una contraseña de entrada equivocada.' }
        }
    }
    return ''
}

# ORM (docs/RIESGOS_RESUELTOS.md, "Red, kit y alojamiento"): before the server opens, what Windows' firewall will do
# with it, in Spanish, so its question is expected and answered right. $Rules: Get-ServerFirewallRules for this kit's
# coop_server.exe ($null: they could not be read). Reads only, never changes a rule. Nothing to say: no lines.
function Get-OrmFirewallPreflight($Rules, [string]$Bind = '') {
    if ($Bind -eq '127.0.0.1') { return @() }  # only this PC: Windows does not ask
    if ($null -eq $Rules) {
        return @('Cortafuegos: no se pudieron leer sus reglas. Si Windows pregunta por coop_server.exe, pulsa "Permitir acceso".')
    }
    $inbound = @($Rules | Where-Object { $_ -and $_.Enabled -and $_.Inbound })
    $blocks = @($inbound | Where-Object { -not $_.Allow })
    if ($blocks.Count) {
        return @('ATENCIÓN: el cortafuegos de Windows BLOQUEA el servidor de este kit (alguien pulsó "Cancelar" cuando Windows preguntó).',
            '  Tus amigos no podrán entrar hasta arreglarlo: con esta ventana abierta, doble clic en comprobar-alojamiento.cmd',
            '  (te da las dos líneas para pegar como administrador). En este mismo PC (127.0.0.1) sí se puede jugar.')
    }
    if (@($inbound | Where-Object { $_.Allow }).Count) { return @() }  # already let in
    return @('La primera vez, Windows va a preguntar si deja entrar a coop_server.exe ("Firewall de Windows Defender bloqueó algunas características").',
        '  Pulsa "Permitir acceso" con "Redes privadas" marcada; marca también "Redes públicas" si jugáis por Radmin VPN, Hamachi, ZeroTier o Tailscale.',
        '  Si pulsas "Cancelar", Windows lo bloquea y no vuelve a preguntar: entonces, comprobar-alojamiento.cmd.',
        '  Si mueves la carpeta del kit, Windows pregunta otra vez (la regla es de esta carpeta).')
}

# Waits for a key, $Seconds at most, so a message is read before this window closes.
function Wait-OrmKey([int]$Seconds = 120) {
    $redirected = $true
    try { $redirected = [Console]::IsInputRedirected } catch { }
    if ($redirected) { return }
    Write-Host -NoNewline "Pulsa una tecla para cerrar (se cierra sola en $Seconds s) "
    $end = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $end) {
        if ([Console]::KeyAvailable) { [Console]::ReadKey($true) | Out-Null; break }
        Start-Sleep -Milliseconds 100
    }
    Write-Host ''
}

# --- The ports, before the server starts ---

# Who holds a port on this PC: objects with Id, Name and Path ('' when Windows does not say, as
# for another user's program). $Protocol 'UDP': any UDP socket on it; 'TCP': a listening one.
function Get-OrmPortUsers([int]$Port, [string]$Protocol) {
    $ids = @()
    try {
        if ($Protocol -eq 'TCP') { $ids = @(Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction Stop | ForEach-Object { $_.OwningProcess }) }
        else { $ids = @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction Stop | ForEach-Object { $_.OwningProcess }) }
    } catch { $ids = @() }  # nothing on the port is an error to these cmdlets
    $out = @()
    foreach ($id in @($ids | Sort-Object -Unique)) {
        $name = ''; $path = ''
        try { $p = Get-Process -Id $id -ErrorAction Stop; $name = "$($p.ProcessName)"; $path = "$($p.Path)" } catch { }
        $out += [pscustomobject]@{ Id = [int]$id; Name = $name; Path = $path }
    }
    return $out
}

# The port ranges Windows keeps for itself, from "netsh interface ipv4 show excludedportrange"
# (objects with Start and End). The header lines are in the language of Windows; only the lines
# that start with two numbers count.
function ConvertFrom-OrmExcludedRanges([string]$Text) {
    $out = @()
    foreach ($line in @("$Text" -split "`r?`n")) {
        if ($line -match '^\s*(\d{1,5})\s+(\d{1,5})(\s|$)') { $out += [pscustomobject]@{ Start = [int]$Matches[1]; End = [int]$Matches[2] } }
    }
    return $out
}

function Get-OrmExcludedRanges([string]$Protocol) {
    try {
        $text = (& netsh.exe interface ipv4 show excludedportrange "protocol=$($Protocol.ToLowerInvariant())" 2>$null) -join "`n"
        return ConvertFrom-OrmExcludedRanges $text
    } catch { return @() }
}

function Test-OrmPortInRanges([int]$Port, $Ranges) {
    foreach ($r in @($Ranges | Where-Object { $_ })) { if ($Port -ge $r.Start -and $Port -le $r.End) { return $true } }
    return $false
}

# Can this kit's server open its ports? From facts: $UdpUsers and $TcpUsers (Get-OrmPortUsers),
# $UdpReserved and $TcpReserved (Windows' reserved ranges); $WsPort 0 = WebSocket off. Returns
# State: 'free', 'ours' (this kit's server already holds them) or 'busy'; and Lines (Spanish:
# what is wrong and what to do).
function Get-OrmPortCheck([string]$ServerExe, [int]$Port, [int]$WsPort, $UdpUsers, $TcpUsers, $UdpReserved, $TcpReserved) {
    $want = Get-OrmFullPath $ServerExe
    $lines = @(); $ours = $false; $busy = $false; $wsBusy = $false
    $checks = @(@{ Proto = 'UDP'; Port = $Port; Users = @($UdpUsers | Where-Object { $_ }); Reserved = $UdpReserved; What = 'el del juego' })
    if ($WsPort) { $checks += @{ Proto = 'TCP'; Port = $WsPort; Users = @($TcpUsers | Where-Object { $_ }); Reserved = $TcpReserved; What = 'el de WebSocket' } }
    foreach ($c in $checks) {
        $others = @($c.Users | Where-Object { -not ($_.Path -and (Get-OrmFullPath $_.Path) -ieq $want) })
        if ($c.Users.Count -and -not $others.Count) { $ours = $true; continue }
        if ($others.Count) {
            $busy = $true
            if ($c.Proto -eq 'TCP') { $wsBusy = $true }
            foreach ($u in $others) {
                $who = if ($u.Name -ieq 'coop_server' -and $u.Path) { "otro servidor del cooperativo, de otra carpeta ($($u.Path), PID $($u.Id))" }
                elseif ($u.Name -ieq 'coop_server') { "otro servidor del cooperativo (PID $($u.Id))" }
                elseif ($u.Name) { "otro programa: $($u.Name) (PID $($u.Id))" }
                else { "otro programa (PID $($u.Id); Windows no dice cuál)" }
                $lines += "El puerto $($c.Proto) $($c.Port) ($($c.What)) ya lo usa $who."
            }
        }
        elseif (Test-OrmPortInRanges $c.Port $c.Reserved) {
            $busy = $true
            if ($c.Proto -eq 'TCP') { $wsBusy = $true }
            $lines += "Windows tiene reservado el puerto $($c.Proto) $($c.Port) ($($c.What)) para otra cosa: Hyper-V, WSL o Docker suelen reservar tramos de puertos."
        }
    }
    if ($busy) {
        $alt = if ($Port -le 65500) { $Port + 10 } else { 27520 }
        $lines += 'Qué hacer, una de estas:'
        $lines += '  - Cierra ese programa (si es otra ventana del servidor del cooperativo, ciérrala) y abre Host.bat otra vez.'
        $lines += "  - O usa otro puerto: en emulator\user\config\coop.ini cambia 'port = $Port' por 'port = $alt' y abre Host.bat otra vez."
        $lines += "    Tus amigos escriben entonces tu dirección con :$alt al final (por WebSocket, el puerto TCP es $($alt + 1))."
        if ($wsBusy) { $lines += '  - O, si nadie entra por ws://, apaga el WebSocket: server_websocket = no en coop-options.ini.' }
    }
    $state = if ($busy) { 'busy' } elseif ($ours) { 'ours' } else { 'free' }
    return [pscustomobject]@{ State = $state; Lines = $lines }
}

# Looks at this PC's ports for this kit's server and prints what is wrong, in Spanish. Returns
# the State of Get-OrmPortCheck: 'free', 'ours' or 'busy'.
function Test-OrmHostPorts([string]$ServerExe, [int]$Port, $Options) {
    $ws = Get-OrmWsPort $Port $Options
    $udpUsers = @(Get-OrmPortUsers $Port 'UDP')
    $tcpUsers = @()
    if ($ws) { $tcpUsers = @(Get-OrmPortUsers $ws 'TCP') }
    $udpReserved = @(); $tcpReserved = @()
    if (-not $udpUsers.Count) { $udpReserved = @(Get-OrmExcludedRanges 'UDP') }
    if ($ws -and -not $tcpUsers.Count) { $tcpReserved = @(Get-OrmExcludedRanges 'TCP') }
    $check = Get-OrmPortCheck $ServerExe $Port $ws $udpUsers $tcpUsers $udpReserved $tcpReserved
    if ($check.State -eq 'ours') { Write-Host "El servidor de este kit ya está abierto en el puerto UDP ${Port}: no se abre otro." }
    foreach ($line in $check.Lines) { Write-Host $line }
    return $check.State
}

# --- The host's self-check (comprobar-alojamiento.cmd) ---

# Does a firewall rule cover $Protocol on $Port? Any or empty counts.
function Test-OrmRuleCovers($Rule, [int]$Port, [string]$Protocol) {
    $number = if ($Protocol -eq 'TCP') { '6' } else { '17' }
    if ("$($Rule.Protocol)" -notin @('', 'Any', $Protocol, $number)) { return $false }
    foreach ($p in @("$($Rule.LocalPort)" -split ',' | ForEach-Object { $_.Trim() })) {
        if (-not $p -or $p -eq 'Any' -or $p -eq "$Port") { return $true }
        if ($p -match '^(\d+)-(\d+)$' -and $Port -ge [int]$Matches[1] -and $Port -le [int]$Matches[2]) { return $true }
    }
    return $false
}

# The way without commands, for a PC with no rule yet for this coop_server.exe.
function Get-OrmNoAdminHint([bool]$Vpn) {
    $lines = @('  Sin comandos: cierra la ventana del servidor y abre Host.bat otra vez. Windows pregunta por coop_server.exe',
        '  ("Firewall de Windows Defender bloqueó algunas características de esta aplicación"): pulsa "Permitir acceso"',
        '  con "Redes privadas" marcada. Si tu cuenta de Windows no es de administrador, te pedirá la contraseña de uno.')
    if ($Vpn) {
        $lines += '  Con Hamachi, Radmin VPN, ZeroTier o Tailscale marca también "Redes públicas" (Windows llama públicas a esas redes);'
        $lines += '  eso deja entrar desde cualquier red pública, así que es más seguro usar la línea de abajo, que solo deja entrar desde tu red virtual.'
    }
    return $lines
}

# The check, from facts gathered by orm-check-hosting.ps1 (or a test):
#   $Running: this kit's coop_server.exe is running. $UdpListen / $TcpListen: its sockets on the
#   UDP port and the WebSocket port (objects with LocalAddress). $PortCheck: Get-OrmPortCheck
#   (when it is not running). $Rules: hosting.ps1's Get-ServerFirewallRules ($null = unreadable).
#   $Local: Get-OrmLocalAddresses. $Profiles: objects with Alias and Category (Public, Private,
#   DomainAuthenticated). $Access: Read-KitServerAccess (Allow, Bind and, in kits with the join
#   password, Password).
# Returns objects with Level ('ok', 'problem', 'note') and Lines (Spanish; commands indented).
function Get-OrmHostingFindings([string]$ServerExe, [int]$Port, [int]$WsPort, [bool]$Running, $UdpListen, $TcpListen,
    $PortCheck, $Rules, $Local, $Profiles, $Access) {
    $found = [System.Collections.ArrayList]::new()
    function Add-Finding([string]$Level, [string[]]$Lines) { [void]$found.Add([pscustomobject]@{ Level = $Level; Lines = $Lines }) }

    $bind = "$($Access.Bind)"
    $nets = @(Get-OrmJoinList $Local $bind)
    $vpnNets = @($nets | Where-Object { Test-OrmVpnKind $_.Kind })

    # 1. The server and its ports.
    if ($Running) {
        $udp = @($UdpListen | Where-Object { $_ })
        if ($udp.Count) {
            $where = @($udp | ForEach-Object { "$($_.LocalAddress)" } | Sort-Object -Unique)
            $on = if ($where -contains '0.0.0.0') { 'en todas las redes de este PC' } else { "solo en $($where -join ', ') (server_bind)" }
            Add-Finding 'ok' @("El servidor de este kit está abierto: UDP $Port, $on.")
        }
        else {
            Add-Finding 'problem' @("El servidor de este kit está abierto, pero no en el puerto UDP $Port.",
                '  ¿Cambiaste el puerto en coop.ini con el servidor abierto? Cierra su ventana y abre Host.bat otra vez.')
        }
        if ($WsPort) {
            if (@($TcpListen | Where-Object { $_ }).Count) { Add-Finding 'ok' @("También entra gente por WebSocket: TCP $WsPort (ws://<dirección>:$WsPort/).") }
            else { Add-Finding 'note' @("El servidor no escucha WebSocket en TCP ${WsPort}: solo se entra por UDP $Port.") }
        }
        else { Add-Finding 'note' @('WebSocket apagado (server_websocket = no): solo se entra por UDP.') }
    }
    else {
        if ($PortCheck -and $PortCheck.State -eq 'busy') { Add-Finding 'problem' @($PortCheck.Lines) }
        else {
            $ports = if ($WsPort) { "UDP $Port y TCP $WsPort están libres" } else { "UDP $Port está libre" }
            Add-Finding 'ok' @("Los puertos del servidor: $ports.")
        }
        Add-Finding 'problem' @('El servidor no está abierto.',
            '  Abre Host.bat, deja abierta la ventana del servidor y vuelve a ejecutar comprobar-alojamiento.cmd.')
    }

    # 2. Who may join.
    if ($Access.Allow) { Add-Finding 'ok' @("Solo pueden entrar estas direcciones (server_allow): $($Access.Allow)") }
    else {
        Add-Finding 'note' @('Puede entrar cualquiera que llegue al puerto (server_allow no está puesto).',
            '  Para dejar entrar solo a tus amigos: en coop-options.ini quita el # de server_allow, pon sus direcciones',
            '  (las de su VPN, o las públicas si abriste el puerto en el router) y abre Host.bat otra vez.')
    }
    if ($bind) { Add-Finding 'ok' @("El servidor solo escucha en $bind (server_bind): desde tu red de casa no se llega.") }
    if ($Access -is [System.Collections.IDictionary] -and $Access.Contains('Password')) {
        if ($Access.Password) { Add-Finding 'ok' @('Hay contraseña de entrada (server_password): tus amigos la escriben en Join.bat.') }
        else {
            Add-Finding 'note' @('Sin contraseña de entrada (server_password).',
                '  Para pedir una: en coop-options.ini pon server_password = <una sin espacios> (o borra esa línea y',
                '  Host.bat te la pregunta), abre Host.bat otra vez y pásasela a tus amigos con la dirección.')
        }
    }

    # 3. The firewall.
    if ($null -eq $Rules) {
        $what = if ($WsPort) { "UDP $Port y TCP $WsPort" } else { "UDP $Port" }
        Add-Finding 'note' @('No se pudieron leer las reglas del cortafuegos (otro antivirus o cortafuegos, o Windows no deja leerlas).',
            "  Comprueba que deja entrar a coop_server.exe por $what.")
    }
    else {
        $rules = @($Rules | Where-Object { $_ -and $_.Enabled -and $_.Inbound })
        $blocks = @($rules | Where-Object { -not $_.Allow -and $_.Program })
        $wide = @($rules | Where-Object { $_.Allow -and -not $_.Program })
        $allowUdp = @($rules | Where-Object { $_.Allow -and $_.Program -and (Test-OrmRuleCovers $_ $Port 'UDP') })
        $allowTcp = @()
        if ($WsPort) { $allowTcp = @($rules | Where-Object { $_.Allow -and $_.Program -and (Test-OrmRuleCovers $_ $WsPort 'TCP') }) }

        # The networks the lines are for: the ones this PC is on, the home network at least.
        $kinds = @()
        # One line per network: the home rule is the same for every home address (localsubnet).
        foreach ($n in $nets) {
            $remote = (Get-OrmScope $n.Kind $n.Address $n.Prefix $Access.Allow).Remote
            if (-not @($kinds | Where-Object { $_.Kind -eq $n.Kind -and (Get-OrmScope $_.Kind $_.Address $_.Prefix $Access.Allow).Remote -eq $remote }).Count) { $kinds += $n }
        }
        if (-not @($kinds | Where-Object { $_.Kind -eq 'lan' }).Count) { $kinds += [pscustomobject]@{ Address = ''; Kind = 'lan'; Prefix = 0; Alias = '' } }
        function Get-RuleBlock([string]$Only) {
            $out = @('  Como administrador ("Símbolo del sistema" con clic derecho > "Ejecutar como administrador"), la línea de tu caso:')
            foreach ($n in $kinds) {
                $label = if ($n.Kind -eq 'lan') { 'Misma casa (red de casa):' } else { "Por $(Get-OrmVpnName $n.Kind):" }
                $out += "    $label"
                foreach ($l in (Format-OrmRuleLines $ServerExe $Port $WsPort (Get-OrmScope $n.Kind $n.Address $n.Prefix $Access.Allow) $Only)) { $out += "      $l" }
            }
            $out += '    Por internet abriendo el puerto en el router (sin VPN):'
            $scope = Get-OrmScope 'public' '' 0 $Access.Allow
            if ($scope.Problem) { $out += "      $($scope.Problem)" }
            else { foreach ($l in (Format-OrmRuleLines $ServerExe $Port $WsPort $scope $Only)) { $out += "      $l" } }
            return $out
        }

        if ($blocks.Count) {
            Add-Finding 'problem' @("El cortafuegos BLOQUEA el servidor: $($blocks.Count) regla(s); suele pasar al pulsar ""Cancelar"" cuando Windows preguntó.",
                '  Esto solo se arregla como administrador: borra las reglas de este coop_server.exe y añade la de abajo:',
                "    netsh advfirewall firewall delete rule name=all dir=in program=`"$ServerExe`"")
        }
        foreach ($w in $wide) {
            Add-Finding 'problem' @("La regla '$($w.Name)' deja entrar a cualquier programa por ese puerto, no solo al servidor del cooperativo.",
                '  Como administrador:', "    netsh advfirewall firewall delete rule name=`"$($w.Name)`"",
                '  y después añade la regla de abajo, que solo deja entrar a coop_server.exe.')
        }
        if (-not $allowUdp.Count -or $blocks.Count -or $wide.Count) {
            $lines = @('Ninguna regla del cortafuegos deja que otros PCs lleguen al servidor de este kit.')
            if (-not $blocks.Count -and -not $allowUdp.Count) { $lines += Get-OrmNoAdminHint ([bool]$vpnNets.Count) }
            $lines += Get-RuleBlock ''
            $lines += '  En el mismo PC (127.0.0.1) no hace falta nada de esto.'
            Add-Finding 'problem' $lines
        }
        else {
            if ($WsPort -and -not $allowTcp.Count) {
                $lines = @("Falta la regla para WebSocket (TCP $WsPort). Solo hace falta si alguien entra con una dirección ws://.")
                $lines += Get-RuleBlock 'TCP'
                Add-Finding 'note' $lines
            }
            foreach ($r in @(@($allowUdp) + @($allowTcp) | Sort-Object Name -Unique)) {
                $remote = if ($r.RemoteAddress -and $r.RemoteAddress -ne 'Any') { $r.RemoteAddress } else { 'cualquier dirección' }
                Add-Finding 'ok' @("Regla '$($r.Name)': solo coop_server.exe, $($r.Protocol) puerto $($r.LocalPort), desde $remote, redes $($r.Profile).")
            }
            # Windows calls a VPN adapter a Public network: a rule for Private networks only does not cover it.
            $all = @(@($allowUdp) + @($allowTcp))
            $coversPublic = [bool]@($all | Where-Object { "$($_.Profile)" -match 'Any|Public' }).Count
            foreach ($n in $vpnNets) {
                $cat = "$(@($Profiles | Where-Object { $_ -and $_.Alias -eq $n.Alias } | ForEach-Object { $_.Category }) | Select-Object -First 1)"
                if ($cat -eq 'Public' -and -not $coversPublic) {
                    $lines = @("Windows llama ""pública"" a tu red de $(Get-OrmVpnName $n.Kind) y la regla solo vale para redes $($all[0].Profile).",
                        '  Como administrador:')
                    foreach ($l in (Format-OrmRuleLines $ServerExe $Port $WsPort (Get-OrmScope $n.Kind $n.Address $n.Prefix $Access.Allow))) { $lines += "    $l" }
                    Add-Finding 'problem' $lines
                }
            }
            # The home network called Public: the fix is to call it Private, not to widen the rule.
            foreach ($n in @($nets | Where-Object { $_.Kind -eq 'lan' })) {
                $cat = "$(@($Profiles | Where-Object { $_ -and $_.Alias -eq $n.Alias } | ForEach-Object { $_.Category }) | Select-Object -First 1)"
                if ($cat -eq 'Public' -and -not $coversPublic) {
                    Add-Finding 'problem' @("Windows llama ""pública"" a tu red de casa '$($n.Alias)', y la regla para redes privadas no vale ahí.",
                        "  Arreglo sin comandos: Configuración > Red e Internet > $($n.Alias) > Tipo de perfil de red: Red privada.",
                        '  (Solo en casa. No abras la regla a las redes públicas para esto.)')
                }
            }
        }
    }
    if (-not $vpnNets.Count) {
        Add-Finding 'note' @('No hay Hamachi, Radmin VPN, ZeroTier ni Tailscale en este PC. Para amigos de fuera de casa: una red de esas',
            '  (lo más fácil y seguro) o abrir el puerto en el router (ALOJAMIENTO.md).')
    }
    return @($found)
}
