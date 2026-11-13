# Lo que funciona en una partida de verdad

Solo lo **confirmado en partida**: visto con dos jugadores en una ejecución de juego de verdad (dos juegos emulados, un
servidor de sala local, el escenario en verde), con el nombre del escenario, el árbol, lo que se vio y la evidencia.
Cómo las ejecutamos, y cada rojo, está en [TESTED.md](TESTED.md). "test9" y "test10" abajo significan nuestro trabajo
sobre tu v0.1.0-test9 o v0.1.0-test10; este regalo es el árbol basado en test10.

Las líneas de registro están copiadas de los registros de los juegos, acortadas con "..." y sin direcciones ni rutas.

## Confirmado en partida

### 02-network

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Protocolo 110 con tu tráfico sellado y la contraseña de entrada, sobre nuestro transporte WebSocket y sobre UDP | todas las ejecuciones de test10; `cartel` (signboard) por WebSocket y por UDP | test10 | los dos juegos entran con la contraseña, el tráfico va sellado, los escenarios pasan con los dos transportes | `coop: on, server 127.0.0.1:... (ws /), name 'A', join password set`; `coop: traffic encrypted`; `cartel` PASS 4/4 por WebSocket y 4/4 por UDP |

### 03-fixes

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Un anfitrión en la pantalla de título ya no pasa las banderas de una partida nueva a quien entra | toda la ronda 7 (50 parejas, 100 juegos, A entrando segundo) y las ejecuciones de test10 | test9, test10 | la primera vida de cada juego es la de su propia partida; ningún `save mismatch` | `coop: flags: we host with no save in (the title screen): our table is held until our save is in, so no joiner gets a new game's flags (3 hearts, no magic)`; arnés: `-- A: first life 44 of 44 = its save's 44 (ok)` |

### 04-downed

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Caído, no muerto; revivido por un compañero | `caer-pareja` (downed pair) | test9 (A primero y A segundo), test10 | A con vida 0 queda caído, B lo revive, la vida vuelve entera | PASS 8/8. `coop: downed: alive -> downed (life 0 of 12, ...)`; `coop: downed: downed -> reviving: revive requested by P2`; `coop: downed: reviving -> alive: revived (by P2, life 12 of 12, 6.96 s after the gate opened)` |

### 05-bosses

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Entrar juntos en la sala de un jefe | `jefe-gohma-entrada` (Gohma boss entry) | test9 (A primero y A segundo) | A en la sala de Gohma; B, en la entrada de la mazmorra, es movido dentro cuando empieza la intro de A; la intro empieza y acaba en los dos | PASS 9/9. comprobaciones: `boss: in the boss room of InsideTheDekuTree: Enemy_DungeonBoss_Gohma` (A y B), `another player is in the boss room of InsideTheDekuTree: moving in`, `intro started`, `intro ended`; nunca `boss beaten` |

### 06-world

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Capacidad de corazones del grupo | `corazon` (heart) | test9 (A segundo) | A consigue un contenedor de corazón; B sube su propio máximo y conserva su vida | PASS 9/9. B: `coop: hearts: max life 12 -> 16 (from P2)`, `coop: hearts: our Link's max 12 -> 16 (life 12 kept)` |
| La cartera propia de cada jugador | `cartera-cada-uno` (each one's wallet) | test9 (A segundo) | cada uno conserva sus rupias; cada juego conoce las de los demás | PASS 21/21. `coop: wallet: P2 'A' (you) has 87 rupees (was 7)`, `... has 25 rupees (was 87)`, `coop: wallet: P1 'B' has 43 rupees (was 7)` |
| Hablar reclama al aldeano; el otro mira | `charla` (talk), `charla-dos` (talk, two) | test9; `charla` también test10 | A habla con un Kokiri; B ve la charla compartida, `not claimed`, la mezcla de pose, `talk over` | `charla` PASS 4/4 (test9 y test10); `charla-dos` PASS 8/8, B: `coop: npc: Npc_Kokiri_004_Child ... talks with its owner P2: not claimed, our talk plays on our copy only` |
| Las banderas de una charla llegan al otro juego (Mido) | `charla-mido` (Mido talk) | test9, test10 | A habla con Mido hasta el final; la bandera llega a B, también con Mido sin cargar allí | PASS 20/20. `coop: npc: talking to Npc_Kokiri_Mido_Child ... (owner P1, claimed), found by its event`; `coop: npc: talk over, claim let go` |
| El evento propio del juego encuentra al aldeano; ninguna charla falsa por cercanía | `charla-saria` (Saria talk) | test9 (A segundo) | A habla con Saria donde está sentada | PASS 13/13. `coop: npc: talking to Npc_Kokiri_Saria_Child_OcarinaHolder ... (ours), found by its event`; `coop: npc: our event 'DefaultBootEvent' is not a talk: no townsperson taken by reach` |
| Los cofres se abren en todos los juegos (`CHEST_OPEN`) | `efectos-cofre` (chest effects) | test9, test10 | A abre un cofre; la tapa de B se abre con él y se queda abierta | PASS 23/23. B: `coop: objects: share Obj_TBox_A_02 ...: opened in another game, its lid opened here with it (CHEST_OPEN)`; `coop: check: chest lid ok Obj_TBox_A_02 kept our 'FrontOpen' 300 ms in its slot 0` |
| Nivel del Templo del Agua compartido, que espera bajo un nadador | `meca-agua-templo` (Water Temple level) | test9 (A segundo) | el nivel que llega del otro jugador espera mientras nuestro Link nada, y luego se escribe; se ve en la siguiente carga de la sala (no en vivo) | PASS 17/17. `coop: water: WaterTemple_Room00_WaterLevel (...) from another player waits: our Link plays 'SwimWait'`; `... waited 7.5 s: written, our Link stands` |
| Interruptor de suelo sostenido, sala 10 del Templo del Fuego | `meca-sostenido-fuego` (held switch, Fire) | test9 (A segundo) | A se queda en el interruptor; el muro de fuego de B se va | PASS 12/12. B: `coop: held switch: FireTemple room10: door opened with the flag (Obj_FireTemple_FireWallTrap_A_01 went, 3.4 s after the press)` |
| Invitación de las canciones de viaje | `viaje-cancion` (song trip) | test9, test10 | B toca el Minueto del Bosque y viaja; A recibe la invitación, mantiene L y le sigue | PASS 9/9. `coop: songs: our Link arrives by a warp song in SacredForestMeadow_Child ... (event Dm_SY_Ocarina_move_Ed)`; `coop: songs: following P2 to SacredForestMeadow_Child (held L)` |

### 07-cutscenes

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Cinemáticas de la historia para todos | `historia` (story) | test9, test10 | el juego de A arranca el encuentro con el Árbol Deku y es su dueño; el juego de B arranca el mismo | PASS (sin cuenta impresa). A: `coop: story: STORY_EVENT start sent ('KokiriForest_Meet_DekuTree_MeetDekuTree_01' in KokiriForest_Child, ...)`; B: `coop: story: STORY_EVENT start received from P1 (...): starting` |

### 09-pvp

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Nuestro daño de arma e invulnerabilidad sobre tu PvP | `pvp-espada` (PvP sword) | test10 | el anfitrión enciende el PvP; la espada de A quita 2 cuartos de corazón a B; un segundo golpe justo después hace 0 | PASS. A: `coop: pvp: P2 hurt by P1: 2 (life 10, flags 0)`, `coop: pvp: P2 hurt by P1: 0 (life 10, flags 2)`; B: `coop: pvp: our rules on P1's hit (2 quarter hearts by upstream's): taken, takes 2`, `coop: pvp: the game took the hit (life 10)` |

### 10-hud

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Chat rápido (parche 0101), y L libre jugando | `comunicacion-aqui` (communication, "here!") | test9 (A segundo) | "¡Aquí!" y "¡Ayuda!" llegan; el límite de uno por segundo se cumple; L no mueve nada | PASS 18/18. `coop: songs: invitation button L: free (3 of 3 presses moved nothing of our Link): L alone`; `coop: quick: on (LICHII_ORM_QUICK_CHAT): hold L and press the d-pad: ...` |
| Mapa de compañeros: etiqueta con el nombre, punto con distancia, flecha en el borde | `mapa-flecha` (map arrow) | test9 (A segundo) | B cerca, lejos y detrás de A | PASS 4/4, por el camino de reserva: `coop: check: map mini KokiriForest_Child fallback no minimap view or box in the map table (coop_map.txt): edge arrows and lines only` |

### 11-puppets

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| La marioneta de un jugador en pausa se retiene, no se esconde, y aún cambia de equipo | `menu-pausa` (pause menu) | test9 (A segundo) | A abre el menú y cambia de escudo; la marioneta en B se retiene, se pone el escudo y vuelve a mostrarse | PASS 15/15. B: `coop: puppet: P2 show -> hold (paused)` |
| Las marionetas siguen visibles durante un evento (cartel) | `cartel` (signboard) | test9, test10 (A primero, B primero, UDP, y con los cuerpos encendidos) | A lee el cartel; B ve la marioneta de A todo el rato | PASS 4/4 cada vez. A: `coop: event running 'Obj_Kokiri_Signboard_GuideAction_A_03' (we play 'EvtTurnSlow')` |
| Objetos que se llevan | `levantar-vasija` (lift a pot) | test9 (A segundo), test10 | A levanta, lleva y lanza una vasija; B la dibuja en las manos de la marioneta, quieta; se rompe en los dos juegos | PASS 16/16. A: `coop: carry: lifting Obj_Pot_A_08 ... (owner P2, claimed), by bone 0`; B: `coop: check: carry hold ok carried copies held still between our writes (largest drop 0.019 in 60 frames)`, `coop: world: ... Obj_Pot_A_08 broken by another player` |
| La ocarina en la mano de la marioneta; se oyen las notas y canciones de los demás | `ocarina-mano` (Ocarina in hand) | test9 (A segundo) | B toca la Nana de Zelda; la marioneta de B en A lleva la ocarina propia de B; A la oye | PASS 17/17. `coop: check: ocarina hand ok P1's own Ocarina is in its hand while P1 plays ('DemoOcarina', up 0.74)`; B: `coop: ocarina note: song ZeldaLullaby sent (serial 21)`; A: `coop: ocarina sound: P1 Melody_ZeldaLullaby at (...)` |

### 12-bodies

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Choque de cuerpos de verdad entre Links (`/choque`) | `choque-bloqueo` (body block) | test10 (B primero) | A anda hacia B y se para contra su cuerpo, luego se aleja libre | PASS 14/14. `coop: check: bodies: collider ok CoopPuppet_Child_Body made: kinematic, placed with its puppet each frame, hitting only our Link`; `bodies: our Link against P1's body (0.44 m apart, the bodies 0.46)` |
| De pie sobre la cabeza de un compañero | `choque-encima` (on top) | test10 | A cae sobre la cabeza de B y se queda de pie | PASS 10/10. `bodies: our Link stands on P1's head (1.17 m over his feet)` |
| Nadie se queda encajado en un mismo sitio de Continuar | `choque-continuar` (Continue spot) | test10 | los dos en el mismo sitio de Continuar; la marioneta se dibuja 1 m a un lado y el cuerpo es sólido allí; el Link de nadie se mueve | PASS 9/9. `coop: puppet: P1 stands on our Link when first seen here (one Continue spot): drawn 1.00 to its side ...` |
| Charlas y carteles con los cuerpos encendidos | `cartel`, `charla` | test10 | el atasco del diseño viejo no sale | PASS 4/4 y 4/4 |

### 13-resume

| Qué | Escenario | Árbol | Lo que se vio | Evidencia |
|---|---|---|---|---|
| Reanudar dentro de una mazmorra: vuelta al punto de la sala de la entrada | `reanudar-mazmorra-1`, `reanudar-mazmorra-2` (resume in a dungeon, dos sesiones) | test10 | la sesión 1 guarda el punto; la sesión 2 va a él, nunca a la otra sala | PASS 4/4 y 10/10. `coop: resume: the last place (InsideTheDekuTree at -51.45 111.25 15.08) is in another room of this dungeon (issue 197: ...): going to the spot of its entrance room ...`; `coop: resume: at the spot of the entrance room of InsideTheDekuTree we last stood on before leaving it` |

## Visto en el registro de una partida, pero el escenario sigue en rojo

No cuenta como confirmado; se lista para que sepas lo cerca que está.

- **08-age, el grupo sigue un cambio de edad** (`edad-grupo-cerca`, test10, rojo 7/20 solo porque las líneas llegaron
  cuando el guion de B ya había acabado): B recibió el aviso y la cuenta 3-2-1, luego `coop: story: the game took 'Market_Child_Field_ChildToAdult' (returned 0x1, component phase 1)`
  y `coop: age: now adult in Market_Adult, 32.7 s after the reload began: the game's own era change, its equipment swap and era; ...`,
  y los dos registraron `notice gathered: Grupo reunido en el Templo del Tiempo`.
- **11-puppets, el Viento de Farore** (`sonda-magia`, `efectos-magia`, test9): el punto se comparte en una mazmorra
  (`spawn: sent FaroreWindWarpPoint`, en B `got FaroreWindWarpPoint`) y la marioneta en B juega el lanzamiento
  (`P2 effects: 'DemoStartFaroreWind' played under the copied pose`).
- **11-puppets, gancho, flechas, bombchu y martillo en la marioneta** (`objetos-uso`, rojo 17/23 por el Fuego de Din y
  la lente y las botas).
- **09-pvp, la lectura del escudo** (`pvp-escudo`, rojo 5/8): `coop: pvp: shield read from 8 AS slots of our Link, the equipped shield (Weapon_Shield_Deku) and the guard button 0x100`
  y un golpe por la espalda recibido como debe; la parada de frente no llegó a probarse.

## Aún no confirmado en partida

Con pruebas unitarias (o visto solo en ejecuciones más antiguas sobre árboles anteriores que este archivo no cubre).
Una línea por función:

- **01-platform**: la capa de plataforma entra en el filtro de archivos de tu módulo de Hookshot; nunca ejecutada en Hookshot.
- **02-network**: seis juegos de verdad (solo un juego más cinco bots, en rojo); elección de anfitrión (`HOST_CHOICE`, `/anfitrion`); avisos de enlace; gestión de interés para seis.
- **03-fixes**: el camino del invitado que carga antes que el anfitrión (`our save load from the title screen: N values of our save kept`) solo en pruebas unitarias.
- **04-downed**: derrota del grupo, hada propia en botella, cámara de espectador, Continuar tras game over, sonido de compañero caído.
- **05-bosses**: cuenta atrás de la huida (`LICHII_ORM_ESCAPE_CLOCK` apagado), golpes al enemigo de otro juego (`golpes-corte` en rojo), final con seis, reglas Gerudo/Espíritu, peligros, reglas de jefe por familia.
- **06-world**: interruptores sostenidos que necesitan las palabras del actor (sala 08 del Espíritu: la bandera sola no la abre), sala 14 del Agua, Jabu; bloques de la Canción del Tiempo; Biggoron; la mayoría de las listas del kit; reglas de objetos.
- **07-cutscenes**: saltarse una cinemática compartida cuando el dueño la salta a propósito.
- **08-age**: el movimiento del grupo al Templo del Tiempo (rojo solo por tiempos, arriba); la vuelta a niño; reglas del Templo del Tiempo.
- **09-pvp**: parada con escudo de frente, Nayru, el último en pie, el empujón; fijar aliados; el registro de comandos con Tab; llamadas y renombres; `/fps`, `/stats`.
- **10-hud**: el HUD dentro de la imagen con todo a la vez; ajustes de HUD por jugador; calibración del minimapa (necesita una ventana visible).
- **11-puppets**: Fuego de Din y Amor de Nayru (el juego los rechaza: `ItemEmpty`); flechas elementales, lente, botas de hierro; caballos y Epona; suelos que se mueven; mirada de los aldeanos; colores.
- **12-bodies**: con retardo de red real (cada juego para a su Link contra el cuerpo del otro); la variante sin disco en la cabeza (`LICHII_ORM_BODY_TOP=0`).
- **13-resume**: reanudar un sitio fuera de una mazmorra dado por la app (`reanudar-sitio` en rojo, sin repetir con la regla nueva).
- **14-app**: la pantalla "Cooperativo" no formó parte de estas ejecuciones.
