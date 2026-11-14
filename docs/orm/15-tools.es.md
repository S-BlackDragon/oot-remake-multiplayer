[English](15-tools.md) | **Español**

# 15-tools: Herramientas de prueba, medidor de rendimiento y scripts del kit

Lo que añadimos para que las demás funciones se puedan probar y distribuir: un medidor del tiempo propio del gancho, más comandos de guion de pruebas para `coop_testinput`, una sonda de banderas, escenarios de PvP para el autotest del original, el archivo de opciones del kit con nuestros interruptores, comprobaciones de alojamiento en español y añadidos al paquete de marionetas. Nuestro arnés de dos juegos y la herramienta de partidas guardadas sintéticas no forman parte del árbol de código: vienen como `tools/orm/harness` y `tools/orm/saves` (véase el final).

## Qué cambia para los jugadores

Nada en el juego. En el kit:

- `coop-options.ini` enumera todos los interruptores de ORM con su valor de fábrica y una explicación sencilla (secciones "On by default", "Off by default", registro de depuración).
- El anfitrión recibe comprobaciones en español antes de que arranque el servidor (puertos libres, ocupados por el servidor de este kit, ocupados por otro programa, o reservados por Windows para Hyper-V, WSL o Docker), las redes que ve (Hamachi, Radmin VPN, Tailscale, ZeroTier), qué hará el cortafuegos de Windows (primero la forma sin comandos, después líneas `netsh` estrechas para un aviso de administrador: solo `coop_server.exe`, solo sus puertos, solo desde la red en uso), y las líneas de registro importantes del servidor explicadas. `comprobar-alojamiento.cmd` es la misma comprobación por separado, junto al `check-hosting.cmd` en inglés del original. Todo ello solo lee: no se abre ningún puerto ni se cambia ninguna regla ni ajuste del router.
- El color que el lanzador escribe en `coop.ini` (uno por jugador) se conserva y se pasa como `LICHII_COOP_COLOR`, de modo que tu propia túnica es el color que ven los demás.

## Interruptores

| Variable | Valor por defecto en el kit | Valor por defecto en el código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_PERF` | no está en el kit | desactivado (`coop_hook.cpp`, lectura `EnvFlag` del original) | sin definir, vacío o `0`: desactivado; **cualquier otro valor lo activa** (también `off` o `false`) | una línea de registro cada 5 s con el reparto del tiempo propio del gancho |
| `LICHII_ORM_FLAG_PROBE` | no está en el kit | desactivado (`coop_hook.cpp`, `coop_switch.h`) | 0/1, true/false, yes/no, on/off | registra cada cambio de las banderas sondeadas con su origen, y nombra una bandera que rebota entre juegos |
| `LICHII_ORM_FLAG_PROBE_NAMES` | no está en el kit | ninguno | nombres de banderas separados por comas | más banderas para la sonda |
| `LICHII_COOP_TEST_SCRIPT` | no está en el kit | ninguno | un archivo de guion | del original: reproduce un guion de pruebas (nuestros comandos de abajo se añaden a él) |
| `LICHII_COOP_TEST_COMMANDS` | no está en el kit | ninguno | un archivo | del original, solo Hookshot: pasos solo de prueba escritos por su controlador de autotest |
| `LICHII_COOP_DEBUG_LIFE` | comentado en el kit | desactivado | `1` | la vida de nuestro Link, el máximo, la palabra del componente Death, el evento y el comando AS se registran en cada cambio (líneas `coop: life:`, como máximo diez por segundo); también inicia el lector de entrada de pruebas |
| `LICHII_ORM_FRAME_LOG` | no está en el kit | | | el registro del tiempo de fotograma: función 01-platform |
| `server_websocket` (clave del kit, anfitrión) | comentado (`# server_websocket = no`) | activado | `yes`/`no` (también on/off, true/false, 1/0) | el oyente WebSocket sobre TCP, en el puerto UDP + 1; `no` deja solo UDP (función 02-network) |

## Cómo funciona

### Medidor de rendimiento

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_perf.h`, `.cpp` | `CoopPerf::Meter`: totales en nanosegundos por parte en ventanas de 5 s. "calc" es lo que se ejecuta dentro del hilo de cálculo del juego (la cueva svc: el juego espera por ella), dividido en fotograma de marioneta, copia de pose, cabezas, cinturón y llamadas, más cada llamador de svc por su orden; "present" es lo que se ejecuta en el hilo compositor del emulador en cada fotograma presentado, dividido en objetos, entrada, lectura, banderas, sesión, publicación, compartir, funciones y colocación; las lecturas y escrituras del invitado se cuentan en bytes. Apagado cuesta una lectura relajada por ámbito |
| `lichii/hook/coop_hook.cpp`, `coop_callers.cpp`, `coop_puppet.cpp` | Ámbitos `// ORM` alrededor de esas partes; la línea se escribe desde `OnFrame` |

Lo que el propio juego gasta en dibujar y animar las marionetas no está en estas cifras: es el tiempo de fotograma que sobra.

### Comandos de guion de pruebas añadidos a `coop_testinput`

Todos son solo de prueba, escritos en el guion `.A.txt` / `.B.txt` de un escenario (`lichii/hook/coop_testinput.h` documenta cada uno):

| Comando | Qué hace |
|---|---|
| `warp <scene> <start> adult\|child` | una última palabra pide esa edad para una escena cuyo nombre no lleva época (una mazmorra). Un warp pedido en juego libre inicia el evento de escena `/teleport` tras 2 s (de nuevo cada 6 s, como máximo 3 veces), ya que el juego solo acepta un cambio de escena cuando termina un evento |
| `life N` | escribe N cuartos de corazón en la vida actual de Link (0 lo mata); se rechaza con una línea de registro cuando no se puede leer la ruta |
| `gate 0\|1` | abre o cierra la palabra de la puerta de caído (función 04-downed; solo se parchea con `LICHII_COOP_DOWNED=1`) |
| `bottle A <value>` / `bottle A restore` | pone un valor de BottleItem (Empty, Fairy, RedPotion...) directamente en la botella A a E, poseyéndola si hace falta; `restore` devuelve lo que encontró la primera escritura |
| `event <name>` | pide al juego que inicie ese evento, como el fin de partida pide el suyo (desde la cueva svc; necesita `LICHII_COOP_PUPPET=1`) |
| `skip` | fuerza la omisión del evento en curso con la omisión forzada propia del juego; la capa de historia lo cuenta como omisión del dueño (07-cutscenes) |
| `flag <name\|0xhash> 0\|1` | encola una bandera Bool por la cola propia del juego, de modo que la partida guardada la conserva |
| `int <name\|0xhash> <value>` | encola una bandera Int (-1000000 a 1000000) |
| `key 0x<key> 0\|1` | encola una bandera de clave de 64 bits (el estado guardado de una colocación: cofres, hielo rojo, paredes agrietadas, contadores de sala) |
| `enum <name\|0xhash> [value]` | registra una bandera Enum tal como la tiene el juego (tipo, hash, nombre, la lista de valores del juego, los bytes de la entrada); con un valor, lo encola |
| `age adult\|child` | este juego recarga su escena con esa edad, como hace un juego que sigue el cambio de edad de un compañero (08-age) |
| `await mate [ticks]` | espera hasta que un compañero esté en nuestra escena (por defecto 9000 ticks, 150 s) |
| `talkthrough [ticks]` | pulsa A mientras corre un evento propio (el texto de una charla) hasta que termina (por defecto 1800 ticks, 30 s) |

### Sonda de banderas

`coop/host/include/coop/host/flag_probe.hpp`, `coop/host/src/flag_probe.cpp`: banderas cuyo significado solo lo muestra una partida. Por defecto: las banderas de interruptor de la sala 14 del Templo del Agua (`WaterTemple_Room14_SwitchPushed`, `..._SwitchPushedByBlock`, `..._SwitchPushedHeavyBoots`) y la piedra de la sala 17 de Jabu-Jabu (`InsidejaubjabuBelly_gimic_switchOn_Stone`). Una bandera que va y viene entre juegos se nombra una vez como candidata para una línea "held" en las listas de mazmorras (06-world). Enganchada en `coop_hook.cpp`.

### Escenarios de autotest del original

Nuestros escenarios de PvP en `tools/autotest/scenarios/` (`orm-pvp.steps.txt`, `pvp-espada`, `pvp-escudo`, `pvp-fijar`, `pvp-choque`, `pvp-bomba`, `pvp-cornisa`) se ejecutan con el `tools/autotest/run.ps1` del original; cada uno se describe en 09-pvp.

### Opciones del kit y scripts de alojamiento

| Archivo | Qué hace |
|---|---|
| `tools/release/kit/coop-options.ini` | Todos los interruptores de ORM con su valor de fábrica y explicación; las claves de anfitrión `server_websocket` añadidas |
| `tools/release/kit/coop.ini.template` | Una línea `color:` (un nombre de color o `#RRGGBB`, opcional) |
| `tools/release/kit/shared_*.txt` | Las listas de contenido (06-world, 07-cutscenes, 08-age); `make_kit.ps1` copia todos los `shared_*.txt` por patrón, así que una lista nueva no necesita ninguna línea ahí |
| `tools/release/kit/scripts/kit-paths.ps1` | Lee también las líneas `LICHII_ORM_*` (y `EDEN_NX2_*`), omite las claves de anfitrión; un valor que nombra archivos del kit, lista separada por comas incluida, se convierte en rutas completas (desde `kit\emulator`, `..\name`, cuando la ruta de la carpeta del kit tiene una coma) |
| `tools/release/kit/scripts/play.ps1` | Conserva el color en `coop.ini` y lo pasa como `LICHII_COOP_COLOR`; comprueba los puertos del servidor antes de que se abra su ventana |
| `tools/release/kit/scripts/start-server.ps1` | El rótulo en español y las direcciones para unirse (WebSocket incluido), comprobaciones de puertos y cortafuegos antes de arrancar, `--ws-port` desde `Get-OrmWsArgs`, las líneas del servidor explicadas en la ventana |
| `tools/release/kit/scripts/coop-ini.ps1` | Lee y escribe la clave `color` |
| `tools/release/kit/scripts/orm-hosting.ps1` | Las comprobaciones en español de arriba, como funciones que reciben sus datos como argumentos (así se prueban sin red). UTF-8 con BOM: Windows PowerShell 5.1 lee como ANSI un archivo que no lo tiene |
| `tools/release/kit/scripts/orm-check-hosting.ps1`, `tools/release/kit/comprobar-alojamiento.cmd` | La autocomprobación del anfitrión en español: antes de Host.bat, si los puertos están libres; con el servidor abierto, si está escuchando (UDP, y TCP para WebSocket), qué direcciones dar, si una regla del cortafuegos lo deja entrar, quién puede unirse |
| `tools/release/make_kit.ps1` | Copia las listas `shared_*.txt` por patrón y los dos scripts `orm-*.ps1` |

### Otros cambios en herramientas

| Archivo | Cambio |
|---|---|
| `tools/puppet_pack/build.py` | Actores nuevos en el paquete, cada uno cuando su origen está en el romfs del propio jugador: sustitutos de disparos (`CoopShot_*`: sedal de pesca, Fuego de Din, la onda de choque del martillo, flechas de fuego / hielo / luz), sustitutos de estado en las marionetas (`CoopFx_Burn`, `_Freeze`, `_Shock`, `_Nayru`), hallazgos de cofre sostenidos sobre quien los encuentra (`CoopGet_*`), caballos de los jinetes (`CoopPuppet_Horse`, `_Ranch`, `_Ingo`), marionetas de fijación (`CoopPuppet_*_Lock`, 09-pvp) y cuerpos de marioneta (`CoopPuppet_*_Body`, `_BodySide`, 12-bodies). `--no-effects` deja fuera los efectos |
| `tools/puppet_pack/dumpcheck.py` | Comprueba que el volcado contiene los orígenes de esos actores (entre ellos los parámetros de atención de Dark Link para las marionetas de fijación) |
| `tools/puppet_pack/tests/test_puppet_pack.py` | Pruebas para cada uno de ellos |
| `tools/hud_font/make_font.py` | Genera `lichii/hook/coop_hud_font.h` (10-hud) |
| `tools/game_scan/make_patterns.py` | `ORM_UNSCANNED`: nuestros desplazamientos solo de compilación (la quietud del objeto cargado, la puerta de caído y sus cuevas) no reciben patrón |
| `tools/coop_test/probe.cpp` | Conoce el evento `player_look` |

## Mensajes y componentes

Ninguno.

## Pruebas

| Prueba | Qué comprueba |
|---|---|
| `tests/lichii/perf_test.cpp` | El medidor: ventanas, partes, llamadores, bytes del invitado, el texto de la línea, apagado no cuesta nada |
| `tests/lichii/testinput_test.cpp`, `tests/orm/lichii_testinput_test.cpp` | Análisis de cada comando añadido y sus errores, la regla del evento de warp, la temporización de `talkthrough` y `await mate` |
| `tests/host/flag_probe_test.cpp` | Cambios sondeados, su origen, la regla del rebote |
| `tools/release/tests/orm_hosting_test.ps1` | El interruptor de WebSocket, tipos de red (ZeroTier), la comprobación de puertos, líneas del cortafuegos, las líneas del servidor en español, la autocomprobación; cada entrada dada como datos. Ejecutar: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\tests\orm_hosting_test.ps1` |
| `tools/release/tests/launch_test.ps1` | Las líneas `LICHII_ORM_*` llegan al emulador; los archivos del kit en el valor de un interruptor se convierten en rutas completas, listas con comas y una coma en la ruta del kit incluidas |
| `tools/puppet_pack/tests/test_puppet_pack.py` | Los actores nuevos |

## Líneas de registro

| Línea | Dice |
|---|---|
| `coop: perf: on (LICHII_ORM_PERF), a line every 5 s` | el medidor está activado |
| `coop: perf: 5.0 s, 300 presents (16.61 ms each), 300 calc frames, 5 puppets; calc 0.212 ms/frame (puppet frame ..., pose ..., heads ..., belt ..., calls ...; callers 10: ...); present 0.305 ms/frame (objects ..., input ..., read ..., flags ..., session ..., publish ..., share ..., features ..., place ...); guest 412 reads 96 writes 181 KB per calc frame` | una ventana |
| `coop: flag probe: <name> 0 -> 1 in our game` / `... from P2` / `... bounces between games (3 changes in 10.0 s, ours and the others'): a held: candidate` | la sonda de banderas |
| `coop: test ...` (por ejemplo `coop: test int PlayerStatus.CurrentRupee (0x...) = 87: queued`, `coop: test skip`, `coop: test key 0x...: queued`) | cada comando de prueba y lo que hizo el juego con él |
| `coop: life: ...` | `LICHII_COOP_DEBUG_LIFE` |
| `coop-options.ini: skipped '<line>' (only LICHII_COOP_NAME=value, LICHII_ORM_NAME=value or EDEN_NX2_NAME=value lines)` | `kit-paths.ps1` ignoró una línea |

## Estado

| Parte | Estado |
|---|---|
| Comandos de prueba | usados en ejecuciones de dos juegos que pasan, sobre nuestra compilación basada en test9: `life` (`caer-pareja`), `int` y `await mate` (`cartera-cada-uno`, `charla-mido`), `talkthrough` (`charla-dos`, `efectos-cofre`), `skip` (`meca-sostenido-espiritu`) |
| Medidor de rendimiento | todavía no confirmado en partida (solo probado con pruebas unitarias) |
| Sonda de banderas | todavía no confirmado en partida |
| Comprobaciones de alojamiento | probadas con pruebas unitarias (`orm_hosting_test.ps1`); todavía no ejecutadas en un PC anfitrión real con el kit de este árbol |
| Añadidos al paquete de marionetas | construidos y probados contra un volcado real del romfs; el uso de los actores en partida se informa por función (09, 11, 12) |

## Nuestro arnés y las partidas guardadas sintéticas

El arnés ejecuta dos juegos ocultos por escenario (A y B, unidos a un servidor local en 127.0.0.1), reproduce un guion en cada uno mediante los comandos de entrada de pruebas de arriba, y comprueba los registros con directivas de escenario (`#! expect`, `#! never`, `#! count`, `within <mark>..<mark>`, `#! switches`, `#! saves`, ...); un medidor de progreso cuenta las líneas de acierto y de fallo sobre los resúmenes de los escenarios. Los nombres de escenario usados en toda esta documentación (`caer-pareja`, `charla-dos`, `choque-bloqueo`...) son sus escenarios. La herramienta de partidas guardadas sintéticas construye las partidas de las que parten los escenarios, en cada PC a partir de sus propios archivos del juego. Ambas vienen en `tools/orm/harness` y `tools/orm/saves`, con sus propios README.

## Carencias conocidas

- `LICHII_ORM_PERF` usa la lectura `EnvFlag` del original, no `coop_switch.h`: `LICHII_ORM_PERF=off` lo **activa**.
- `start-server.ps1` siempre pasa `--ws-port` (un puerto, o `off`). El servidor compilado desde este árbol (`server/options.cpp`) no conoce esa opción y se detiene con `unknown argument: --ws-port`; el oyente WebSocket no forma parte de este árbol (función 02-network). O bien el servidor acepta `--ws-port`, o bien `start-server.ps1` lo omite cuando el servidor no tiene WebSocket.
- `make_kit.ps1` no copia `comprobar-alojamiento.cmd` al kit, y no rellena `@ORM_MAX_PLAYERS@` en `orm-hosting.ps1` (entonces el script dice 6).
- `pvp-cornisa` necesita una posición de cornisa antes de poder ejecutarse (09-pvp).
