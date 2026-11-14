[English](02-network.md) | **Español**

# 02-network: Seis jugadores, nuestro bloque de mensajes, WebSocket, un anfitrión

La parte de red de nuestro trabajo: hasta seis jugadores en una sesión, un bloque de números de mensaje para todo lo que añadimos (0xF000 a 0xF0FF) y la versión de protocolo 110, una interfaz `Transport` bajo el cliente para que el mismo protocolo corra sobre ENet/UDP o WebSocket (`ws://`, `wss://`), avisos sobre el propio enlace del jugador, los ajustes del grupo, la elección de anfitrión por el relé y la gestión de interés para las poses con seis jugadores.

La contraseña de unión y el cifrado del tráfico de upstream (test10) están por encima de nuestro `Transport`: `Client` sella y abre, así que UDP, `ws` y `wss` llevan el mismo paquete sellado por el mismo canal.

## Qué cambia para los jugadores

- Hasta **seis** jugadores en una sesión. Los jugadores 5 y 6 reciben morado y naranja por defecto (del 1 al 4 conservan el azul, rojo, verde y amarillo de upstream).
- Una partida puede unirse por WebSocket: `server = ws://your-server.example:27511/` o `wss://your-server.example/path` en `coop.ini`. Útil donde UDP está bloqueado o detrás de un proxy web.
- Cuando un enlace no da abasto (fotogramas perdidos al subir o al bajar, o el servidor descartando mensajes de un jugador por pasarse de su tasa), se avisa al jugador en el chat y en el registro, una sola vez, y se le vuelve a avisar cuando todo va bien. Cuando el servidor es de otro protocolo, el rechazo dice cuál.
- Un anfitrión para toda la sala, elegido por el servidor a partir de tiempos de ida y vuelta medidos (el promedio más bajo hacia los demás, enlace estable, partida dentro del mundo), con histéresis. Un jugador puede elegir a mano con `/anfitrion <name>` (o `/host`), y `auto` devuelve la elección al servidor. Cada cambio es un aviso en el chat ("quién hospeda y por qué").
- Los ajustes del grupo: amiibo y ZELDA NOTES siguen el valor del anfitrión mientras juegas como invitado (el tuyo vuelve después). Un interruptor cooperativo que difiere del del anfitrión se avisa en el chat a ambos lados, una sola vez.
- Nada visible de la gestión de interés: una partida envía la pose completa de su Link solo mientras otro jugador está en su mismo lugar, y el servidor reenvía las poses solo a los jugadores que están en el lugar del emisor.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_COOP_TRANSPORT` (clave `transport` de `coop.ini`) | no está en el kit | `udp` (`coop_settings.cpp`) | `udp`, `ws`, `wss` | El medio de transporte. Una dirección `server =` con esquema también lo fija |
| `LICHII_COOP_SERVER` (clave `server` de `coop.ini`) | la escribe el lanzador | ninguno | `host`, `host:port`, `udp://host[:port]`, `ws://host[:port][/path]`, `wss://host[:port][/path]` | Puertos por defecto: 27510 udp, 27511 ws, 443 wss. Una ruta requiere `ws://` o `wss://` |
| `LICHII_COOP_PATH` (clave `path` de `coop.ini`) | no está en el kit | `/` | una ruta que empiece por `/` | La ruta de WebSocket |
| `LICHII_ORM_HOST` | no está en el kit | `auto` (`coop_host_choice.cpp`) | `auto`, `me`, `never` | Nuestro deseo de hospedar, enviado tras cada `WELCOME` |
| `LICHII_ORM_HOST_FILE` | no está en el kit (lo define un lanzador) | sin definir: ninguno | una ruta de archivo | El archivo donde el juego escribe quién hospeda (en cada cambio y cada 20 s), para un lanzador que guarda la partida de la sala con el anfitrión |
| `LICHII_ORM_GROUP_SETTINGS` | no está en el kit | activado salvo `0` (`orm_group_settings.cpp`) | `0` lo desactiva | Ajustes de grupo (`GROUP_SETTINGS`) |
| `LICHII_COOP_INTEREST` | no está en el kit | activado salvo `0` (`coop_hook.cpp`) | `0` lo desactiva | Enviar poses solo mientras otro jugador está en nuestro lugar |
| `server_websocket` (kit, solo anfitrión) | comentado (`# server_websocket = no`): activado | | `yes`, `no` | Los scripts del kit pasan `--ws-port <UDP port + 1>` o `--ws-port off` al servidor (véanse las carencias conocidas) |

`LICHII_ORM_GROUP_SETTINGS` y `LICHII_COOP_INTEREST` son anteriores al lector de interruptores compartido: solo se desactivan con el valor exacto `0`.

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `coop/protocol/include/coop/protocol/orm_types.hpp` | La tabla única de todo número que añadimos: tipos de mensaje (bloque 0xF000), ids de componente (24 a 31, los nuestros contando hacia abajo desde el 30), bits de `PLAYER_STATUS` (los nuestros desde el bit 7 hacia abajo), bits de estado de `OBJECT_SAMPLE` (desde el 7 hacia abajo) y los bloques que usa upstream. Los `static_assert` comprueban que los nuestros nunca se solapen con los de upstream. Un número nuevo se añade aquí primero, nunca como literal |
| `coop/protocol/include/coop/protocol/orm_status.hpp` | Nuestros bits de `PLAYER_STATUS`: `at_title` (7) y `age_waits` (6) |
| `coop/protocol/include/coop/protocol/orm_messages.hpp`, `src/orm_messages.cpp` | Cuerpos de `GROUP_SETTINGS`, `ROOM_SWITCH`, `PLAYER_HIT`, `PLAYER_CALL`, `PLAYER_RENAME`, mantenidos fuera de `codec.cpp` para que una nueva fijación de versión toque pocas líneas allí |
| `coop/protocol/include/coop/protocol/messages.hpp`, `src/codec.cpp` | `kProtocolVersion = 110`, `kMaxPlayers = 6`, `slot_color` 5 y 6; nuestras estructuras, tabla de canales (`check_received`) y tabla de emisores (`may_send`) |
| `coop/net/include/coop/net/transport.hpp` | La interfaz `Transport`: `start`, `resolve`, `connect(host, port, path, timeout)`, `send(channel, bytes)`, `service(wait)`, `flush`, `stats` (ida y vuelta, pérdida, MTU, bytes y fotogramas en cola, fotogramas descartados), y los ganchos de datagramas pequeños de upstream (`full_datagrams_lost`, `use_small_datagrams`, solo ENet) |
| `coop/net/src/enet_transport.*` | ENet sobre UDP, el código de upstream movido detrás de la interfaz (camino pequeño, reparación de datos en espera) |
| `coop/net/src/ws_transport.*` | WebSocket sobre TCP, o TLS y luego WebSocket para `wss://` (el almacén ROOT de Windows; las pruebas pueden fijar sus propias raíces). Un fotograma binario por mensaje de protocolo: el byte 0 es el canal, luego el mensaje. Los canales 0 a 2 son fiables (como máximo 16 MiB en cola); el 3 y el 4 guardan 64 fotogramas cada uno y descartan el más antiguo. Nagle desactivado. Un fotograma mayor que cualquier mensaje sellado termina la conexión |
| `coop/net/src/client.cpp` | Elige el transporte a partir de `transport` (`ws`/`wss` → `WsTransport`, si no `EnetTransport`); sella por encima de él; guarda los últimos 16 `LINK_NOTICE` para el juego; lee un aviso `server_protocol` antes de un `REFUSED` |
| `coop/host/orm_link_watch.*` | Decide cuándo avisar de un enlace y cuándo vuelve a estar bien, a partir de lo que mide el cliente (nuestros fotogramas descartados, bytes retenidos en alto durante 3 s, pérdida UDP mantenida en alto) y de `LINK_NOTICE`. Cada tipo llega al jugador como máximo una vez por minuto; el registro tiene todos los cambios |
| `coop/host/host_choice.hpp` (+ `lichii/hook/coop_host_choice.*`) | Lado del juego de la elección de anfitrión: `/anfitrion`, nuestro deseo, la palabra del relé al chat, el archivo del anfitrión |
| `coop/host/orm_group_settings.*` (+ `lichii/hook/orm_group_settings.*`) | Tres tipos de ajuste: `host_wins` (opciones del juego que traen cosas al mundo compartido: amiibo, ZELDA NOTES), `must_match` (interruptores cooperativos, comparados con los del anfitrión y avisados una vez), `local` (los propios de cada jugador, nunca enviados). Cada interruptor se lee como lo lee su propio código; los archivos de lista se envían como huella |
| `lichii/hook/coop_interest.*` | Solo dos niveles: otro jugador en nuestro lugar → poses a los 30 por segundo nativos, a cualquier distancia; nadie → ninguna pose. El lugar es la escena sin su época. Todo lo desconocido cuenta como aquí. `PLAYER_SAMPLE` va siempre a todos (el mapa de compañeros y el salto hasta un compañero lo usan) |
| `server/orm_interest.hpp` | El lado del servidor: un `PLAYER_POSE` va solo a los jugadores del lugar del emisor (escenas desconocidas: todos) |
| `server/host_choice.hpp`, `host_choice.cpp` | La elección de anfitrión del relé: hace ping a cada jugador cada 250 ms; puntuación = su propia ida y vuelta más la media de los demás; solo un jugador dentro del mundo con un enlace estable puede ser elegido por medición; uno mejor releva solo cuando es mejor por un margen durante un tiempo de espera, fuera de un periodo de enfriamiento; un anfitrión no apto se sustituye; los deseos (`me`, `never`) van antes que las mediciones; un nombre elegido a mano se mantiene hasta la siguiente elección, `auto`, o hasta que la sala se vacíe |
| `server/orm_host.cpp` | Manejo de `HOST_CHOICE` en la `Session`: deseo, elección (como máximo una por segundo y jugador), `told` a todos tras cada cambio y a quien se une, `refused` a quien eligió |
| `server/orm_session.cpp` | `GROUP_SETTINGS` (guardado por jugador, retransmitido, una repetición no se retransmite, enviado a quien se une empezando por el número más bajo), `ROOM_SWITCH`, `PLAYER_HIT`, `PLAYER_CALL`, `PLAYER_RENAME` (09-pvp) |
| `server/enemies.cpp` | Reglas de propiedad de enemigos con seis jugadores (05-bosses) |

### Puntos de enganche en archivos de upstream

| Archivo de upstream | Qué añadimos |
|---|---|
| `server/session.cpp`, `session.hpp` | Al unirse, después de lo de upstream: los eventos de historia reproducidos (07), el color de cada jugador, estados, ajustes de grupo e interruptores de sala. Retransmisión de poses mediante `orm::pose_goes`. PONG medidos y el ciclo de la elección de anfitrión. Un jugador que supera su tasa de mensajes se registra y se le envía `LINK_NOTICE over_rate`, como máximo una vez por minuto. Derrota de grupo, reanimación y gracia de PvP (04, 09); reglas de enemigos y jefes (05) |
| `server/enet_server.hpp` | El límite de conexiones es `kMaxPlayers` más uno, de modo que a un séptimo jugador se le dice que el servidor está lleno |
| `coop/net/client.cpp`, `client.hpp` | La división en `Transport`; el código de ENet movido a `enet_transport.*`; los límites de tamaño suman `kSealOverheadBytes`; `REJOIN_CLAIM` y los reintentos pasan por el `Transport` |
| `lichii/hook/coop_settings.cpp`, `.h` | `server =` con `udp://`, `ws://`, `wss://` y una ruta; claves `transport`, `path`; sus variables `LICHII_COOP_*` |
| `lichii/hook/coop_hook.cpp` | Interés, elección de anfitrión, ajustes de grupo y vigilancia del enlace conectados al fotograma y a los eventos de red |
| `coop/presence`, `coop/overlay` | Números de jugador hasta `kMaxPlayers`; el color `PLAYER_LOOK` de cada jugador guardado para el panel |

## Mensajes y componentes

Protocolo **110**. Nuestros tipos son 0xF000 + (número antiguo − 240): eran del 60 al 65 hasta el protocolo 108 y del 240 al 254 en el 109; test10 tomó del 241 al 255, así que todo el bloque se movió. Los tipos son `u16` en el cable.

| Tipo | Número | Canal | Enviado por | Lleva |
|---|---|---|---|---|
| `REVIVE` | 0xF000 | 0 sesión | cliente | Que un compañero mantiene A junto a un jugador caído: `other_player`, `progress` (0 a 100 %, 255 = hecho). El servidor lo pasa solo al jugador caído (04) |
| (reservado, "revived") | 0xF001 | | nunca se envía | |
| `GROUP_DEFEAT` | 0xF002 | 0 | servidor | Todos los jugadores (dos o más) muertos durante 2 s: jugadores caídos, hora del servidor (04) |
| `STORY_EVENT` | 0xF003 | 0 | cliente, servidor | Una escena de historia: tipo (`start`, `end`, `played` para quien se une, `skipped`), escena, nombre, posición, hora del servidor (07) |
| `PLAYER_LOOK` | 0xF004 | 0 | cliente, servidor | El color de un jugador (RGB) y un byte de estilo reservado |
| `OBJECT_POSE` | 0xF005 | 4 muestras de objeto | cliente, servidor | Los huesos de un habitante, en hasta 4 partes de 128 (11) |
| `GROUP_SETTINGS` | 0xF006 | 0 | cliente, servidor | Hasta 48 pares clave/valor de texto, con las claves en orden creciente |
| `OCARINA_NOTE` (nuestro, `OrmOcarinaNote`) | 0xF007 | 0 | cliente, servidor | Nota, suelta o canción, instrumento, serie (11) |
| `ROOM_SWITCH` | 0xF008 | 0 | cliente, servidor | Interruptores de toda la sala fijados en vivo: clave 1 PvP (ya no se fija: se usa el `PVP_MODE` de upstream), 2 fijar objetivo en compañeros, 3 cuerpos (09, 12) |
| `CHEST_OPEN` | 0xF009 | 0 | cliente, servidor | El hash de colocación de un cofre, escena, animación de apertura, hora (06) |
| `PLAYER_HIT` | 0xF00A | 0 | cliente, servidor | Nuestro golpe de arma sobre la marioneta de un compañero, solo a la víctima; desde test10 la base de PvP es el `PVP_HIT` de upstream (09 explica cómo encajan ambos) |
| `PLAYER_CALL` | 0xF00B | 0 | cliente, servidor | "Ven a mí" a un compañero, o el "reunirse" del anfitrión (09) |
| `PLAYER_RENAME` | 0xF00C | 0 | cliente, servidor | Un nombre nuevo, comprobado por el servidor (09) |
| `LINK_NOTICE` | 0xF00D | 0 | solo servidor, nunca retransmitido | Cuerpo fijo `u8 kind, u32 a, u32 b`: `behind` (porcentaje descartado, KiB en espera), `caught_up` (segundos de retraso), `server_protocol` (su versión, antes de un `REFUSED`), `over_rate` (no fiables, fiables descartados). Los tipos desconocidos se conservan y se ignoran |
| `HOST_CHOICE` | 0xF00E | 0 | cliente (`wish`, `pick`), servidor (`told`) | Quién hospeda y por qué (`first`, `left`, `measured`, `unfit`, `picked`, `wished`, `refused`), quién eligió, la ida y vuelta medida del anfitrión, el nombre fijado a mano |

Componentes y bits (ids sin cambios por el paso a 110):

| Dónde | Id o bit | Qué |
|---|---|---|
| Componente de `PLAYER_SAMPLE` | 30 `ride` | La plataforma sobre la que va un jugador y el desplazamiento sobre ella (11) |
| Componente de `PLAYER_EQUIP` | 30 `mount` | El caballo sobre el que va montado un jinete, por nombre de actor (11) |
| Componente de `PLAYER_EQUIP` | 29 `rupees` | La cartera propia del jugador, mostrada, nunca escrita en la partida de otro (06, 10) |
| Componente de `OBJECT_SAMPLE`, `OBJECT_SPAWN`, `HANDOVER_STATE` | 29 `carry` | Un objeto levantado en las manos de un jugador: quien lo sostiene, lugar, hueso (11) |
| Bit de `PLAYER_STATUS` | 7 `at_title` | En la pantalla de título o la selección de archivo (enviado con `loading`) |
| Bit de `PLAYER_STATUS` | 6 `age_waits` | La edad de la sala cambió y nuestro movimiento espera (08) |
| Bit de estado de `OBJECT_SAMPLE` | 7 `owner_keeps`, 6 `owner_keeps_near` | La partida del dueño conserva a un jefe durante un tiempo (05) |

## Pruebas

| Archivo de prueba | Qué comprueba |
|---|---|
| `tests/net/ws_transport_test.cpp` | `ws` y `wss` conectan y hacen el saludo; un fotograma vuelve con su canal y sus bytes; un fotograma mayor que cualquier mensaje termina la conexión; se informa de que el servidor cierra; se rechaza un certificado de otro nombre de host (18 casos, solo 127.0.0.1) |
| `tests/net/client_rtt_test.cpp` | Ceros de ida y vuelta antes del primer PONG, una mediana una vez que se responden los pings |
| `tests/orm/protocol_codec_test.cpp` | Cada uno de nuestros tipos: número, canal, formato, cuerpos erróneos rechazados; el componente `carry` omitido por un lector antiguo |
| `tests/orm/group_settings_test.cpp`, `group_settings_codec_test.cpp`, `group_settings_server_test.cpp` | Opciones e interruptores leídos como los lee su código; huellas de listas; un invitado juega con el amiibo del anfitrión y recupera el suyo; una diferencia avisada una vez en ambos lados; límites del códec; retransmisión, quien se une, quien se va |
| `tests/orm/host_choice_test.cpp`, `tests/server/host_choice_test.cpp` | Formato de `HOST_CHOICE`, canal, emisores; análisis de `/anfitrion` y `/host` y coincidencia de nombres; palabras de `LICHII_ORM_HOST`; las líneas del chat; en el servidor: contabilidad de PONG, enlace estable, la puntuación, espera, margen, enfriamiento, anfitrión no apto, deseos, elecciones (26 casos) |
| `tests/orm/link_watch_test.cpp` | Un enlace que da abasto no dice nada; las pérdidas de subida, los bytes retenidos y la pérdida mantenida avisan una vez y "vuelve a estar bien" tras el tiempo de calma; el parpadeo se avisa una vez por minuto; reinicio cuando no hay conexión |
| `tests/orm/net_client_test.cpp` | Nuestro color tras cada `WELCOME`; `LINK_NOTICE server_protocol` antes de `REFUSED`; avisos conservados (como máximo 16); un cliente nunca envía uno |
| `tests/server/orm_interest_test.cpp`, `tests/lichii/interest_test.cpp` | Lugar = escena sin época; una pose solo al lugar del emisor; todo lo desconocido cuenta como aquí; la pose del habitante sigue la misma regla |
| `tests/orm/server_session_test.cpp` | Seis jugadores recibidos como 1 a 6; la partida del anfitrión con seis; reglas de retransmisión de reanimación y derrota de grupo |
| `tests/server/session_fuzz_test.cpp` | Mensajes aleatorios y mutados (nuestros tipos 0xF000 y los de upstream hasta el 511) nunca hacen caer la sesión, nunca producen un mensaje erróneo, nunca suplantan a un emisor |
| `tests/server/orm_test_join.hpp` | Ayuda para que las pruebas del servidor se unan con la contraseña y el intercambio de claves de upstream |

Escenarios del arnés (vivirán en `tools/orm/harness`): `ajustes-amiibo`, `ajustes-distintos` (ajustes de grupo), `ancho-seis` (una partida y bots, ancho de banda con seis).

## Líneas de registro

| Línea | Cuándo |
|---|---|
| `coop: link: our uplink cannot keep up: N of our frames dropped in ...` | Nuestra subida descarta fotogramas |
| `coop: link: our uplink keeps up again (behind for ...` | Vuelve a estar bien |
| `coop: link: the server drops N% of the frames for us (...` / `... keep up again (behind for N s)` | `LINK_NOTICE behind` / `caught_up` |
| `coop: link: the server dropped N of our samples or poses and ...` | `LINK_NOTICE over_rate` |
| `coop: link: the server speaks protocol N` | `LINK_NOTICE server_protocol` |
| `player N (name) is over its message rate: ...; told the player` | Registro del servidor, un jugador por encima de su tasa |
| `coop: host: our wish is <wish> (LICHII_ORM_HOST), host file <file>` | Arranque |
| `coop: host: player N <name> hosts (<why>...)` | Se recibe `HOST_CHOICE told` |
| `coop: host: host file: <name> hosts...` / `coop: host: could not write the host file <file>` | Archivo del anfitrión escrito / fallido |
| `coop: settings: group settings on: amiibo and ZELDA NOTES follow the host, ...` | Ajustes de grupo activados |
| `coop: settings: group settings off (LICHII_ORM_GROUP_SETTINGS=0)` | Desactivados |
| `coop: settings: ours: ...` | Nuestros ajustes tal como se envían |

## Estado

- Protocolo 110 y el bloque 0xF000: confirmado en partida en test10 (dos partidas unidas con la contraseña de unión en los escenarios de cuerpos `choque-bloqueo`, `choque-encima`, `choque-continuar`).
- Transporte WebSocket: confirmado en partida en una compilación anterior (escenario `walk` con dos partidas por `ws://`, antes de test9); no repetido en test10.
- Seis jugadores: todavía no confirmado en partida con seis partidas reales (una ejecución con una partida y cinco bots unió a los cinco y mostró sus marionetas; otras comprobaciones de esa ejecución fallaron por motivos ajenos a la red).
- Elección de anfitrión, ajustes de grupo, avisos de enlace, gestión de interés: todavía no confirmado en partida (probado con pruebas unitarias).

## Carencias conocidas

- **El servidor de este árbol no tiene bucle de WebSocket.** `coop_server` solo habla ENet/UDP y no tiene la opción `--ws-port`, pero los scripts del kit (`start-server.ps1` hasta `orm-hosting.ps1`) siempre pasan `--ws-port <port>` o `--ws-port off`. Con el servidor de este árbol, o se quitan esos dos argumentos en `Get-OrmWsArgs` o se añade un bucle de WebSocket al servidor. El lado del juego (`ws://`, `wss://`) está completo.
- `LINK_NOTICE behind` y `caught_up` necesitan un servidor con su propia cola de envío por receptor que descarte los fotogramas no fiables antiguos; el servidor de este árbol solo envía `over_rate`. El juego maneja los cuatro tipos.
- La elección de anfitrión mueve el anfitrión del relé (banderas, transferencia de guardado, reloj); un lanzador que guarda la partida de la sala debe leer `LICHII_ORM_HOST_FILE`.
