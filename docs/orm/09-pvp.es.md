[English](09-pvp.md) | **Español**

# 09-pvp: reglas de PvP, fijar aliados y comandos de chat

Desde test10, el interruptor de jugador contra jugador de la sesión y los mensajes de golpe son los de upstream (`PVP_MODE`, `PVP_HIT`, `PVP_HURT`, `lichii/hook/coop_pvp*`). Esta función mantiene nuestras reglas como una capa fina encima de ellos, y añade tres cosas que no son PvP pero se construyeron junto a él: "Fijar aliados" (apuntar con Z a la marioneta de un compañero), un registro de comandos de chat con completado con Tab, y las llamadas, los cambios de nombre y las estadísticas en pantalla que esos comandos manejan.

## Qué cambia para los jugadores

- **El PvP está apagado por defecto** y solo el anfitrión lo enciende o apaga (`pvp =` en el `coop-options.ini` del anfitrión, o `/pvp on|off` en el chat). Esa es la regla de upstream; `/pvp` ahora va al comando de upstream.
- Con el PvP encendido, un golpe hace el **daño real del arma**: medio corazón por un tajo de la Espada Kokiri, un corazón por la espada larga, los ataques giratorios y de salto, dos por el Martillo Megatón, y nuestra tabla para flechas, semillas, gancho, bumerán y el Fuego de Din. Se usa la cantidad única por causa de upstream cuando se desconoce el comando de espada del atacante.
- Después, el juego de la víctima juzga el golpe:
  - un **escudo levantado hacia el atacante** (70 grados a cada lado) lo bloquea; de lado o por detrás no;
  - el **Amor de Nayru** bloquea desde todos los lados;
  - 0,8 s de **invulnerabilidad** tras recibir un golpe, y mientras suena nuestra propia reacción al golpe;
  - un Link caído no recibe nada;
  - el **último en pie** nunca se queda en 0: se le deja un cuarto de corazón, de modo que el golpe de un amigo nunca termina la partida de la sala (el servidor también cuenta como en pie, para la derrota del grupo, a un jugador que cae en los 3 s posteriores a un golpe de PvP, durante 15 s);
  - un **empujón**: el golpe empuja a la víctima (el gancho tira de ella), repartido en 0,22 s; la propia gravedad del juego puede tirarlo de una cornisa.
- **Fijar aliados** (`/fijar on`): Z fija la marioneta de un compañero como si fuera un enemigo. Afecta a toda la sala; cualquiera puede cambiarlo; todos ven quién lo hizo.
- **Comandos de chat**: una línea de chat que empieza por `/` es un comando y nunca se envía como chat. Al escribir `/` aparece la lista sobre la caja del chat con una ayuda de una línea cada uno; se acota a medida que escribes; tras un comando ofrece los argumentos (on/off, nombres de jugadores, lugares del HUD). Tab completa (otra vez: el siguiente), las flechas eligen, Enter ejecuta.
- **Llamadas**: `/traer <nombre>` pide a un compañero que venga (él responde `/aceptar` en el plazo de un minuto); `/reunir` del anfitrión llama a todos. Aceptar lleva allí con las reglas del `/teleport` de upstream (no en una cinemática, no en un combate de jefe).
- **Cambiar de nombre jugando**: `/nombre <nuevo>` (de 1 a 16 letras, dígitos, `.`, `_`, `-`); el servidor rechaza un nombre ya en uso; todos ven el nuevo nombre en las etiquetas, el chat, las listas y el HUD; se guarda en `coop.ini`.
- **Estadísticas**: `/fps on`, `/ping on`, `/red on`, `/stats on` mantienen una línea pequeña en la esquina superior derecha del HUD; se recuerdan entre sesiones en `orm_stats.txt`, junto a `coop.ini`. `/stats` y `/ping` solos responden una vez en el chat.

Los comandos, sus alias y argumentos (de `coop/host/src/orm_commands.cpp`; las palabras de los comandos están en español, como las escriben los jugadores):

| Comando | Alias | Quién | Alcance | Qué hace |
|---|---|---|---|---|
| `/ayuda [comando]` | `/help`, `/comandos`, `/?` | cualquiera | local | esta lista, o un comando con sus alias |
| `/pvp [on\|off]` | | cualquiera lo pide; upstream deja cambiarlo solo al anfitrión | sala | el interruptor de PvP de upstream; solo: cómo está |
| `/fijar [on\|off]` | `/fijaraliados`, `/lock` | cualquiera | sala (aviso) | fijar con Z a los compañeros como enemigos; solo: cómo está |
| `/choque [on\|off]` | `/cuerpos`, `/bodies` | cualquiera | sala (aviso) | cuerpos reales entre los Links (función 12-bodies) |
| `/jugadores` | `/players`, `/quien` | cualquiera | local | nombre, lugar, en pie o caído, ping |
| `/donde <nombre>` | `/where` | cualquiera | local | el lugar de ese jugador, y a qué distancia cuando es el tuyo |
| `/ir [nombre]` | `/teleport`, `/tp`, `/go` | cualquiera | local | el `/teleport` de upstream con sus reglas; solo: la lista |
| `/traer <nombre>` | `/bring`, `/llamar` | cualquiera | a ese jugador | le pide que venga |
| `/reunir` | `/gather` | anfitrión | a todos | llama a todos junto al anfitrión |
| `/aceptar` | `/accept` | cualquiera | local | ir con quien te llamó |
| `/anfitrion [nombre\|auto]` | `/host` | cualquiera | sala | quién es el anfitrión y por qué; un nombre o `auto` (función 02-network) |
| `/ping [on\|off]` | | cualquiera | local | latencia al servidor y a cada compañero; on/off: siempre en pantalla |
| `/fps [on\|off]` | | cualquiera | local | los fps del juego en pantalla |
| `/red [on\|off]` | `/net` | cualquiera | local | kbps de subida y bajada y pérdida en pantalla |
| `/stats [on\|off]` | | cualquiera | local | todo lo anterior más marionetas y los ms del hook; solo: un informe |
| `/hud [on\|off\|nombres\|rupias\|panel\|tamano\|chat\|reset]` | | cualquiera | local | el panel (como F9), o un ajuste del HUD (función 10-hud) |
| `/rupias [on\|off\|lugar]` | `/rupees` | cualquiera | local | la línea de rupias de todos, y dónde (10-hud) |
| `/nombres [on\|off]` | `/names`, `/etiquetas` | cualquiera | local | nombres sobre los compañeros (10-hud) |
| `/mapa [on\|off]` | `/map` | cualquiera | local | compañeros en los mapas (10-hud) |
| `/color <#RRGGBB\|nombre>` | `/colour` | cualquiera | los demás lo ven | tu color ahora; se guarda en `coop.ini` |
| `/espectar <nombre>` | `/mirar`, `/spectate` | solo caídos | local | la cámara de espectador mira a ese compañero (04-downed) |
| `/registros` | `/logs` | cualquiera | local | pide al ayudante de la app que envíe tus registros (14-app; sin la app: lo dice) |
| `/version` | | cualquiera | local | versiones de la app, del kit y del protocolo |
| `/hora` | `/time` | cualquiera | local | la hora del día compartida (la última que envió un juego) |
| `/reintentar` | `/retry` | cualquiera | local | reintenta la conexión ahora, y las descargas de la app |
| `/silencio [on\|off]` | `/mute` | cualquiera | local | oculta en tu pantalla el chat de los compañeros |
| `/nombre <nuevo>` | `/name`, `/nick` | cualquiera | sala (aviso) | cambiar de nombre ahora |

`on|off` también acepta `true/false`, `sí/si/no`, `1/0`, `activar/desactivar`, `encender/apagar`. A un jugador se le nombra por su nombre completo (mayúsculas o minúsculas), por el comienzo de su nombre cuando solo coincide uno, o por su número (`2`, `P2`). Un comando desconocido responde `Comando desconocido, escribe /ayuda`.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `pvp` (clave del kit, solo anfitrión) | `pvp = off` | el de upstream | `on`, `off` | el de upstream: el PvP de la sesión al empezar (el `--pvp` del servidor) |
| `LICHII_ORM_PVP` | no está en el kit | on (`orm_commands_game.cpp`) | 0/1, true/false, yes/no, on/off | nuestras reglas sobre los golpes de upstream (daño del arma, escudo, Nayru, invulnerabilidad, último en pie, empujón). Off: los golpes de upstream tal cual |
| `LICHII_ORM_PVP_PUSH` | no está en el kit | on (`orm_commands_game.cpp`) | igual que arriba | el empujón tras un golpe |
| `LICHII_ORM_PVP_GUARD` | no está en el kit | `ZL` (`orm_commands_game.cpp`) | `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `LCLICK`, `RCLICK` | el botón de guardia que se lee para un escudo levantado. Otro valor: ZL y una línea de aviso |
| `LICHII_ORM_BLAST_FALLBACK` | no está en el kit | on (`orm_commands_game.cpp`) | igual que arriba | la bomba de un compañero que explotó sin copia en nuestro juego hace daño a nuestro Link aquí (1 corazón, empujón) si está a menos de 4 m; solo mientras el PvP de upstream está apagado (con él encendido, el atacante de upstream ya envía ese golpe) |
| `LICHII_ORM_COMMANDS` | no está en el kit | on (`orm_commands_game.cpp`) | igual que arriba | el registro de comandos. Off: la caja del chat pasa las líneas con `/` a los manejadores más antiguos (`/teleport`, `/hud`...) como antes |
| `LICHII_ORM_BODIES` | `LICHII_ORM_BODIES=1` | on | igual que arriba | se lee también aquí para `/choque`; descrito en 12-bodies |
| `LICHII_ORM_NAYRU_S` | no está en el kit | el valor del paquete de marionetas | segundos | cuánto tiempo se muestra el cristal de Nayru en la marioneta de un compañero: función 11-puppets, no es una regla de PvP |
| `LICHII_COOP_PVP_REPLAY` | no está en el kit | el de upstream | el de upstream | el interruptor "el golpe del propio juego" de upstream; no es nuestro (ver Carencias conocidas) |

Todo interruptor on/off `LICHII_ORM_*` de aquí lo lee `lichii/hook/coop_switch.h` (función 01-platform): un valor que no acepta mantiene el valor por defecto y escribe `coop: switch NAME=VALUE is not ...`.

Ajustes del servidor (`server/session.hpp`, no son interruptores): `pvp_down_window_us` 3 s, `pvp_down_hold_us` 15 s, `hits_per_second` 8, `call_gap_us` 1 s (una llamada o un cambio de nombre por segundo y jugador).

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/orm_pvp.hpp`, `coop/host/src/orm_pvp.cpp` | Reglas portables, sin emulador. `attack_amount` (el daño real del arma a partir del comando AS de nuestro Link o de la tabla de disparos), `knock_of`, `UpstreamVictim` (escudo, Nayru, invulnerabilidad, repetición, caído, último en pie), `guard_of` (un escudo levantado leído de tres maneras: un comando de guardia en cualquier ranura AS, el botón de guardia con un escudo equipado en una pose que guarda, o sin escudo), `Shove`, `blast_hurt`, `RoomSwitches` (los interruptores de la sala y sus avisos en español), `AllyTarget` (la comprobación de la marioneta de fijar y su alternativa), `spare_last`. El par más antiguo `Attacker`/`Victim` para nuestro propio `PLAYER_HIT` sigue ahí, sin usar mientras el PvP de upstream es el de la sesión |
| `coop/host/include/coop/host/orm_commands.hpp`, `coop/host/src/orm_commands.cpp` | El registro de comandos: nombres, alias, tipos de argumento, quién puede ejecutarlos, líneas de ayuda, análisis, nombres de jugadores, colores, ediciones de `coop.ini`, y las sugerencias con el ciclo Tab / flechas / Enter |
| `coop/host/include/coop/host/orm_stats.hpp`, `coop/host/src/orm_stats.cpp` | `/fps`, `/ping`, `/red`, `/stats`: las líneas de la esquina, los informes del chat, las preferencias de `orm_stats.txt` (también `/hud on|off`, `/mapa`, `/silencio`) |
| `coop/protocol/src/orm_messages.cpp` | Disposición en bytes de `ROOM_SWITCH`, `PLAYER_HIT`, `PLAYER_CALL`, `PLAYER_RENAME` |
| `server/orm_session.cpp` | Reglas del servidor: cualquier jugador cambia un interruptor de sala, el servidor sella quién y cuándo, lo retransmite a todos y se lo da a quienes entran tarde; golpes solo con el PvP encendido, solo a la víctima, como mucho 8 por segundo por atacante; `/traer` a un jugador, `/reunir` solo del anfitrión; cambios de nombre comprobados contra choques |
| `lichii/hook/orm_commands_game.h`, `.cpp` | El pegamento: ejecuta los comandos desde la caja del chat, lee nuestro Link (ranuras AS, escudo equipado, mando), alimenta la lectura del escudo al juez de la víctima, aplica el empujón con `MoveLink`, crea las variantes de marioneta con fijación para "Fijar aliados", publica los interruptores a la app (14-app), mantiene las líneas de la esquina |
| `lichii/hook/orm_chat_complete.h`, `.cpp` | La lista de sugerencias de Qt sobre la caja del chat |
| `lichii/hook/orm_pvp_shared.h` | Solo cabecera: nuestros disparos en vuelo y las bombas de los compañeros que explotaron sin copia aquí, pasados desde la sincronización de aparición al fotograma de PvP |
| `tools/puppet_pack/build.py` | Construye `CoopPuppet_Child_Lock` y `CoopPuppet_Adult_Lock`: la marioneta más un `AttentionSenderParam` de fijación (solo el cliente de fijación, para que las flechas no apunten solas a los compañeros), tomado del paquete de Dark Link (`dumpcheck.py` ahora comprueba que está en el volcado) |

Puntos de enganche en archivos de upstream:

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_pvp_game.cpp` | Tres líneas `// ORM`: después de que el atacante de upstream crea un `PVP_HIT` por un tajo de espada y por un disparo, `OrmCommands::PvpAttackAmount` sustituye la cantidad por la del arma real; después de que la víctima de upstream acepta un golpe, `OrmCommands::PvpVictimTake` aplica nuestras reglas y reescribe el `PVP_HURT` (cantidad, vida restante, indicador `guarded` o `died`) para que el atacante oiga lo que realmente se recibió |
| `lichii/hook/coop_hook.cpp`, `coop_window.cpp` | La caja del chat envía las líneas con `/` al registro; el fotograma de PvP corre en cada fotograma |
| `lichii/hook/coop_spawn.cpp`, `coop_spawn.h` | Nuestros disparos y las explosiones de bombas no vistas, al tablero compartido |
| `lichii/hook/coop_camera_probe.cpp`, `coop_spectator.cpp` | `/espectar` |
| `coop/net/client.*` | El nombre nuevo tras un cambio de nombre, contadores de bytes para `/red` |
| `server/session.cpp`, `session.hpp` | La gracia de 15 s "caer por PvP cuenta como en pie", activada también por el `PVP_HIT` de upstream; los manejadores de ORM |
| `coop/protocol/codec.cpp`, `messages.hpp` | Los cuatro tipos de mensaje |

## Mensajes y componentes

| Mensaje | Número | Canal | Para qué se usa |
|---|---|---|---|
| `PVP_MODE`, `PVP_HIT`, `PVP_HURT` (de upstream) | 241, 242, 243 | el de upstream | el PvP de la sesión; nuestras reglas cambian la cantidad en `PVP_HIT` y el resultado en `PVP_HURT` |
| `ROOM_SWITCH` | 0xF008 | 0 | interruptores de sala: clave 2 "Fijar aliados", clave 3 cuerpos (12-bodies). La clave 1 (nuestro antiguo PvP) no se activa nunca desde test10, así nadie recibe el golpe dos veces |
| `PLAYER_HIT` | 0xF00A | 0 | nuestro mensaje de golpe más antiguo; no se envía mientras el PvP de upstream es el de la sesión |
| `PLAYER_CALL` | 0xF00B | 0 | `/traer` (tipo 1) y `/reunir` (tipo 2) |
| `PLAYER_RENAME` | 0xF00C | 0 | `/nombre`; resultado 0 correcto, 1 en uso, 2 no válido |

## Pruebas

| Prueba | Qué comprueba |
|---|---|
| `tests/orm/pvp_test.cpp` | Avisos de interruptores (también a quien entra tarde), cantidades por arma, los veredictos de la víctima (invulnerable, repetición, escudo de frente y por detrás, Nayru en otra ranura, demasiado lejos, solo empujón, último en pie), las tres vías del escudo con los nombres reales de comandos AS, el empujón, cuerpos, la bomba sin copia |
| `tests/orm/commands_test.cpp` | Nombres y alias del registro únicos, chat frente a comando, cada escritura de on/off, tipos de argumento, permisos (anfitrión, caído, sin conexión), `/ayuda`, nombres de jugadores, colores, cambios de nombre, ediciones de `coop.ini`, sugerencias y el ciclo Tab / flechas / Enter |
| `tests/orm/stats_test.cpp` | El archivo de preferencias, líneas de la esquina, informes, fps y kbps |
| `tests/orm/pvp_codec_test.cpp` | Bytes, ida y vuelta y rechazos de los cuatro mensajes; nadie se golpea a sí mismo |
| `tests/orm/pvp_server_test.cpp` | Cualquiera cambia un interruptor y todos lo oyen, quienes entran tarde lo reciben; golpes solo con el PvP encendido y solo a la víctima; frecuencia; reglas de `/traer` y `/reunir`; nombres en uso; las caídas por PvP no terminan de inmediato la partida de la sala |
| `tests/orm/host_reader_test.cpp` | Cada ranura AS leída de memoria (la vía de comandos del escudo) |

Escenarios de autotest en el árbol (`tools/autotest/scenarios/`, se ejecutan con el `run.ps1` de upstream):

| Escenario | Qué comprueba |
|---|---|
| `orm-pvp.steps.txt` | Reproduce `pvp-espada`, `pvp-fijar` y `pvp-choque` seguidos (renombrado desde `pvp.steps.txt` en test10: el escenario `pvp` propio de upstream tomó el nombre) |
| `pvp-espada.A/B.txt` | A gira junto a B con el PvP apagado (nada), enciende el PvP y gira (B herido y empujado), luego B levanta el escudo (bloqueado) |
| `pvp-escudo.A/B/steps.txt` | B se pone el Escudo Deku; A gira tres veces: B de cara a A con ZL (bloqueado), de espaldas (recibe), sin escudo (recibe); el registro nombra la vía del escudo |
| `pvp-fijar.A/B.txt` | `/fijar on`, ZL, `/fijar off`, ZL: solo los avisos y el ZL de A; la fijación en sí no se comprueba |
| `pvp-choque.A/B.txt` | A camina hacia B y vuelve; ninguno atraviesa al otro (12-bodies) |
| `pvp-bomba.A/B/steps.txt` | La bomba de A hiere a ambos Links una vez cada uno, en sus propios juegos; B no registra ninguna línea "no copy here" |
| `pvp-cornisa.A/B/steps.txt` | Un giro de A empuja a B desde una cornisa. **Todavía no se puede ejecutar**: falta rellenar la posición de la cornisa de B |

## Líneas de registro

| Línea | Dice |
|---|---|
| `coop: commands: on (/ayuda); pvp on (push on, bodies on (the puppets drawn aside), bombs with no copy on); stats in orm_stats.txt` | los interruptores tal como se leyeron, una vez |
| `coop: commands: /<command> '<argument>'` | se ejecutó un comando |
| `coop: pvp: our hit (<command>, cause N) takes N quarter hearts, not upstream's N` | la cantidad real del arma del atacante sustituyó a la de upstream |
| `coop: pvp: our rules on P1's hit (2 quarter hearts by upstream's): blocked, takes 0; shield up (command 'ShieldGuard' in slot 0), facing the hit (cos 0.97)` | el veredicto de la víctima y la vía del escudo (`command '...' in slot N`, `the guard button ...`, `no shield equipped`) |
| `... (no fresh shield reading: main command only)` | sin lectura reciente del escudo en este fotograma: la regla más antigua de la ranura principal |
| `coop: pvp: shield read from N AS slots of our Link, the equipped shield (<actor>) and the guard button 0x... (LICHII_ORM_PVP_GUARD)` | una vez por partida |
| `coop: pvp: a mate's bomb went off N m from us with no copy here: N quarters` | la alternativa de explosión |
| `coop: pvp: life N -> N` | vida escrita |
| `coop: pvp: lock-on puppets in the puppet pack: yes (CoopPuppet_Child_Lock, CoopPuppet_Adult_Lock)` / `no (an older pack)` | la comprobación del paquete |
| `coop: check: ally target ok (...)` / `coop: check: ally target fallback (...)` | "Fijar aliados" creó una marioneta con fijación, o recurrió a la normal durante la sesión |
| `coop: commands: /nombre <name>: ...`, `coop: commands: coop.ini <key> = <value>` | cambio de nombre y `/color` guardados |
| `coop: commands: /registros: ...`, `coop: commands: /reintentar: connection ..., downloads ...` | los comandos del canal de la app |
| Servidor: `player N set switch K on`, `player N fell to PvP: up for the group defeat for 15 s`, `player N renamed: A -> B` | lado del servidor |

## Estado

| Parte | Estado |
|---|---|
| Daño, invulnerabilidad y empujón sobre el PvP de upstream | confirmado en partida en test10 (escenario `pvp-espada`, parte de `orm-pvp`) |
| Escudo | todavía no confirmado en partida: la ejecución de `pvp-espada` recibió un golpe que B quería bloquear (B no llevaba escudo y nuestra regla no conocía los nombres reales del comando de guardia). Corregido con la lectura del escudo por tres vías; las ejecuciones de `pvp-escudo` que siguieron no pasaron |
| Comandos de chat | confirmado en partida en test10 para los comandos que escriben los escenarios: `/pvp` e `/ir` (`pvp-espada`), `/choque on` (`choque-bloqueo`, `choque-encima`, `choque-continuar`); los demás comandos y la lista con Tab no se comprobaron en una ejecución |
| Fijar aliados | todavía no confirmado en partida (nunca se ha visto Z sobre una marioneta con fijación) |
| Llamadas, cambio de nombre, estadísticas | todavía no confirmado en partida |
| Alternativa de bomba, empujón en cornisa | todavía no confirmado en partida |

## Carencias conocidas

- La pausa de 0,8 s de upstream tras un golpe también se aplica después de un golpe que nuestro escudo bloqueó.
- Con el `LICHII_COOP_PVP_REPLAY` de upstream (la reacción al golpe del propio juego) encendido, el retroceso del juego y nuestro empujón pueden sumarse; no se ha mirado en una partida.
- Los alcances y los daños son estimaciones, a ajustar jugando.
- Se espera que escribir la vida de Link a 0 lo derribe (04-downed); no se ha visto con PvP.
- `pvp-cornisa` necesita una posición de cornisa antes de poder ejecutarse.
- `/registros` y `/reintentar` necesitan la app en el mismo proceso (14-app); sin ella lo dicen en el chat.
- `/hora` muestra la última hora que envió un juego, no el reloj del propio juego.
