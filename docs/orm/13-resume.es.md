[English](13-resume.md) | **Español**

# 13-resume: Volver a tu último lugar

Un jugador que cierra el juego y vuelve a la misma sala regresa a su propia última escena y lugar, no a donde la partida cargada coloca a todos. Un lugar que estaba en un combate de jefe o en una escena cinemática lo envía a la entrada de esa escena, y dentro de una mazmorra vuelve a la sala de entrada (el juego solo puede cargar una mazmorra por su entrada). Es la "reanudación de sesión" de nuestro cooperativo lockstep anterior para el juego de N64.

## Qué cambia para los jugadores

| Último lugar | Al volver |
|---|---|
| Juego libre en algún sitio, la misma escena que la carga | Link se mueve de vuelta a donde estuvo por última vez de pie ("De vuelta donde lo dejaste"). |
| Juego libre en otra escena | Un viaje hasta allí (el propio cambio de escena del juego al punto de inicio más cercano), y después Link se mueve al lugar. |
| En una sala de jefe o durante tu combate de jefe, o mientras corría una escena cinemática o una conversación | La entrada de esa escena; un combate de jefe que sigue en marcha te arrastra a él igual que a un jugador que llega tarde. |
| Dentro de una mazmorra, en una sala distinta de la sala de entrada | De vuelta en la mazmorra, en la sala de entrada, en el punto donde estuviste por última vez antes de salir de ella (el camino hacia el resto de la mazmorra): "De vuelta a la sala por donde saliste: tu sitio está en otra sala". Sin ese punto guardado (otro PC, un archivo antiguo): la entrada de la mazmorra. |
| Guardado con la otra edad, o una escena de la otra edad | Te quedas donde te puso la carga. |
| No hay nada guardado | No pasa nada. |

La decisión se toma una sola vez, después de la carga, cuando Link ha estado quieto 3 s sin ningún evento, ninguna carga y ningún cambio de escena pedido por nadie. No se escribe nada sobre el lugar guardado antes de que esa decisión y su viaje estén hechos, de modo que un fallo por el camino conserva el lugar antiguo.

Aparte, cuando un ayudante avisa al juego de que el guardado de la sala se almacenó mientras se jugaba, "Guardado" se muestra durante 2 s abajo a la derecha del HUD dentro de la imagen (ver [14-app](14-app.es.md) y [10-hud](10-hud.es.md)).

## Interruptores

| Variable | Valor por defecto del kit (coop-options.ini) | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_RESUME` | no está en el kit | activado (`coop_resume_game.cpp`) | on/off | Desactivado: no se guarda ningún lugar, no hay camino de vuelta. |
| `LICHII_ORM_RESUME_PLACE` | no está en el kit | sin definir: el `coop-resume.txt` de este PC | una línea de lugar (tomada como nuestra sea cual sea su sala y nombre), varias líneas separadas por `\|` (una por nombre de jugador, para una pareja del arnés), o `none` | El lugar al que volver, dado desde fuera (un ayudante que guarda lugares de varios PC). Un valor sin ninguna línea buena de este jugador se ignora, con una línea en el registro. |
| `LICHII_ORM_ROOM` | no está en el kit | sin definir: la dirección del servidor de `coop.ini` (`server:port/path`) | texto | El nombre de la sala bajo el que se guarda el lugar. |
| `LICHII_COOP_TELEPORT` | no está en el kit | activado (interruptor de upstream) | `0` = desactivado | La reanudación viaja con el viaje de `/teleport`; con él desactivado, solo es posible un movimiento dentro de la misma escena. |

## Cómo funciona

### El archivo

`coop-resume.txt`, junto a `coop.ini`, contiene dos líneas de comentario y:

- la línea de lugar: `v1;room;name;scene;x;y;z;age;kind;unix_s` (age `child`, `adult` o `-`; kind `ok`, `boss` o `event`);
- en una mazmorra, el punto de la sala de entrada: `d1;room;name;scene;start;x;y;z;age;unix_s` (un ayudante que solo pasa líneas `v1` lo deja en este PC).

Los campos se limpian (`;`, `#` y los caracteres de control pasan a `_`, como máximo 64 bytes). El lugar se escribe de nuevo cuando cambia la escena o el kind (como máximo una vez por segundo) o tras un movimiento de más de 1 unidad (como máximo cada 5 s).

### Dentro de una mazmorra (incidencia 197 de upstream)

Una mazmorra es una escena cargada de sala en sala, y el cambio de escena del juego solo puede aterrizar en un punto de inicio; todas las mazmorras tienen sus puntos de inicio utilizables solo en la sala de entrada (los demás inician escenas cinemáticas: jefe, sabio, piedra). Escribir el lugar de Link en una sala que no está cargada lo dejaba atascado en una imagen negra, así que upstream rechaza un movimiento fuera de la sala en la que está Link (`CheckMove`, con el `coop_rooms.txt` del paquete de marionetas, o 10 m sin él). Así pues, mientras se juega el juego guarda el último punto de la sala de entrada en el que estuvo Link, y al volver viaja a la entrada y mueve a Link a ese punto. El movimiento se comprueba de nuevo contra el lugar donde la carga lo puso realmente.

### Archivos

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_resume.h/.cpp` | Las reglas, sin código del emulador: `Place` y `Door`, `Format`/`Parse`/`FileText`/`FromFile`/`FromGiven`, `Recorder` (qué guardar y cuándo), `DoorAt` (el punto de la sala de entrada), `Returner` (la única decisión tras la carga: nada, mover o viajar, con la línea de registro y el aviso en el HUD). |
| `lichii/hook/coop_resume_game.h/.cpp` | El lado del hook: lee los interruptores y `coop.ini`, lee y escribe `coop-resume.txt` (a través de un archivo temporal), la tabla de salas del paquete de marionetas, una vez por fotograma `CoopResume::Frame`. |

### Puntos de enganche en archivos de upstream

| Archivo | Enganche `// ORM` |
|---|---|
| `lichii/hook/coop_teleport_game.cpp` | Llama a `CoopResume::Frame` en cada fotograma; un `move` se convierte en una acción de viaje, un `travel` inicia `Trip::Resume`. |
| `lichii/hook/coop_teleport.h/.cpp` | `Trip::Resume`: el cambio de escena, y después nuestro propio lugar o el punto de la sala de entrada; sus líneas van al registro, no al chat; una conexión perdida no lo detiene, el `/teleport` de un jugador sí. |

## Mensajes y componentes

Ninguno. El lugar se queda en el PC (o se entrega mediante `LICHII_ORM_RESUME_PLACE`).

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/lichii/resume_test.cpp` | Una línea de lugar y vuelta; líneas erróneas; limpieza; la línea de esta sala y nombre; una línea dada o una pareja; el ritmo del registrador; kinds boss, fight y event; la misma escena mueve, otra escena viaja (al lugar o a la entrada); nunca entre edades ni a una escena sin punto de inicio; la decisión espera a que Link se quede quieto, una sola vez; renunciar; el viaje (cambio de escena y luego lugar, solo la entrada, terminado por `/teleport`, renunciar va al registro). Mazmorra: el punto solo está en la sala de entrada, la ida y vuelta de la línea `d1`, viaje al punto o a la entrada, la misma mazmorra, el viaje. |

Escenarios del arnés (en `tools/orm/harness`): `reanudar-mazmorra-1` (B camina hasta un punto de la sala de entrada del Árbol Deku, luego se le coloca con un comando de prueba en la repisa de la resortera en otra sala, y conserva lugar y punto) seguido de `reanudar-mazmorra-2` con las mismas instancias (B vuelve: viaje a la entrada del Árbol Deku, después el punto de la sala de entrada, nunca movido a la repisa); `reanudar-sitio` (el lugar de A en la sala de Gohma va a la entrada del Árbol Deku; B vuelve a su lugar). El arnés borra `coop-resume.txt` entre ejecuciones salvo que `ORM_KEEP_RESUME=1`.

## Líneas de registro

- `coop: resume: off (LICHII_ORM_RESUME=0): no last place kept, no way back`
- `coop: resume: on: the last place of 'B' in room <room>: InsideTheDekuTree at x y z (ok, this PC's coop-resume.txt)` (o `given by the app`), o `coop: resume: on: no last place of 'B' in room <room> (...)`
- `coop: resume: the spot of InsideTheDekuTree's entrance room last stood on: x y z (start point Entrance, this PC's coop-resume.txt)`
- `coop: resume: dungeon rooms: N placements in the puppet pack's table (...)` o `no room table in the puppet pack (moves within 10 m of the entrance only)`
- La decisión: `coop: resume: going back to the last place, ... (start point ...)`, `coop: resume: back to the last place, ... (same scene)`, `coop: resume: already at the last place (...)`, `coop: resume: the last place (...) is not a place to go back to (...)`, `coop: resume: the last place (...) is in another room of this dungeon (issue 197 ...)`, `... was kept with the other age: staying here`, `coop: resume: no last place kept for this player in this room`, `coop: resume: our Link never stood still after the load: no way back to the last place`
- El viaje: `coop: resume: scene change to InsideTheDekuTree at Entrance`, `coop: resume: at the spot of the entrance room of InsideTheDekuTree we last stood on before leaving it`, `coop: resume: not moved to ... from where the load put us (...): staying here`
- Después: `coop: resume: keeping our last place in <file>`, `coop: resume: kept ...`
- HUD: `coop: hud: "Guardado" for 2000 ms (the app sent the room's save)`

## Estado

| Parte | Estado |
|---|---|
| Dentro de una mazmorra: de vuelta al punto de la sala de entrada | confirmado en partida en test10 (escenarios `reanudar-mazmorra-1` y luego `reanudar-mazmorra-2`, dos partidas, 10/10) |
| Fuera de las mazmorras: de vuelta al lugar exacto, viaje a otra escena | todavía no confirmado en partida (`reanudar-sitio` no se ha ejecutado) |
| Lugar en sala de jefe o escena cinemática: la entrada | todavía no confirmado en partida |
| "Guardado" en el HUD | todavía no confirmado en partida |

## Carencias conocidas

- Un lugar en una sala de mazmorra distinta de la de entrada nunca se alcanza exactamente: el juego no tiene ninguna petición que conozcamos para su propio cambio de sala (una puerta).
- El lugar se guarda por nombre de sala y nombre de jugador; renombrar a un jugador o cambiar la dirección del servidor (sin `LICHII_ORM_ROOM`) empieza sin ningún lugar.
- No comprobado en una partida: que el viaje de reanudación no choque con la invitación de la canción de teletransporte ni con las reglas de la sala de jefe.
- Entre PC, el lugar solo viaja si algo lo entrega mediante `LICHII_ORM_RESUME_PLACE`; el kit solo usa el archivo de este PC.
