[English](05-bosses.md) | **Español**

# 05-bosses: Jefes, enemigos y peligros con seis

Reglas para los combates con hasta seis jugadores, sobre la sala de jefe compartida de upstream ([dungeons-boss.md](../dungeons-boss.md)) y la propiedad de objetos ([objects.md](../objects.md)): familias de jefes que actúan sobre "el jugador" (agarres, tenis, el espejo de Twinrova), jugadores caídos en la puerta del jefe, enemigos que se vuelven hacia los jugadores que siguen en pie, golpes a un enemigo de otra partida que se sienten inmediatos, el final del juego (cuenta de la barrera, salas de Ganondorf y Ganon, la huida), la Fortaleza Gerudo y el Templo del Espíritu, y los peligros. Las reglas vienen de nuestro cooperativo anterior en lockstep para el juego de N64, convertidas en "actúa sobre el Link de su dueño, y su dueño no cambia mientras lo hace".

## Qué cambia para los jugadores

| Dónde | Qué pasa |
|---|---|
| Un Like Like, Wallmaster, Floormaster, ReDead, Gibdo, Dead Hand, el tentáculo de Morpha o la mano de Bongo Bongo agarra a tu Link | Ese enemigo se queda en tu partida hasta que te suelta: ni un golpeador ni un jugador más cercano se lo lleva. Los demás te ven colgado de su copia. El golpe de un compañero sigue liberándote como en el original (el Fuego de Din sobre el tentáculo, una espada sobre el Like Like). |
| Phantom Ganon, Ganondorf (tenis) | Mientras una bola de luz está en vuelo, el jefe no cambia de dueño a mitad del intercambio; solo la partida del dueño devuelve la bola, así que nunca hay doble devolución. |
| Twinrova | Mientras el hielo de Kotake sujeta a tu Link (o tu escudo espejo sostiene media carga), las hermanas se quedan con tu partida para que el hielo se derrita. |
| Un Link caído en la puerta del jefe | Se lleva a la sala del jefe con el grupo, todavía tumbado (una carga lo pondría en pie, así que se mueve tal como yace). |
| Una reanimación o un game over durante un combate de jefe | Nunca se toma como "jefe derrotado" (antes lo era cualquier evento de un jugador caído en el combate). |
| Game over dentro de una sala de jefe | La sala empieza de nuevo tras la recarga. Tras la recarga de Continuar, si estás en pie en la misma mazmorra pero fuera de la sala y el jefe no está derrotado, se te mueve de vuelta dentro (cada 3 s, 3 intentos). |
| Un compañero cae a mitad del combate | En 1 s los enemigos que era dueño pasan al jugador en pie más cercano. |
| Un dueño sale de la escena, descarga al enemigo o se desconecta | Sus enemigos pasan al jugador en pie más cercano, no a uno que esté caído, en pausa o cargando. |
| Seis alrededor de un Iron Knuckle, Stalfos, Lizalfos, Dinolfos o Wolfos | Se trata como un minijefe: combate con el jugador que tiene al lado (un jugador a menos de 10 m y 6 m más cerca que el dueño se lo lleva tras 3 s, no más a menudo que cada 8 s); cada golpe cuenta una vez, muere una vez. |
| Sales de una sala despejada mientras un compañero sigue en ella, y vuelves | Sus enemigos siguen muertos para ti (la sala "se quedó cargada"). Si todos salen, vuelven como en el juego. |
| Golpeas a un enemigo que es de otra partida | El retroceso, el sobresalto, el destello y el sonido de tu propia partida se ven al instante; la copia se mantiene ahí hasta que llega la respuesta del dueño y se funde de vuelta; sin segundo sobresalto, una copia que mataste no sigue andando, una respuesta lenta nunca la cura ni la vuelve a golpear. |
| Pruebas del Castillo de Ganon | Dos pruebas terminadas en el mismo instante en dos partidas cuentan las dos; la barrera de la torre cae con seis. |
| Ganondorf y Ganon | Sus salas son salas de jefe como las otras ocho (traslado, entrada tardía, introducciones omitidas cuando alguien ya las terminó). |
| El castillo que se derrumba | Una partida que sigue en la huida sigue a un compañero (en pie) que ya llegó al combate con Ganon. |
| Fortaleza Gerudo | Cada partida tiene sus propios guardias y solo el Link al que un guardia atrapa va a la celda; una cuenta de carpinteros perdida cuando dos jugadores liberan carpinteros a la vez en dos partidas se corrige sola tras 3 s. |
| Templo del Espíritu | Una escena que empieza un compañero solo la siguen los jugadores de su edad. |
| Lava, arenas movedizas, agua profunda, remolinos, el vacío | El calor, el aire y el frío de cada jugador son los de su propia partida (sin cambios). Si caes donde nadie puede alcanzarte, los demás ven tu cuerpo en el último suelo firme en el que estuviste y pueden reanimarte allí. |
| Desierto Encantado | El Poe guía corre en cada partida (antes, tu Poe seguía el camino de otro jugador). |

## Interruptores

| Variable | Valor por defecto del kit (coop-options.ini) | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_BOSS_FAMILIES` | no está en el kit | activado (`coop_objects.cpp`) | on/off | Reglas de agarre y tenis, y dibujar al jugador agarrado en la parte. |
| `LICHII_ORM_TWINROVA_KEEP` | comentado: `# LICHII_ORM_TWINROVA_KEEP=0` | activado (`coop_final_game.cpp`) | on/off | Twinrova se queda con la partida del jugador congelado o cargando. |
| `LICHII_COOP_BOSS_PULL_DOWNED` | comentado: `# LICHII_COOP_BOSS_PULL_DOWNED=0` | igual que `LICHII_COOP_DOWNED` (`coop_boss_game.cpp`) | on/off | Un Link caído se mueve a la sala del jefe tal como yace. |
| `LICHII_COOP_BOSS_RESPAWN` | comentado: `# LICHII_COOP_BOSS_RESPAWN=0` | igual que `LICHII_COOP_DOWNED` | on/off | De vuelta a la sala del jefe tras un game over dentro de ella. |
| `LICHII_COOP_HITS` | no está en el kit | activado (`coop_objects.cpp`) | on/off | El lado del golpeador en los golpes a un enemigo de otra partida. |
| `LICHII_COOP_HITS_DEDUPE` | no está en el kit | desactivado | on/off | En el dueño: deshacer una bajada de vida que parece el mismo golpe dos veces (un arma de marioneta golpeando allí). Desactivado hasta que una prueba muestre si las armas de marioneta llegan a golpear; los pares se registran en ambos casos. |
| `LICHII_COOP_COPY_SMOOTH` | no está en el kit | activado | on/off | Suavizado de las copias de enemigos (lo usa la respuesta a los golpes). |
| `LICHII_ORM_COUNTS` (el nombre antiguo `LICHII_COOP_COUNTS` se sigue leyendo) | `LICHII_ORM_COUNTS=shared_counts.txt` | sin definir: no se cuenta nada (`coop_final_game.cpp`) | un archivo de lista | Los Int contados (cuenta de la barrera) y sus Bool. |
| `LICHII_ORM_ESCAPE_CLOCK` (antiguo `LICHII_COOP_ESCAPE_CLOCK`) | comentado: `# LICHII_ORM_ESCAPE_CLOCK=1` | desactivado | on/off | Una sola cuenta atrás para el grupo en el castillo que se derrumba. |
| `LICHII_ORM_ESCAPE_CLOCK_WORD` | comentado: `# LICHII_ORM_ESCAPE_CLOCK_WORD=main+0x...:f32` | sin definir | `main+0x<ptr>,0x<off>:f32` (o los tipos de palabra que acepta `CountdownWord::parse`) | Dónde guarda nuestro juego su cuenta atrás. Sin definir: se busca en el juego en ejecución. |
| `LICHII_ORM_ESCAPE_CLOCK_SEARCH` | no está en el kit | activado | off, o raíces extra donde buscar | La búsqueda de la cuenta atrás. |
| `LICHII_ORM_ESCAPE_CLOCK_LENGTH_S` | no está en el kit | 180 | 1 a 3600 | La duración total de la cuenta atrás. |
| `LICHII_ORM_ESCAPE_CLOCK_TRIES` | no está en el kit | 3 | 1 a 720 | Búsquedas antes de rendirse. |
| `LICHII_ORM_GROUP_EXIT` (antiguo `LICHII_COOP_GROUP_EXIT`) | comentado: `# LICHII_ORM_GROUP_EXIT=0` | activado (`coop_teleport_game.cpp`) | on/off | La huida va con el grupo. |
| `LICHII_ORM_GERUDO` (antiguo `LICHII_COOP_GERUDO`) | no como línea (el comentario dice que `LICHII_ORM_GERUDO=0` lo desactiva) | activado (`coop_gerudo.cpp`) | on/off | Corrección de la cuenta de carpinteros y la regla de edad del Templo del Espíritu para las escenas. Los guardias y el Poe guía corren en cada partida sin interruptor. |
| `LICHII_COOP_HAZARDS` | no está en el kit | activado salvo que sea exactamente `0` (`coop_hook.cpp`, `getenv` simple) | `0` = desactivado | Banderas de peligro propias de cada jugador y el punto de alcance del caído (el punto de alcance solo con `LICHII_COOP_DOWNED=1`). |
| `LICHII_COOP_DEBUG_BOSS` | no está en el kit | desactivado (interruptor de upstream) | | Líneas de depuración de la sala de jefe; nuestros escenarios lo usan. |

Parámetros del servidor (campos de `SessionConfig` en `server/session.hpp`, valores compilados, sin opción de línea de comandos): `owner_downed_us` 1 s (0: los 10 s de upstream), `fight_near_m` 10, `fight_near_margin_m` 6, `fight_near_hold_us` 3 s (0: desactivado), `fight_near_cooldown_us` 8 s, `dead_room_m` 40 (0: desactivado), `dead_left_m` 15, `owner_keep_max_us` 30 s (lo máximo que una partida puede conservar un objeto con `owner_keeps`; 0: desactivado).

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/boss_rules.hpp`, `src/boss_rules.cpp` | Sin direcciones del juego, sin sockets. `boss_pull` (cómo se puede mover a un Link a la sala según su estado de caído), `BossRespawn` (recuerda un game over que empieza en una sala de jefe; tras la recarga, pide mover a Link de vuelta dentro), y las familias: la tabla de actores (`family_actors`), `held_animation` (los comandos AS de Link agarrado: `Grab*`, `Eaten*`, `FaceGrab`, no `GrabMovableObj` ni `Cliff_Hold*`), `family_key`, `family_holds`, `FamilyWatch` (una vez por recorrido de objetos: qué nos sujeta, si hay un intercambio en curso, y las líneas de comprobación). |
| `lichii/hook/coop_boss_game.cpp` (archivo de upstream, con nuestro pegamento añadido) | Lee `CoopDowned::State()`, fija `pull_while_down`, hace avanzar `BossRespawn` y ejecuta su movimiento con lo que upstream ya tiene (el cambio de escena cuando hay un evento en curso y la sala tiene punto de inicio, si no el movimiento por matriz). |
| `server/enemies.cpp` | Cuatro reglas del servidor como funciones de `Session`: un dueño caído cede sus enemigos tras `owner_downed_us`; un dueño que se va los pasa al jugador en pie más cercano; en un combate el enemigo va al jugador claramente más cercano; una muerte sigue muerta para una partida que recarga la sala mientras otro jugador se quedó en ella. Los enemigos que no son jefe también respetan `owner_keeps`. |
| `lichii/hook/coop_enemies.h/.cpp` | La lista de minijefes (`Enemy_Stalfos`, `Enemy_Lizalfos`, `Enemy_Dinolfos`, `Enemy_IronKnuckle_*`, `Enemy_Wolfos*`; nunca actores `_Dictionary` ni `_Haribote`); `CoopObjects::IsBoss` la consulta, así que cada uno recibe el bloqueo de combate y las reglas de traspaso de jefe de upstream. |
| `lichii/hook/coop_hits.h/.cpp` | Respuesta a los golpes en el golpeador (la copia dejada a su propia partida durante 300 ms, mantenida hasta que la respuesta del dueño está en pantalla, fundida de vuelta), la ventana de deduplicación medida con la propia latencia de las muestras, y el registro del dueño (`OwnerLedger`). El dueño sigue aplicando cada golpe y decide la vida. |
| `coop/host/include/coop/host/final_rules.hpp`, `src/final_rules.cpp` | Cuentas (`apply_counts`: un Int nunca por debajo del número de sus Bool activados), el reloj de huida (`EscapeClock`: arranca en el primer evento de derrumbe visto en la sala; una partida con más tiempo que el grupo se ajusta al del grupo, nunca más, nunca 0), `ending_holds_age`, `hand_over_holds` (Twinrova; lee la tabla de familias). |
| `lichii/hook/coop_final.h/.cpp`, `coop_final_game.h/.cpp` | Pegamento: añade las banderas contadas al conjunto compartido al empezar la sesión, recibe `STORY_EVENT`, cada 0,5 s aplica las cuentas, escribe la palabra de la cuenta atrás, fija los bits de conservación de Twinrova mediante `CoopObjects::SetKeep`. |
| `coop/host/include/coop/host/countdown_search.hpp`, `src/countdown_search.cpp`; `lichii/hook/coop_escape_timer.h/.cpp` | Busca la cuenta atrás de la huida en un juego en ejecución: palabras del módulo GameUI (`main+0x4e7cfa8`) y los objetos a los que apunta que se lean como segundos restantes, seguidas una vez por segundo; la que baja 1 s por segundo se informa en el formato de `LICHII_ORM_ESCAPE_CLOCK_WORD`. |
| `coop/host/include/coop/host/group_exit.hpp`, `src/group_exit.cpp`; `lichii/hook/coop_escape.h/.cpp` | Una partida que sigue en la escena de huida sigue a un compañero (en pie) que llegó a la siguiente escena, mediante el viaje de `/teleport`; nunca mientras nuestro Link está caído o hay un evento en curso; el compañero debe verse allí durante un tiempo de asentamiento. (La nota del encabezado "no glue in this tree" es antigua: el pegamento es `coop_escape.cpp`, alimentado desde `coop_teleport_game.cpp`.) |
| `coop/host/include/coop/host/gerudo.hpp`, `src/gerudo.cpp`; `lichii/hook/coop_gerudo.h/.cpp` | Los guardias con el cerebro de guardia de seguridad corren en cada partida (`runs_in_each_game`; los guardias que la propia sincronización de guardias de upstream comparte siguen compartidos), corrección de `CarpenterCounts`, `story_start_for_age`, una línea de registro cuando los cuatro carpinteros están libres y nadie tiene la tarjeta. |
| `coop/host/include/coop/host/hazards.hpp`, `src/hazards.cpp` | `hazard_flags` (20 Bool propios de cada jugador: Lente, Poe guía, reaparición del Desierto, reinicios al caer al vacío...), `unshare_hazard_flags`, `is_own_hazard_actor` (`Npc_GuidePoe*`), `DownedReach` (último suelo firme; mientras está caído en lava/arena/agua/remolino/vacío nuestras muestras llevan ese lugar). |

### Puntos de enganche en archivos de upstream

| Archivo | Enganche `// ORM` |
|---|---|
| `lichii/hook/coop_boss.cpp` | `Local::pull_while_down` (un Link caído se mueve por matriz, nunca por cambio de escena), `Sync::Beaten`, las salas de Ganondorf y Ganon añadidas a la tabla de salas. |
| `lichii/hook/coop_objects.cpp` | Lecturas de interruptores (`LICHII_COOP_HITS`, `_HITS_DEDUPE`, `_COPY_SMOOTH`, `LICHII_ORM_BOSS_FAMILIES`), las bolas de luz entregadas a la vigilancia de familias, las partes que sujetan pasadas al código de montura. |
| `lichii/hook/coop_objects_sync.cpp` | `IsMiniBoss` en `IsBoss`, `FamilyWatch` en cada tick, `KeepBits` (fija `owner_keeps` en nuestras muestras), la respuesta a los golpes y el registro del dueño. |
| `lichii/hook/coop_hook.cpp` | `CoopFinal::AddShared/OnEvent/Frame`, `CoopEscapeTimer::Frame`, `CoopGerudo::Frame`, el punto de alcance de los peligros y `unshare_hazard_flags`. |
| `lichii/hook/coop_teleport_game.cpp` | La huida (`CoopEscape::Escape`) en el viaje de `/teleport`. |
| `server/session.cpp`, `server/session.hpp` | `owner_keeps` / `owner_keeps_near` respetados para golpes y cercanía (como máximo `owner_keep_max_us`), los enganches de las reglas de enemigos (`unfit_owner`, `drop_presence`, `run_ownership`, `dead_for`, `set_scene`, `on_object_present`), contadores `owner_kept`, `downed_handed`, `fight_near_moved`, `dead_kept_inside`. |

## Mensajes y componentes

Sin tipos de mensaje nuevos. Usa:

| Qué | Dónde |
|---|---|
| `object_state` bit 7 `owner_keeps` (sin traspaso a un golpeador ni a un jugador más cercano) y bit 6 `owner_keeps_near` (sin traspaso a un jugador más cercano) | `OBJECT_SAMPLE` componente 18, `orm_types.hpp` |
| `ride` (componente 30 de `PLAYER_SAMPLE`): las muestras de un jugador agarrado nombran la parte que lo agarra, como un levantamiento | `orm_types.hpp` |
| `STORY_EVENT` (`0xF003`): el inicio del reloj de huida, la regla de edad del Templo del Espíritu | véase [07-cutscenes](07-cutscenes.es.md) |
| `DAMAGE`, `OBJECT_REMOVE`, `HANDOVER_*`, `PLAYER_STATUS` bit 4 de upstream | sin cambios |

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/lichii/boss_rules_test.cpp` | Tabla de `boss_pull`; caído llevado tumbado; sin traslado mientras se le reanima o en game over; cadenas con memoria falsa `read_vitals` a `Downed` a `boss_pull` a `Sync`, y derrota del grupo a Continuar a de vuelta en la sala; reintentos; entrada tardía; la tabla de familias, nombres de agarre, claves, lo que se conserva, la vigilancia con un Like Like, el respaldo, la copia de otra partida, el tenis, sincronización de objetos con un Like Like y Phantom Ganon. |
| `tests/orm/lichii_boss_test.cpp` | Nuestros casos de sala de jefe sacados del `boss_test.cpp` de upstream: la sala de Ganondorf (el piso superior de la torre) y la de Ganon, un jugador tardío llevado a la sala de Ganondorf; el evento de reanimación o de game over de un jugador caído nunca es la muerte del jefe; tras un game over y su recarga la sala empieza de nuevo; combates con memoria falsa con una reanimación y con una derrota del grupo. |
| `tests/server/enemies_test.cpp` | Dueño caído en 1 s, todos caídos, salida/desconexión/descarga con un jugador caído más cerca, cercanía de combate y sus negativos, jefe excluido, minijefe con dos y con seis golpeadores, una muerte a través de un traspaso una sola vez, recarga con alguien dentro, `owner_keeps` en un enemigo que agarra. |
| `tests/lichii/enemies_test.cpp` | Nombres de minijefes, `IsBoss`, dos Stalfos = dos combates. |
| `tests/lichii/hits_test.cpp` | Fases de la respuesta, latencia, muerte, ventana de deduplicación, el registro del dueño, dos partidas simuladas a 100 ms y 350 ms en un sentido. |
| `tests/host/final_rules_test.cpp` | La lista de cuentas del kit, dos pruebas a la vez en memoria falsa, nunca más bajo, el reloj (inicio, nunca más, nunca 0, vuelta del reloj del servidor), la edad del final, conservación de Twinrova y carga del espejo, las líneas del pegamento. |
| `tests/host/countdown_search_test.cpp` | Candidatos, una palabra que baja 1 s por segundo, reinicios, el formato de palabra. |
| `tests/host/group_exit_test.cpp`, `tests/lichii/escape_test.cpp` | Seguir solo a un compañero en pie, nunca estando caído o en un evento, asentamiento y reintento; el viaje. |
| `tests/host/gerudo_test.cpp` | Actores guardia, corrección de carpinteros, regla de edad, la línea de la tarjeta. |
| `tests/host/hazards_test.cpp` | Hashes de banderas frente a nombres, nada compartido lleva una bandera de peligro, Poe guía no compartido, comandos de muerte y de suelo firme, el punto de alcance en lava, suelo, vacío, agua, resbalones, cambio de zona, game over, desactivado. |
| `tests/server/session_test.cpp` | `owner_keeps` detiene un traspaso por golpe y se libera después, `owner_keeps_near` solo detiene la regla del más cercano, 30 s como máximo. |

Escenarios del arnés (en `tools/orm/harness`): `jefe-<boss>-entrada/caer/derrota/tarde` para Gohma, King Dodongo, Barinade, Phantom Ganon, Volvagia, Morpha y Bongo Bongo; `jefe-morpha-agarre`, `jefe-bongo-bongo-agarre`, `jefe-dead-hand-agarre`, `jefe-phantom-ganon-tenis`, `jefe-ganondorf-tenis`; `jefe-twinrova-*`; `final-*` (pruebas, cuenta de la barrera, Ganondorf, huida, reloj de huida, espada, créditos); `enemigo-caido`, `enemigo-sale`, `enemigo-reaparece`, `enemigo-minijefe`; `golpes-corte`, `golpes-muerte`.

## Líneas de registro

- Sala de jefe: `coop: boss rooms shared, a downed Link moved in as he lies, back into the boss room after a game over in it`
- Reaparición: `coop: boss: respawn: game over in the boss room of ...`, `... Continue left us in the boss room: nothing to do`, `... back in the boss room of ...`, `... not in the boss room yet: moved again (try N)`
- Agarre: `coop: grab: our Link is held by Enemy_LikeLike ... (Like Like), 0.5 m from it; ours: our samples of Like Like carry owner_keeps while it holds us ...`, `coop: grab: our Link is let go by ...`; en los demás `coop: ride: P2 is held by ...: drawn on our copy of it`
- Tenis: `coop: tennis: Phantom Ganon's ball is in flight in our game: the volley is ours, ...`, `... volley is over: it may be handed over again`
- Una vez cada una, si las lecturas funcionan: `coop: check: grab ok|fallback ...`, `coop: check: tennis ok|fallback ...`, `coop: check: twinrova charge ok|fallback`
- Golpes: `coop: objects: share hits feedback on, owner dedupe off (logged only), copy smoothing on`; cada 5 s `coop: objects: share hits: 5.0 s: N of ours on copies ...`
- Servidor: `enemies: told player N a kill again: another player stayed in its room (...)`
- Final del juego: `coop: count: on (LICHII_ORM_COUNTS shared_counts.txt): ...`, `coop: count: InsideGanonCastle_Dungeon_Barrier_Count 4 -> 5 (5 of 6 set)`, `coop: escape clock: on (LICHII_ORM_ESCAPE_CLOCK): one countdown for the group in ...`, `coop: escape clock: our countdown found at main+0x4e7cfa8,0x1a0:f32 (...)`, `coop: escape: on (a game still in the collapsing castle follows the mate at Ganon)`, `coop: escape: going with the group: trip to P2`
- Gerudo: `coop: gerudo: on (LICHII_ORM_GERUDO): ...`, `coop: gerudo: in GerudoFortress_Adult: each game runs its own guards ...`, `coop: gerudo: carpenters: count mended: ...`
- Peligros: `coop: hazards: on (LICHII_COOP_HAZARDS): 20 per-player flags kept out of sharing ...`, `coop: hazards: down in lava ('LavaDead', ...): the others see us lying there ...`, `coop: hazards: down on the ground ('Dead'): our real place is sent (a mate can reach us)`

## Estado

| Parte | Estado |
|---|---|
| Entrada a la sala de jefe con dos (la sala de upstream más nuestras reglas activadas) | confirmado en partida en nuestra compilación basada en test9 (escenario `jefe-gohma-entrada`, 9/9: A en la sala con la introducción, B trasladado) |
| Caído llevado a la sala, reaparición tras un game over en la sala | todavía no confirmado en partida |
| Agarre, tenis, Twinrova | todavía no confirmado en partida |
| Enemigos con seis (reglas del servidor, minijefes) | todavía no confirmado en partida |
| Golpes a un enemigo de otra partida | todavía no confirmado en partida (`golpes-corte` salió en rojo: B no llegó a golpear copias, sin datos) |
| Cuenta de la barrera, salas de Ganondorf/Ganon, huida con el grupo | todavía no confirmado en partida |
| Cuenta atrás de la huida | todavía no confirmado en partida (`final-huida-reloj` en rojo: el juego rechazó el evento de derrumbe en la partida guardada de la sonda, así que no corrió ninguna cuenta atrás) |
| Gerudo / Templo del Espíritu | todavía no confirmado en partida |
| Peligros | todavía no confirmado en partida |

## Carencias conocidas

- El test10 de upstream desactivó "el jefe va al más cercano" (`boss_near_hold_us = 0`), así que nuestro bit `owner_keeps_near` ya no cambia nada para los jefes; se conserva para cuando esa regla vuelva.
- Solo se puede agarrar al Link del dueño (la IA de un jefe conoce al Link de su propia partida): a un no dueño nunca se le agarra mientras no sea el dueño. Diferencia aceptada.
- No se sabe si los demás ven la bola del tenis (no es un objeto compartido).
- El aliento de Volvagia y el tambor de Bongo Bongo siguen actuando solo sobre el dueño; cada uno necesita un escenario de dos jugadores.
- No se conoce la palabra de la cuenta atrás de la huida; sin ella el reloj del grupo no puede ajustar el temporizador de nuestra partida.
- No se lee la cantidad de carga del espejo de Twinrova.
- No se sabe si las armas de marioneta golpean a los enemigos en la partida del dueño (el motivo del interruptor de deduplicación).
- Derrota del grupo dentro de una sala de jefe: no se midió dónde deja al grupo el propio Continuar del juego.
- Peligros: no se ha medido si la rama del hada del juego realmente devuelve un cuerpo del vacío o del fondo del lago al reanimarlo.
