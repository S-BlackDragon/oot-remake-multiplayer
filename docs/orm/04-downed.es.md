[English](04-downed.md) | **Español**

# 04-downed: Caído, reanimación y derrota de grupo

Con otro jugador en la sala, un Link al que se le acaban los corazones queda tendido en el suelo en vez de recibir el game over, y un compañero que mantenga A a su lado durante 3 s lo levanta. Cuando todos caen a la vez, el game over se ejecuta para todos (derrota de grupo). Alrededor de ese núcleo: un hada en tu propia botella funciona como en el original, una cámara de espectador mientras estás en el suelo, el grupo conserva su estado al pulsar Continuar, y un sonido opcional cuando cae un compañero. Las reglas siguen a nuestro anterior cooperativo lockstep para el juego de N64 ("caído, no muerto"; nadie muere y simplemente vuelve).

## Qué cambia para los jugadores

| Situación | Qué ocurre |
|---|---|
| Solo en la sala (o sin sesión cooperativa) | La muerte y el game over propios del juego, sin cambios. |
| Un compañero en la sala, se acaban los corazones | Link queda tendido donde cayó. Arriba de la pantalla: "Has caído: un compañero puede revivirte (A 3 s a tu lado)". Los compañeros reciben un aviso de chat "<name> ha caído: ve a su lado y mantén A". |
| Un compañero está a menos de 1,5 unidades y mantiene A | En su pantalla aparece "Mantén A para revivir a <name> n %", en la tuya "<name> te está reviviendo n %". Tras 3 s te levantas donde caíste (la animación de reanimación con hada del propio juego, sin recarga). |
| Vida tras la reanimación de un compañero | Al menos 3 corazones o el 25 % de tu máximo, lo que sea mayor, redondeado hacia arriba a un corazón entero. |
| Tienes un Hada real en una botella propia | Te levanta al instante, como en el original; nunca estás "caído" y nadie tiene que venir. |
| El último compañero se va mientras estás en el suelo | 10 s para que vuelva uno ("Has caído y no queda nadie: game over en n s"), y después tu propio game over. |
| Tu propia conexión se cae mientras estás caído | Sigues caído hasta 60 s; tras la reconexión sigues caído y se te puede reanimar. |
| Todos caídos a la vez durante 2 s | Derrota de grupo: "Todos habéis caído: game over", el game over se ejecuta en todas las partidas y cada jugador pulsa Continuar. |
| Tras un game over de grupo (Continuar) | Por defecto el grupo sigue con lo que tenía cuando cayó: puertas, cofres, interruptores, llaves, capacidad de corazones, historia, y la cartera y la munición de cada jugador se reescriben sobre lo que la recarga deshizo. La vida está llena y estás en la entrada de la zona, como en el original. |
| Mientras estás caído (cámara de espectador) | Tu vista gira suavemente hacia el compañero en pie más cercano y lo sigue; cruceta derecha/izquierda elige el siguiente/anterior compañero; la pantalla dice "Mirando a <name>". Cuando te levantas, la cámara vuelve a ser tuya. |
| Un Link caído ante una puerta de jefe | La sala del jefe lo recibe junto con el grupo, todavía tendido; un game over dentro de una sala de jefe te devuelve a ella (detalles en [05-bosses](05-bosses.es.md)). |
| Cae un compañero (opcional) | Una nota grave de ocarina (0,7 s) en su marioneta si está a menos de 20 m en tu escena, y si no en la interfaz. Desactivado en el kit. |

Los textos dirigidos al jugador están en español (es el idioma en que juega nuestro grupo); están en `coop/host/src/downed.cpp` (`banner_text`, `mate_down_notice`) y `lichii/hook/coop_spectator.cpp`.

## Interruptores

| Variable | Valor por defecto del kit (coop-options.ini) | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_COOP_DOWNED` | `LICHII_COOP_DOWNED=1` | desactivado (`coop_downed_game.cpp`, `coop_svc.cpp`) | on/off (`0/1/true/false/yes/no/on/off`) | Toda la función. También parchea en `main` la compuerta del game over al cargar. |
| `LICHII_ORM_HOOKSHOT_GATE` | no está en el kit | desactivado (`coop_downed_game.cpp`, solo compilaciones de Hookshot) | on/off | Una compilación de Hookshot no tiene parche de código, así que no hay compuerta: allí `LICHII_COOP_DOWNED=1` se desactiva solo, salvo que la compilación lleve el hook de la compuerta y lo indique con este interruptor. |
| `LICHII_COOP_SPECTATOR` | `LICHII_COOP_SPECTATOR=1` | desactivado (`coop_camera_probe.cpp`) | on/off | Cámara de espectador (necesita `LICHII_COOP_DOWNED=1`). |
| `LICHII_COOP_SPECTATOR_WAKE` | comentado: `# LICHII_COOP_SPECTATOR_WAKE=none` | `stick` | `stick`, `bits`, `both`, `none` | Cómo se despierta la cámara del juego tras una carga: `stick` da un breve empujón derecha-izquierda en el mando emulado mientras Link está de pie, si el jugador no ha tocado el stick derecho en 3 s (la vista gira unos 5 grados, una vez por carga). Cualquier otro valor mantiene el predeterminado y registra una línea. |
| `LICHII_COOP_CAMERA_YAW_RULE` | comentado: `# LICHII_COOP_CAMERA_YAW_RULE=1,180` | `1,180` | `<1 or -1>,<offset>` | Cómo la palabra de guiñada (yaw) de la cámara se convierte en el rumbo de la vista (depuración). |
| `LICHII_COOP_DEBUG_CAMERA2` | comentado | desactivado | on/off | Líneas de sondeo de la cámara (depuración). |
| `LICHII_ORM_CONTINUE` | comentado: `# LICHII_ORM_CONTINUE=save` | `keep` (`coop_continue.cpp`) | `keep`, `save`, off (`0/false/no/off`); `1/true/yes/on` = `keep` | Tras un game over de grupo: `keep` reescribe los valores del grupo sobre la recarga; `save` sigue en cambio la recarga del anfitrión (enviada como `FLAG_RESET`); off: las reglas del upstream. No hace nada sin `LICHII_COOP_DOWNED=1`. |
| `LICHII_ORM_MATE_DOWN_SOUND` | no está en el kit | desactivado (`orm_ocarina_sound.cpp`) | on/off | La nota de compañero caído. |
| `LICHII_ORM_MATE_DOWN_NOTE` | no está en el kit | `LowB` | un nombre de nota de ocarina del juego | Qué nota se toca. |
| `LICHII_COOP_BOSS_PULL_DOWNED`, `LICHII_COOP_BOSS_RESPAWN` | comentado (se muestra `=0`) | igual que `LICHII_COOP_DOWNED` (`coop_boss_game.cpp`) | on/off | Reglas de la sala de jefe para jugadores caídos; véase [05-bosses](05-bosses.es.md). |
| `LICHII_COOP_DEBUG_LIFE` | comentado | desactivado | on/off | La vida y el estado de tu Link tal como los lee el cooperativo (depuración). |

Parámetros del servidor (campos de `SessionConfig` en `server/session.hpp`, valores por defecto compilados, sin opción de línea de comandos): `revive_hold_us` = 1 s (un progreso de REVIVE mantiene a un jugador caído contado como en pie durante ese tiempo; 0 lo desactiva).

## Cómo funciona

### La compuerta

El juego inicia su evento de game over desde la actualización de Link con `bl 0x4aa890` en `main+0x4ae6bc`. Con `LICHII_COOP_DOWNED=1` esa llamada se parchea al cargar para saltar a una pequeña cueva de código en `main+0x3c46800` (`lichii/hook/coop_downed.s`, bytes en `coop_downed.h`). La cueva lee una palabra en `main+0x3c46880`: 0 significa abierta (llama a la función real), distinto de cero significa cerrada (devuelve 0, sin game over). Mientras la palabra está cerrada, un Link muerto yace en `DeadRagdoll` y su componente Death dice muerto.

Para levantarlo, se pone un Hada en una botella (una real si la hay, si no una botella vacía que se posea, si no una botella simulada llena) y se abre la compuerta: la propia rama de hada del evento de game over lo reanima y gasta el hada. Después la botella se deja como estaba. Una botella con cualquier otra cosa dentro nunca se usa.

### Archivos

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/downed.hpp`, `src/downed.cpp` | El lado portable, sin emulador: lee la vida, la vida máxima y la palabra Death (`read_vitals`), lee y escribe las cinco botellas, elige la botella (`pick_bottle`), la máquina de estados local `Downed` (alive, downed, reviving, game_over, fairy), el lado del compañero `ReviveWatch` (A mantenida 3 s a menos de 1,5 unidades de un compañero caído), `revive_life`, `write_life`, los textos de pantalla. |
| `lichii/hook/coop_downed.h`, `coop_downed.s` | La cueva y sus direcciones. |
| `lichii/hook/coop_downed_game.h/.cpp` | El pegamento con el emulador, una vez por fotograma: lee a Link y las botellas, avanza `Downed`, escribe la palabra de la compuerta, las botellas y la vida, activa el bit 4 de `PLAYER_STATUS`, mantiene callado al vigilante de banderas sobre las banderas de botellas mientras las escribe, ejecuta la vigilancia de reanimación con el mando del jugador 1 (incluida la entrada de prueba con guion), pone en cola `REVIVE` y `GROUP_DEFEAT` desde el hilo de red, la línea de pantalla y los avisos de chat, el sonido de compañero caído. |
| `coop/host/include/coop/host/continue_load.hpp`, `src/continue_load.cpp` | `ContinueLoad` (idle, game_over, reloading, idle) y `parse_continue_mode`. Desde el game over hasta que la recarga de Continuar se ha asentado, esa recarga cuenta como una carga de guardado; en `keep` cada bandera compartida que mueve se reescribe al valor del grupo, y la cartera y la munición (tomadas una vez, en el primer fotograma del game over) se reescriben con `FlagStore::write_own`. Termina solo cuando se cierra una ventana de carga abierta tras el game over con Link en pie y el evento terminado (o tras 10 s sin ventana, p. ej. un hada). |
| `lichii/hook/coop_continue.h/.cpp` | Pegamento, llamado desde `CoopDowned::Frame`. |
| `lichii/hook/coop_spectator.h/.cpp` | Reglas del espectador sin código del emulador: a quién seguir (el compañero en pie más cercano de nuestra habitación, anillo de la cruceta, lo sigue a otra habitación de la escena; otra escena: la vista se mantiene y la línea dice dónde está durante 10 s), giro suave (yaw 180 grados/s, pitch 90 grados/s), el empujón de despertar, liberación cuando ya no está caído. |
| `lichii/hook/coop_camera_probe.h/.cpp` | Encuentra el objeto de la cámara del jugador (yaw `+0x1a4`, pitch `+0x18c`) y aplica la salida del espectador. |
| `lichii/hook/coop_camera_aim.h/.cpp` | Comprobaciones de cámara y cálculos de yaw/rumbo que usa el sondeo. |
| `coop/host/include/coop/host/mate_down_sound.hpp`, `src/mate_down_sound.cpp` | Una vez por caída, solo para un compañero visto en pie antes; varias caídas en 2 s dan un solo sonido (el más cercano); ninguno durante nuestro propio game over. |
| `lichii/hook/orm_ocarina_sound.cpp` | Toca la nota de aviso en el usuario de sonido `Ocarina` del juego, sobre un handle que ningún jugador usa, liberado a los 0,7 s. |

### Puntos de enganche en archivos del upstream

| Archivo | Enganche `// ORM` |
|---|---|
| `lichii/hook/coop_svc.cpp` | `PatchDownedGate`: el parche de la compuerta al cargar, junto al parche de la marioneta, solo con `LICHII_COOP_DOWNED=1`. |
| `lichii/hook/coop_hook.cpp` | `CoopDowned::OnEvent` con los mensajes, `CoopDowned::Frame` en cada fotograma (con el nombre del evento para Continuar), los avisos de chat, `DownedBanner()` para la línea de pantalla, el punto de alcance de peligros (hazards) alimentado con el estado de caído. |
| `lichii/hook/coop_window.cpp` | El widget `DownedBanner` (la línea de la ventana Qt cuando nuestro HUD dentro de la imagen está apagado). |
| `coop/host/src/flag_watcher.cpp` | `set_continue_load`, `set_continue_keep_extra`: la ventana de Continuar cuenta como una carga de guardado. |
| `coop/host/src/sampler.cpp`, `coop/host/include/coop/host/session.hpp` | Bit 4 de `PLAYER_STATUS` a partir de nuestro estado de caído, conservado a través de `Sampler::reset` (reconexión estando caído); `Session::set_continue_load`, `set_continue_keep_extra`. |
| `server/session.cpp` | `on_revive`: encamina `REVIVE` solo a un jugador que está caído y lo marca como retenido; `check_group_defeat` en `tick`: todos los jugadores de la sesión (dos o más) con el bit 4 durante 2 s seguidos, ninguno retenido, da un `GROUP_DEFEAT` a todos. Un jugador que cayó por PvP cuenta como en pie durante 15 s (`pvp_down_hold_us`). |
| `lichii/hook/coop_boss_game.cpp` | Lee `CoopDowned::State()` para las reglas de la sala de jefe. |

## Mensajes y componentes

| Mensaje | Número | Canal / emisor | Cuerpo |
|---|---|---|---|
| `REVIVE` | `0xF000` | cliente, retransmitido por el servidor solo al jugador caído | `u8` reanimador (0 desde un cliente), `u8` jugador caído, `u8` progreso: 0 a 100 mientras se mantiene A (cada 250 ms), 255 = la petición. 101 a 254 rechazados. |
| (reservado) | `0xF001` | nunca se envía | reservado para "revivido" |
| `GROUP_DEFEAT` | `0xF002` | solo el servidor, canal 0 | `u8` jugadores caídos (1 a 6), `u32` hora del servidor en ms. |

El bit 4 de `PLAYER_STATUS` (`status::dead`, el bit del upstream) es lo que cuenta el servidor. Se activa mientras se está caído o mientras corre el game over, y se desactiva desde el momento en que se toma una reanimación (para que el grupo nunca se parta mientras alguien se levanta).

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/host/downed_test.cpp` | La máquina de estados sobre memoria simulada: palabra Death, constantes vitales, elección y restauración de botella, intentos de la compuerta, regla en solitario, los plazos de gracia de 10 s y 60 s, reconexión estando caído, progreso, derrota de grupo (también durante una reanimación), hada propia (3 casos), vida tras una reanimación (cola y luego directa, como máximo 3 escrituras), mantenido caído a través de una recarga por cambio de edad y vuelto a poner caído. |
| `tests/host/continue_load_test.cpp` | `keep` y `save` para anfitrión e invitado, recargas hacia atrás y hacia delante, cartera y munición mediante `ItemFlagStore`, el orden de las ventanas (vida de vuelta antes o después de la ventana de carga, una ventana antigua no cuenta, ninguna ventana en 10 s), cambios de recuento de otros durante la recarga del anfitrión. |
| `tests/host/mate_down_sound_test.cpp` | Una vez por caída, cerca o en la interfaz, un sonido para varias, ninguno en nuestro game over. |
| `tests/orm/orm_ocarina_sound_test.cpp` | La nota de aviso en la marioneta, en la interfaz, liberada a los 0,7 s, desactivada. |
| `tests/lichii/spectator_test.cpp`, `tests/lichii/camera_aim_test.cpp` | Reglas de seguimiento con una cámara simulada: la más cercana, anillo de la cruceta con seis, habitaciones y pisos, otra escena, empujón de despertar, liberación. |
| `tests/server/session_test.cpp`, `tests/orm/server_session_test.cpp` | Derrota de grupo tras 2 s, un jugador retenido cuenta como en pie, el fin de la retención reinicia los 2 s, `revive_hold_us = 0`. |
| `tests/orm/protocol_codec_test.cpp` | Ida y vuelta (round trips) y rechazos de `REVIVE` y `GROUP_DEFEAT`. |

Escenarios del arnés (en `tools/orm/harness`): `caer-pareja` (A cae, B mantiene A a su lado, A es reanimado), `caer-solo` (una partida: el game over original), `caer-derrota` (caen ambos: `GROUP_DEFEAT`, Continuar en ambos), `caer-hada-propia` (hada propia), `caer-camara` (cámara de espectador con dos partidas), `caer-gate`, `caer-fairy`.

## Líneas de registro

- `coop: downed: on (LICHII_COOP_DOWNED): a dead Link stays down until a mate holds A beside him`
- En Hookshot sin la compuerta: `coop: check: downed fallback off on Hookshot: no game over gate in this build (needs the ENTRY_HOOK at main+0x4aa890 and LICHII_ORM_HOOKSHOT_GATE=1): the normal game over`
- Sala: `coop: downed: solo, gate open`, `coop: downed: gate shut` / `gate open`
- Transiciones, una línea cada una: `coop: downed: alive -> downed: ...`, `downed -> reviving: revive requested by P2`, `reviving -> alive: revived (by P2, life 12 of 12, ...)`, `alive -> fairy: a fairy in our own bottle ...`, `downed -> game_over: group defeat (2 down)`, `game_over -> alive: back after the game over`
- Lado del compañero: `coop: downed: P1 is down (PLAYER_STATUS dead)`, `holding A beside P1 (0.80 away)`, `coop: downed: REVIVE progress 41 % sent to P1`, `coop: downed: REVIVE sent to P1`
- Lado del caído: `coop: downed: REVIVE from P2: holding A beside us, 41 %`, `coop: downed: REVIVE from P2 (the request)`, `coop: downed: group defeat: GROUP_DEFEAT from the server (2 down, server time ... ms)`
- Servidor: `group defeat: all 2 players down`
- Texto en pantalla: `coop: downed: screen: '...'`
- Continuar: `coop: continue: keep: after a game over the group goes on with what it had when it fell ...` (o `save ...`, `off ...`), `coop: continue: the game over event is '...' (its name has GameOver)`, `coop: continue: our Continue's reload is done: N write(s) put the group's values from before it back (nothing sent)`
- Espectador: `coop: camera2: ... spectator on (LICHII_COOP_SPECTATOR): a downed Link's view turns to a mate, wake stick`, `coop: spectator: following P2 (nearest)`, `coop: spectator: the camera woke (the right-stick nudge) ...`, `coop: spectator: screen: '...'`
- Sonido: `coop: ocarina sound: a mate's fall plays the note LowB for 700 ms ...`, `coop: downed: sound: P2 is down: the sound at his puppet (5.0 away)`

## Estado

| Parte | Estado |
|---|---|
| Caído y reanimación por un compañero | confirmado en partida en nuestra compilación basada en test9 (escenario `caer-pareja`, 8/8, también con A entrando en segundo lugar) |
| Solo: el game over original | confirmado en partida, una partida (escenario `caer-solo`) |
| Derrota de grupo | confirmado en partida en una compilación anterior (basada en test8) (escenario `caer-derrota`); no repetido en test10 |
| Hada propia en botella | confirmado en partida con una partida y un bot como compañero (escenario `caer-hada-propia`) |
| Cámara de espectador, seguimiento básico y despertar | confirmado en partida en una compilación anterior (basada en test8) (escenario `caer-camara`); la cruceta con 3 o más jugadores, habitaciones/pisos y otra escena: todavía no confirmado en partida |
| Continuar `keep` / `save` | todavía no confirmado en partida (`caer-derrota` ahora espera también las líneas de Continuar; no repetido) |
| Vida tras una reanimación por encima de 3 corazones, mantenido caído a través de una recarga por cambio de edad | todavía no confirmado en partida |
| Sonido de compañero caído | todavía no confirmado en partida (desactivado en el kit) |
| "Ser reanimado cuenta como estar en pie" (retención del servidor) | todavía no confirmado en partida |

## Carencias conocidas

- Hookshot: la compuerta es un parche de código, que Hookshot no aplica; allí la función está desactivada (game over normal) salvo que una compilación de Hookshot añada el hook y fije `LICHII_ORM_HOOKSHOT_GATE=1`. El empujón de stick del espectador no hace nada en Hookshot (lo dice una vez).
- La derrota de grupo es por sesión, no por escena: grupos en lugares distintos caen igualmente juntos.
- Las botellas y la magia no se conservan en un Continuar `keep` (son de cada jugador; solo se conservan la cartera y la munición).
- No se ha medido si el Continuar del remake recarga el autoguardado ni cómo interactúa con él el paso de "guardar en el game over" del juego.
- El alcance de la reanimación (1,5 unidades) no está medido en metros.
- El sonido de compañero caído usa el usuario de sonido de la ocarina porque es el único que el hook sabe tocar por nombre; para un sonido mejor hay que encontrar la llamada de otro usuario de sonido.
- La cámara del espectador gira desde el cuerpo caído; no puede pasar a otra escena con el compañero (el cuerpo tiene que quedarse donde se le pueda reanimar).
