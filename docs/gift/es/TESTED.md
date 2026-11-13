# Lo que ejecutamos, y cómo

Este archivo lista cada escenario de dos juegos que ejecutamos entre el 2026-11-12 y el 2026-11-13 mientras
preparábamos este regalo: las rondas de sondas 1 a 7, las pruebas de colisión de cuerpos, la primera pasada completa
sobre el árbol basado en test10 (la llamamos "la pasada de punta a punta de la 0.12", por el número de versión de
nuestra app) y la comprobación en partida sobre el árbol de test10 a la mañana siguiente. Para cada escenario: qué
comprueba, verde o rojo, cuántas comprobaciones pasaron, sobre qué árbol y, para cada rojo, la causa que encontramos y
dónde vive el arreglo (por ID de función). Lo que está confirmado que funciona se resume en [WORKS.md](WORKS.md).

Fuentes: nuestras notas en español, guardadas en nuestro propio repositorio (`SONDAS.md` rondas 1-7, `TRIAGE_ROJOS.md`, `CHOQUE.md`,
`PIN_TEST10.md`, `T9_INTEGRADO.md`, `EDAD.md`, `PVP.md`, `PARTIDA_SALA.md`) y el archivo de resumen que escribió cada
ejecución. Donde dos fuentes no coinciden, gana la más reciente y el texto lo dice.

## Cómo se ejecuta un escenario

Versión corta (el método está en [HOW-WE-WORK.md](HOW-WE-WORK.md)):

- **Dos juegos ocultos en un PC**, A y B, cada uno con su carpeta de instancia y su partida, y un servidor de sala
  local en 127.0.0.1 (WebSocket por defecto, UDP si se pide). Ventanas ocultas, sin foco, sin sonido. Una pareja a la
  vez.
- **Archivos de guion**: `tools/orm/harness/escenarios/<nombre>.A.txt` y `<nombre>.B.txt` (los de PvP están en
  `tools/autotest/scenarios/`). Cada juego lee su guion por la entrada de pruebas del gancho: `warp`, `place`, `goto`,
  pulsaciones de botones, `talkthrough` (pulsar A mientras dura nuestra charla), `await mate` (esperar a que el otro
  juego esté en nuestra escena), comandos de chat y `mark <nombre>` para cortar el registro en partes.
- **Directivas** (líneas `#!` arriba): `expect A|B <regex> within <marca>..<marca>`, `never ...`, `#! saves <bloque>`
  (una partida sintética construida desde una receta), `#! switches`, `#! env`, `#! join A-first|B-first`,
  `#! timeout`.
- **El juez es el registro.** El arnés (`r03_pareja.py`; tandas con `secuencia.ps1` o `rapida_t9.ps1`) comprueba que
  los dos juegos entraron, cargaron y se asentaron (fps), que había RAM libre suficiente para empezar (14 GB) y, desde
  la ronda 7, que la primera vida que lee cada juego es igual al `MaxLife` de su partida. Luego cuenta las líneas
  expect/never. `RESULT: PASS (expect 9/9)` significa 9 comprobaciones de 9. Algunos escenarios no imprimen cuenta
  (`edad`, `historia`, `pvp-espada`): de esos solo tenemos PASS/FAIL.
- **Orden de entrada**: en la ronda 7 y en la mayoría de las ejecuciones de test10 B entra primero y A segundo (A es el
  jugador 2), el orden que escondía el fallo de la partida de abajo.

Los nombres de los escenarios se dejan como están (en español); la primera vez que aparece cada uno lleva una glosa
corta en inglés.

## Árboles (compilaciones)

| Nombre aquí | Qué es | Protocolo | Ejecuciones |
|---|---|---|---|
| **árbol basado en test9** | tu v0.1.0-test9 más nuestro trabajo, integrado (pruebas unitarias de 2140 a 2157 en verde durante las rondas); emulador con tus parches de lichii y los nuestros (0100 HUD, 0101 chat rápido) | 109 | rondas de sondas 1-7 (2026-11-12, de 00:04 a 18:08) y las pruebas de cuerpos (de 18:41 a 22:15): **198 ejecuciones de escenario** |
| **árbol basado en test10** | tu v0.1.0-test10 más nuestro trabajo: la base de este regalo (pruebas unitarias 2569/2569 en la fusión, 2591/2591 en la cabeza del regalo) | 110 (tráfico sellado, contraseña de entrada) | la pasada de punta a punta de la 0.12 (2026-11-12, de 22:15 a 23:20) y la comprobación en partida (2026-11-13, de 10:19 a eso de las 11:40): **29 ejecuciones de escenario** |

`PIN_TEST10.md` y `T9_INTEGRADO.md` todavía dicen "nada visto en una partida"; era verdad cuando se escribieron y las
ejecuciones de abajo lo sustituyen.

## Resumen

Un escenario cuenta por su **última ejecución en ese árbol**. 45 escenarios distintos, 227 ejecuciones en total.

| Función | test9: escenarios | verdes | rojos | test10: escenarios | verdes | rojos |
|---|---|---|---|---|---|---|
| 02-network | 1 | 0 | 1 | (todas las ejecuciones: WebSocket o UDP, sellado, con contraseña) | | |
| 03-fixes | (comprobado en cada ejecución desde la ronda 7: la comprobación de la primera vida) | | | (igual) | | |
| 04-downed | 1 | 1 | 0 | 1 | 1 | 0 |
| 05-bosses | 3 | 1 | 2 | 0 | 0 | 0 |
| 06-world | 15 | 11 | 4 | 4 | 4 | 0 |
| 07-cutscenes | 1 | 1 | 0 | 1 | 1 | 0 |
| 08-age | 1 | 1 (*) | 0 | 2 | 1 (*) | 1 |
| 09-pvp | 0 | 0 | 0 | 2 | 1 | 1 |
| 10-hud | 2 | 2 | 0 | 0 | 0 | 0 |
| 11-puppets | 11 | 4 | 7 | 3 | 2 | 1 |
| 12-bodies | 4 | 3 | 1 | 3 | 3 | 0 |
| 13-resume | 0 | 0 | 0 | 3 | 2 | 1 |
| **Total** | **39** | **24** | **15** | **19** | **15** | **4** |

(*) `edad` (edad) pasó, pero los registros muestran que el juego rechazó el cambio de edad todas las veces: el verde
era hueco (ver 08-age). No se cuenta como funcionando en ningún sitio.

Sin escenario propio en estas ejecuciones: 01-platform, 14-app, 15-tools (es el propio arnés), 16-docs.

## Familias de rojos

Antes de arreglar, los rojos se ordenaron por causa (`TRIAGE_ROJOS.md`, rondas 1-5, leído solo de los registros):

| Familia | Causa | Dónde se arregló |
|---|---|---|
| a | el segundo juego en entrar cargaba la partida equivocada (la tabla del anfitrión en la pantalla de título) | 03-fixes (ver "Fallos" abajo) |
| b1 | el archivo de reanudar de la ejecución anterior ponía a Link en otro sitio | 15-tools (el arnés lo borra) |
| b2 | B empieza unos 115 s después que A; esperas fijas demasiado cortas | 15-tools (`await mate`) |
| b3 | líneas que un juego escribe al arrancar, antes del guion, comprobadas en la ventana equivocada | 15-tools (`within boot..`) |
| b4 | B se queda en la pantalla de título (`NEVER STARTED`) | 15-tools (vigilante previsto) |
| b5 | movimiento del guion: el `goto` se agota, sitio u orientación equivocados, charlas largas | 15-tools (guiones) |
| b6 | PC sin RAM (bajan los fps, `FROZEN`) | no es código: cada juego reserva hasta 14 GB de memoria del invitado; no se empieza nada con menos de 16 GB libres |
| b7 | escenario mal planteado (sin `#! saves`, interruptores) | 15-tools (escenarios) |
| c1 | el Fuego de Din y el Amor de Nayru juegan `ItemEmpty` | abierto (11-puppets) |
| c2 | una copia que se lleva se rompe en las manos de la marioneta | 11-puppets |
| c3 | la reserva por radio de la charla tomaba un evento del arnés como charla | 06-world |
| c4 | la palabra de la cuenta de la huida es la copia que dibuja la interfaz | 05-bosses (reserva de seguridad; abierto) |
| c5 | arreglos de código dentro de las rondas (pausa, ocarina, cofre, paquete de marionetas) | por función, abajo |
| d | un momento que las partidas sintéticas o los guiones aún no alcanzan | abierto |

## Escenarios por función

### 02-network

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `seis-kokiri-recorrido` (six, Kokiri walk) | un juego y cinco bots recorren el Bosque Kokiri sala a sala; los bots reviven a A, mandan una bandera compartida, siguen los cambios de escena | test9: **rojo 25/37** (ronda 1) | entraron los cinco bots y salieron las cinco marionetas; rojo: el `goto` se agotó 6 veces (b5), A acabó en game over en vez de ser revivido, la historia del Árbol Deku no llegó, dos líneas de arranque (b3, arreglado). Antes de esa ejecución todos los bots caían al entrar (`message too big: read limited at 32769 bytes`, el límite de lectura por defecto de la librería WebSocket de nuestro bot) y el bot no conocía `-check`: los dos arreglados en 15-tools. No se repitió. |

Todas las ejecuciones de test10 usaron el protocolo 110 con tráfico sellado y contraseña de entrada (`coop: traffic
encrypted`, `coop: on, server 127.0.0.1:... (ws /), name 'A', join password set`); `cartel` también se ejecutó por UDP
(11-puppets).

### 04-downed

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `caer-pareja` (downed pair) | la vida de A a 0 en el inicio de Kokiri: A queda caído (no muerto), B lo revive; la botella del hada, la puerta y el evento de game over tal como los lleva el gancho | test9: **verde 8/8** (ronda 1 con A primero; ronda 7 con A segundo); test10: **verde 8/8** | 3/8 una vez en la ronda 7 con A segundo: A cayó a 0,7 m de su sitio de siempre y B, a 1,58 m, quedaba fuera del alcance de 1,5 m. El guion acerca más a B (15-tools). |

### 05-bosses

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `jefe-gohma-entrada` (Gohma boss entry) | A carga en la sala de Gohma; B, en la entrada de la mazmorra, es movido dentro en cuanto empieza la intro de A; la intro empieza y acaba; el jefe de nadie vencido | test9: **verde 9/9** (ronda 1; ronda 7 con A segundo) | ninguno |
| `final-huida-reloj` (final escape clock) | los dos en lo alto del castillo que se derrumba; la cuenta atrás de la huida del grupo (exploratorio) | test9: **rojo, lo mejor 7/9** (ronda 7) | El juego toma `CrumblingGanonCastle_Arrive` con la partida `21-huida-arriba`. Las palabras que encuentra la búsqueda son la copia que la interfaz redibuja cada fotograma desde el temporizador de verdad, así que nunca conservan lo que escribimos; el temporizador de verdad es el de minijuegos que arranca el flujo `Defeat_Ganon` y probablemente cuenta hacia arriba. Reserva de seguridad en 05-bosses: tras 3 escrituras que el juego no conserva (una escritura que rebota también cuenta), se deja de escribir (`that word is not the countdown`). `LICHII_ORM_ESCAPE_CLOCK` sigue apagado. Abierto: buscar una palabra que sube 1 s por segundo; arrancar el reloj del grupo tras la cinemática de 88 s. Las rondas 1-3 tenían además la familia a. |
| `golpes-corte` (hits, slash) | el juego de A es dueño de un enemigo quieto delante de A; B corta la copia cuatro veces | test9: **rojo 5/6** (ronda 1) | B nunca golpeó la copia (colocación, b5). Previsto: B de cara a la copia tras `await mate`. No se repitió. |

### 06-world

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `corazon` (heart) | A consigue un contenedor de corazón; B aprende el nuevo máximo del grupo y sube el de su propio Link, conservando su vida | test9: **verde 9/9** (ronda 7, A segundo) | 8/9 en la ronda 1: una línea de arranque comprobada en la ventana equivocada (b3). `LICHII_ORM_HEARTS` está encendido en el kit. |
| `cartera-cada-uno` (each one's wallet) | cada jugador conserva sus rupias; A ve las 43 de B y conserva sus 87, luego 25; las de B nunca caen en la cartera de A | test9: **verde 21/21** (ronda 2; ronda 7 con A segundo) | 14/21 y 20/21 antes: la línea del HUD de B salía después de que A hubiera acabado (b2): ahora A espera a B. |
| `charla` (talk) | A cruza el túnel de Kokiri hasta un Kokiri y habla; A reclama al aldeano; B mira | test9: **verde 4/4** (ronda 2; otra vez en la primera ejecución del diseño nuevo de cuerpos); test10: **rojo 3/4** en la pasada de la 0.12, luego **verde 4/4** (2026-11-13, cuerpos encendidos) | Ronda 1: el archivo de reanudar (b1). Ronda 7 con A segundo: 2/4 en cinco tandas, por el empuje de cuerpos viejo (12-bodies, ver "Fallos"). El rojo de test10: no salió la línea de la reclamación; sin triar; el mismo escenario pasó a la mañana siguiente. |
| `charla-dos` (talk, two) | la charla compartida: `not claimed` en B, la mezcla de pose de 600 ms, `talk over` en los dos | test9: **verde 8/8** (ronda 3; ronda 7) | 1/8 antes: A aparecía empujado contra una esquina por la marioneta de B (inicio movido 2,5 m); una charla de Kokiri tiene unas 30 páginas: nuevo paso de guion `talkthrough` (15-tools). |
| `charla-mido` (Mido talk) | A habla con Mido (el que bloquea el paso); la bandera de la charla llega a B, también cuando el Mido de B no está cargado | test9: **verde 20/20** (ronda 6; ronda 7); test10: **verde 20/20** | La charla dura 49-57 s (`talkthrough` de 90 s); el tope de 60 s de la reclamación la soltaba al final, ahora 3 min (06-world); la bandera de B entraba sin línea cuando Mido estaba descargado, ahora `written: its actors are not loaded (at once, nothing waited)` (06-world). Una ejecución 20/20 pero FAIL: `FROZEN at the end`, 9,8 fps con 0,3 GB libres (b6). `LICHII_COOP_TALK=shared_talk.txt` está encendido en el kit. |
| `charla-saria` (Saria talk) | A habla con Saria (sentada, la que tiene la ocarina), encontrada por el componente de evento del juego | test9: **verde 13/13** (ronda 7) | El `warp KokiriForest_Child` de prueba sin punto de inicio dejaba a los dos Links cayendo al vacío en bucle (`DmF_SY_FallDown`) en todas las rondas: ahora `KokiriForest_FrontOfLostWood` (15-tools). Durante esa caída la reserva por radio tomaba a Saria (c3): ahora un aldeano solo se toma por cercanía dentro de un evento de charla (06-world): `coop: npc: our event 'DefaultBootEvent' is not a talk: no townsperson taken by reach`. |
| `charla-biggoron` (Biggoron talk) | A habla con Biggoron desde el saliente | test9: **rojo 3/13** (ronda 1) | No hubo charla por el componente de evento; el radio de 25 m tomó un evento `place` del arnés como charla (c3, arreglado después en 06-world); colocación (b5). No se repitió. |
| `efectos-cofre` (chest effects) | A abre un cofre; el cofre de B se abre con él (`CHEST_OPEN`); el objeto se ve en alto sobre la marioneta de A | test9: **verde 23/23** (ronda 4; ronda 7); test10: **verde 23/23** | La tapa empieza a abrirse en el mismo fotograma que nuestro movimiento: ahora cuenta el primer vistazo (06-world). El remake no tiene orden de "obtener objeto": el que abre sigue en `DemoOpenTreasureBox`; regla nueva: seguir en ella 2,5 s después de la tapa = lo sostiene en alto (11-puppets). Esperas y colocación (15-tools). El paquete de marionetas usado era viejo (sin dobles): rehecho. |
| `meca-agua-templo` (Water Temple level) | el nivel del Templo del Agua se comparte; espera mientras un Link nada | test9: **verde 17/17** (ronda 3; ronda 7) | Link se ahogaba (la partida sintética no llevaba puesta la túnica Zora: receta arreglada); la comprobación "en vivo" se armaba en mal momento (06-world). Valor: el nivel **no es en vivo**: ningún plano de agua se mueve en 10 s; se aplica en la siguiente carga de la sala. `LICHII_ORM_WATER` está encendido. |
| `meca-sostenido-fuego` (held switch, Fire) | A se queda en el interruptor de la sala 10 del Templo del Fuego; el muro de fuego de B se va | test9: **verde 12/12** (ronda 2; ronda 7) | Valor: la bandera sola lo abre (`door opened with the flag`, 3,3 s). Antes: B empezaba tarde (b2), familia a. |
| `meca-sostenido-espiritu` (held switch, Spirit) | interruptor de la sala 08 del Templo del Espíritu | test9: **verde 12/12** (ronda 3; ronda 7) | Verde, pero lo que demuestra es que el camino de la bandera dice la verdad: B escribe la bandera y registra que la puerta **no** se movió. La puerta necesita las palabras del actor (candidatas encontradas, sin programar: escribir un contacto sin su puntero podría tumbar el juego). |
| `meca-sostenido-agua` (held switch, Water) | interruptor de la sala 14 del Templo del Agua (tres banderas: pulsado, por bloque, botas de hierro) | test9: **rojo 10/14** (ronda 7) | El modo de botas de hierro no se alcanza por guion: con las botas puestas Link se hunde, se pone de pie y vuelve a nadar; ZR con `HeavyBoot` no hace nada nadando (ningún `SwimBootsChange`) (d). Abierto. |
| `meca-sostenido-jabu` (held switch, Jabu-Jabu) | interruptor de la piedra de la sala 17 de Jabu-Jabu | test9: **rojo 2/13** (ronda 1) | Sin línea `#! saves`: una partida de niño en Kokiri; el viaje no podía ocurrir (b7). Previsto: `#! saves 10-jabu`. No se repitió. |
| `meca-cancion-tiempo` (Song of Time block) | A toca la Canción del Tiempo ante el bloque del Templo del Bosque; el bloque de B también se va | test9: **rojo 5/9** (ronda 7) | La canción ya mueve el bloque (`TriggerSongOfTimeBlock`) y el juego escribe cuatro banderas Bool guardadas que no tienen nombre en `GameDataList`; aún no están en `shared_dungeon_songs.txt`, así que B no recibe nada (d). Antes 2/8: familia a y B no llegó a empezar (b4). |
| `viaje-cancion` (song trip) | B toca el Minueto del Bosque y viaja; A recibe la invitación y le sigue manteniendo L | test9: **verde 9/9** (ronda 4; ronda 7); test10: **verde 9/9** | Las canciones no se reconocían: el remake guarda un anillo de 8 notas y estaba lleno (guardar la ocarina y sacarla otra vez); el remake hace una pregunta tras la canción (B contesta con A); la llegada viene como `Dm_SY_Ocarina_move_Ed`, no como `OcarinaWarp_Arrive` (06-world toma las dos). Botones de la ocarina del remake leídos del juego: L = Re, R = Fa, A = La, Y = Si, X = Re agudo. `LICHII_COOP_WARP_INVITE=1` está encendido. |

### 07-cutscenes

| Escenario | Qué comprueba | Resultado | Rojos |
|---|---|---|---|
| `historia` (story) | el juego de A arranca el encuentro con el Árbol Deku; A es su dueño y manda `STORY_EVENT` start; B arranca el mismo evento | test9: **verde** (ronda 1; ronda 7); test10: **verde** | ninguno |

### 08-age

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `edad` (age) | A pasa a adulto con un comando de prueba; B le sigue; luego vuelta a niño | test9: **verde** (rondas 1 y 7); test10: **verde** (pasada de la 0.12) | **Verde hueco** (la fuente más reciente, nuestras notas de edad del 2026-11-13, por encima de las notas de las rondas): el juego rechazó el evento que pedíamos, `ReplaceEquipmentForEraChange`, 45 de 45 veces en las ejecuciones de test9 y 15 veces en el registro de A de la ejecución de test10 (`the game refused ... (returned 0)`), y cada intento ya había escrito la bandera de la era sin devolverla. El escenario no comprobaba que el cambio prendiera. Causa: el juego solo arranca por nombre un evento con fila en `RSDB/EventSetting`; ese es un subflujo. Arreglo en 08-age: pedir el cambio de era del propio juego (`Market_Child_Field_ChildToAdult` / `Market_Adult_Field_AdultToChild`) y no dejar nunca la era cambiada. |
| `edad-grupo-cerca` (age group, near) | con el arreglo de 08-age: A saca la espada; B, en la plaza del mercado, recibe el aviso y la cuenta 3-2-1, el cambio de era del propio juego, y aparece adulto en el Templo del Tiempo; los dos ven "Grupo reunido" | test10: **rojo 7/20** (2026-11-13) | Por los registros: todos los pasos ocurrieron, pero después de que acabara el guion de B (última marca de B a los 309 s, el movimiento entre 394 y 430 s), y la línea "age: on" se escribe al arrancar: las ventanas de las comprobaciones no las vieron (b2, b3). Arreglo previsto en 15-tools (guion de B más largo, `within boot..`). No se repitió. |

### 09-pvp

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `pvp-espada` (PvP sword) | el anfitrión enciende el PvP; A golpea a B con la espada: daño, invulnerabilidad justo tras un golpe | test10: **verde** (pasada de la 0.12) | Verde, pero la misma ejecución mostró que el escudo de B nunca paró un golpe: B no llevaba escudo (`shield ''`), y nuestra regla buscaba nombres de animación `Guard_*` en vez de las órdenes AS del juego (`ShieldGuard`, `GuardHit`, ...), y solo en la ranura principal. Arreglado en 09-pvp: hasta 8 ranuras AS, el botón de guardia con un escudo equipado, sin escudo = no para. |
| `pvp-escudo` (PvP shield) | B recibe el escudo Deku; A golpea tres veces: B de frente con ZL (parado), por la espalda (recibido), sin escudo (recibido) | test10: **rojo, lo mejor 5/8** (tres ejecuciones) | Ejecución 1: 0/8, B se congeló a 0 fps con 0,7 GB de RAM libre (b6). Ejecuciones 2 y 3: 5/8; solo uno de los tres golpes de A alcanzó a B (por la espalda, recibido como debe: `taken, takes 2; shield up (...) but the hit came from beside or behind it (cos -0.47)`), así que la parada de frente y el caso sin escudo nunca se probaron. Previsto: arreglar la colocación y los giros de A en el guion (15-tools). |

### 10-hud

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `comunicacion-aqui` (communication, "here!") | chat rápido: "¡Aquí!" con L + cruceta arriba, el límite de uno por segundo, "¡Ayuda!"; y que L está libre jugando | test9: **verde 18/18** (ronda 1; ronda 7) | ninguno (el valor: `invitation button L: free (3 of 3 presses moved nothing of our Link)`) |
| `mapa-flecha` (map arrow) | mapa de compañeros: B cerca (etiqueta con el nombre), lejos (punto y distancia), detrás (flecha en el borde, de su color) | test9: **verde 4/4** (ronda 1; ronda 7) | 3/4 una vez: línea de arranque (b3). Se ejecutó por el camino de reserva (`no minimap view or box in the map table: edge arrows and lines only`); calibrar el minimapa necesita una ventana visible (`mapa-piso`, no ejecutado). |

### 11-puppets

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `menu-pausa` (pause menu) | A abre el menú de pausa 8 s y cambia de escudo; la marioneta de A en B queda retenida (no escondida), se pone el escudo nuevo y vuelve a mostrarse | test9: **verde 15/15** (ronda 1; ronda 7) | 10/14 antes: el detector de pausa por fotograma quieto falla en el remake (un Link parado lee el mismo fotograma, así que cada espera parecía una pausa). Ahora manda primero la bandera del juego `Sequence_IsPause` (se pone 0,5 s después de abrir el menú); el fotograma quieto solo si no se puede leer la bandera (03-fixes, 11-puppets). |
| `cartel` (signboard) | A lee el cartel guía de Kokiri (un evento); B mira la marioneta de A, nunca escondida | test9: **verde 4/4** (ronda 2; otra vez en la primera ejecución del diseño nuevo de cuerpos); test10: **verde 4/4** tres veces en la pasada de la 0.12 (WebSocket con A primero, WebSocket con B primero, UDP) y otra vez 4/4 el 2026-11-13 con los cuerpos encendidos | Ronda 1: el archivo de reanudar (b1). Ronda 7 con A segundo: 2/4 en cinco tandas, el empuje de cuerpos viejo (12-bodies). |
| `levantar-vasija` (lift a pot) | A levanta una vasija, la lleva y la lanza; B la dibuja en las manos de la marioneta; se rompe en los dos juegos | test9: **verde 16/16** (ronda 7, A segundo); test10: **verde 16/16** | Levantar desde 1,1 m de cara a la vasija. La copia de B se rompía en las manos de la marioneta por el golpe de caída del propio juego, **1.000.000.000**, porque su cuerpo físico sigue cayendo entre nuestras colocaciones: vida subida mientras se lleva y 3 s después; una copia en manos de otro nunca manda su rotura; las copias que se llevan se quedan quietas poniendo a cero su velocidad lineal con la del juego (11-puppets). |
| `ocarina-mano` (Ocarina in hand) | B toca; A ve la ocarina propia de B en la mano de la marioneta y oye las notas y la Nana de Zelda | test9: **verde 17/17** (ronda 6; ronda 7) | Tocar es `DemoOcarina` en el remake, no `Ocarina...`; las partidas sintéticas no llevaban la ocarina en su botón (recetas arregladas); el anillo de 8 notas; familia a (marioneta sin piezas, la Nana desconocida); el sitio de la mano medido una vez y puesto cada fotograma (11-puppets). `LICHII_COOP_OCARINA_SOUND=1` está encendido. |
| `objetos-uso` (items in use) | A usa gancho, flechas, bombchu, martillo, magias, lente, botas de hierro; B ve cada uno en la marioneta | test9: **rojo 17/23** (ronda 4; ronda 7); test10: **rojo 17/23** | Visto: gancho, flechas, bombchu, martillo. Rojo: el Fuego de Din nunca se lanza (`ItemEmpty`, c1), las flechas elementales salen normales, la lente y las botas de hierro en ZR no cambian nada visible (d). c1, leído sin juego: `ItemEmpty` es la acción de fallo del cuerpo de arriba del jugador (`ExecutePlayerFailAction`), y el verbo `UseDinFire` nunca se pide; descartado con datos: la partida (MP 48 de 48, las dos bendiciones), la marioneta (ejecución de control), el sitio, el tiempo de pulsación, el proyectil. Sin arreglo todavía; siguiente sonda: engancharse a la petición del verbo secundario y registrar quién la pide. |
| `efectos-magia` (magic effects) | A lanza las tres magias y rueda; la marioneta en B juega los lanzamientos bajo la pose copiada | test9: **rojo 10/20** (ronda 7) | Arreglado: el Viento de Farore en la marioneta (el remake juega `DemoStartFaroreWind` / `DemoEndFaroreWindSetting`, que una regla `skip *Demo*` tapaba; 11-puppets). Rojo: Din y Nayru (c1) y la voltereta (`stick + A` corriendo no rueda en el remake; sin valor). |
| `sonda-magia` (magic probe) | las tres magias en el vestíbulo del castillo de Ganon, partida `20-magia` con la barra doble de verdad | test9: **rojo 3/8** (ronda 7) | c1. Farore funciona: `spawn: sent FaroreWindWarpPoint`, y en B `got FaroreWindWarpPoint`. |
| `sonda-magia-sin-marioneta` (magic probe, no puppet) | lo mismo con la marioneta de B apagada en A: el control | test9: **rojo 3/8** (ronda 7) | el mismo resultado que con la marioneta: la capa co-op no es la causa de c1 |
| `epona-montar` (ride Epona) | A monta a Epona; B ve la marioneta de A sobre un caballo | test9: **rojo 4/13** (ronda 1) | ninguna partida tenía a Epona junto a Link (d); sustituido por los dos de abajo |
| `epona-rancho` (ranch Epona) | lo mismo con la Epona del rancho (`12-epona-rancho`) | test9: **rojo, el último 5/13** (ronda 7) | Epona está en otro sitio del que decía la receta, y no se deja montar por ningún lado (A pulsado a 1,3-1,8 m por tres lados: ni evento ni animación) (d) |
| `epona-cancion` (Epona's Song) | A toca la Canción de Epona en el rancho (`12-epona`), ella viene, A monta | test9: **rojo 5/13** (ronda 7) | la canción se reconoce (`DemoOcarinaCorrect`) pero no aparece ninguna Epona (d); siguiente: llamarla en la Pradera de Hyrule. Sin altura del sillín medida (`LICHII_ORM_HORSE_SADDLE` sin valor). |

### 12-bodies

Se ejecutaron dos diseños.

**Diseño viejo (test9)**: empujar a nuestro Link fuera del cuerpo de un compañero. `cartel` y `charla` salieron en
rojo 2/4 con A segundo (la primera prueba de cuerpos y cinco tandas de la ronda 7); el mismo `cartel` con los cuerpos
apagados, `cartel-sincuerpos`, salió **verde 4/4** (control). La causa está en "Fallos" (Link anda en el sitio tras
`MoveLink`). Este diseño se dejó.

**Diseño nuevo**: un actor de cuerpo cinemático por compañero, colocado donde se dibuja la marioneta, que solo choca
con la capa del jugador; nuestro Link no se escribe nunca. Se enciende por sala con `/choque`.

| Escenario | Qué comprueba | test9 | test10 (2026-11-13) |
|---|---|---|---|
| `choque-bloqueo` (body block) | B pone `/choque on` y se queda quieto; A anda hacia los pies de B y debe pararse contra el cuerpo, y luego alejarse sin quedar encajado | primera ejecución: crear el cuerpo tumbó el juego (ver "Fallos"); ejecuciones 2-5: de 7/14 a 13/14 mientras se conseguía que el cuerpo apareciera y se volviera sólido (la causa de cada ejecución no está escrita); la última: **verde 14/14** | **verde 14/14** |
| `choque-encima` (on top) | A cae desde 1,5 m sobre la cabeza de B y debe quedarse de pie en ella | 4/10 y 9/10; la última: **verde 10/10** | **verde 10/10** |
| `choque-continuar` (Continue spot) | los dos aparecen en el mismo sitio de Continuar; nadie queda encajado, ningún `goto` se agota, el Link de nadie se mueve | 5/9, 7/9; la última: **rojo 8/9** (un `goto` agotado) | **verde 9/9** (la primera vista de la marioneta se dibuja 1 m a un lado y el cuerpo es sólido allí) |
| `cartel-sincuerpos` (signboard, no bodies) | `cartel` con los cuerpos apagados: control del diseño viejo | **verde 4/4** | no ejecutado |

En test10, `cartel` y `charla` también pasaron 4/4 con los cuerpos encendidos. Desde entonces los cuerpos van
encendidos por defecto; `/choque off` los apaga para la sala.

### 13-resume

| Escenario | Qué comprueba | Resultado | Rojos: causa y arreglo |
|---|---|---|---|
| `reanudar-sitio` (resume place) | A y B vuelven a su último sitio, dado por la app | test10: **rojo 16/19** (pasada de la 0.12) | Las líneas "resume: on" se escriben al arrancar (b3), y en test10 Link nunca se mueve a otra sala de una mazmorra (tu issue 197: una mazmorra carga sala a sala), así que el movimiento esperado a la repisa del tirachinas no podía ocurrir. De ahí la regla de mazmorra de 13-resume (vuelta al punto de la sala de la entrada donde estuvo de pie por última vez) y el escenario ahora espera eso. No se repitió. |
| `reanudar-mazmorra-1` (resume in a dungeon, session 1) | B guarda un punto en la sala de la entrada y un último sitio en otra sala | test10: 1/4, luego **verde 4/4** dos veces | las líneas de la entrada se leen ahora desde el arranque; la altura de aterrizaje en la repisa (15-tools) |
| `reanudar-mazmorra-2` (session 2, mismas instancias) | B vuelve a ese punto de la sala de la entrada, nunca a la otra sala | test10: 4/10, 8/10, luego **verde 10/10** | el arnés borraba `coop-resume.txt` entre las dos sesiones: `ORM_KEEP_RESUME=1` conserva el archivo de la sesión 1 (15-tools) |

## Fallos que encontramos en caminos de código de upstream

**1. Un anfitrión en la pantalla de título manda la tabla de banderas de una partida nueva (arreglado en 03-fixes).**
B entra primero y pasa a ser anfitrión estando aún en la pantalla de título; como anfitrión manda su tabla de banderas,
que en la pantalla de título es la de una partida nueva (3 corazones, sin magia, sin la bendición). El servidor se queda
con la primera tabla que recibe (una tabla posterior que choca no la sustituye, `flag_clashes`) y se la da a A al
entrar; A carga su partida y su ventana de carga de invitado lo devuelve todo a esa tabla. Después el servidor pasa el
papel de anfitrión a A, y el estado roto de A pasa a ser el del grupo (y puede guardarse). Con amigos pasa cuando alguien
pulsa Continuar antes que el anfitrión. Firma en todas las tandas en que A entró segundo con una partida de más de 3
corazones (31 parejas, ninguna con A primero):

```
coop: objects: our Link's life 12 of 12, MaxLife flag 12
```

El arreglo: un anfitrión sin partida cargada retiene su tabla hasta que lee a Link, y la primera carga de un invitado
desde la pantalla de título, sin valores del grupo todavía, conserva los valores de su propia partida:

```
coop: flags: we host with no save in (the title screen): our table is held until our save is in, so no joiner gets a new game's flags (3 hearts, no magic)
coop: flags: ... our table sent: our save is in now
-- A: first life 44 of 44 = its save's 44 (ok)
```

Confirmado en la ronda 7: 50 parejas (100 juegos) con A segundo, cada primera vida igual a la de su partida, ningún
`save mismatch`; también en test10. Esta era además la causa escondida de otros rojos (la marioneta de la ocarina sin
piezas, la Nana desconocida, la magia a 0).

**2. La entrada en la tabla de tamaños de un parámetro Phive nuevo (arreglado en 12-bodies, el constructor del paquete
de marionetas).** El juego analiza el parámetro Phive de un actor dentro de un montón del tamaño de su entrada en la
tabla de tamaños de recursos, y las entradas del propio juego están unos 5,1 KB por encima del archivo y crecen con lo
que lista el parámetro, no con sus bytes (el `ControllerSetParam` de los Kokiri, 2.688 bytes: 10.624; el del jugador,
15.256: 34.160; un `ShapeParam` de 548: 6.600; el `ControllerSetParam` de 608 bytes del muro invisible: 7.480, 3,9
veces sus bytes sobre la base). Con 3 veces los bytes, nuestro `ControllerSetParam` de 924 bytes (dos cuerpos, dos
formas) recibía 7.892: demasiado poco; el análisis devolvía un objeto nulo y la creación tumbaba el juego:

```
body coop: puppet: P1: create CoopPuppet_Child_Body requested (returned 0x1), ...
Cannot execute instruction at unmapped address 0x00000000000000
body coop: puppet: P1 CoopPuppet_Child_Body not spawned after 10000 ms (actor 0, state -); trying again in 5 s
coop: check: bodies: collider fallback a body could not be made 2 times (see the 'body coop: puppet:' lines): the mates are drawn aside (CoopPuppet_Child_Body)
```

La entrada es ahora 0x1400 + 8 veces los bytes (por encima de todas las entradas del romfs que miramos). Después, en
test10: `coop: check: bodies: collider ok CoopPuppet_Child_Body made: kinematic, placed with its puppet each frame, hitting only our Link`.
Le importa a cualquiera que añada al paquete de marionetas un actor con su propio parámetro Phive.

**3. Las matrices de mundo del esqueleto no tienen posiciones (03-fixes).** El censo de huesos leyó el arreglo de
matrices del esqueleto como 3x4 cada 0x30: 301 de 309 son giros sin traslación (todos en el actor), y como 4x4 con la
posición en 12-14, ninguno. Así que la búsqueda del hueso más cercano daba siempre el hueso 0. El buscador del
esqueleto toma ahora un arreglo 4x4 si una versión del juego lo tiene y si no se queda con el 3x4, y dice cuál
(`carry bone ok ... (3x4 (old))`).

**4. Link anda en el sitio tras varios `MoveLink` seguidos (sin explicar; evitado).** Con el empuje de cuerpos viejo,
cuando los dos jugadores pulsaban Continuar en el mismo sitio, nuestro Link se movía con `MoveLink` (el movimiento del
tirón del jefe) 13 veces en 2 s (`coop: boss: moved Link to -82.1 122.3 33.5`) y después andaba en el sitio
(animación `Move`, posición fija) un minuto entero; todos los `goto` se agotaban. Con los cuerpos apagados el mismo
escenario pasaba 4/4. También pasaba con A primero (48 veces en una tanda) sin el atasco. Por qué `MoveLink` deja a
Link sin poder avanzar no está encontrado; el diseño nuevo de cuerpos nunca escribe a nuestro Link, así que no lo pisa.

**5. Encontrado al fusionar test10 (arreglado en 03-fixes o en la fusión).**
- Tus pruebas nuevas de red escuchaban en todas las interfaces: `relay_test`, `nat_loopback_test`, `small_path_test` y
  `encrypt_loopback_test` escuchan ahora en 127.0.0.1.
- Con tu tráfico sellado sobre nuestro transporte WebSocket, un mensaje de tamaño máximo cerraba el WebSocket: los
  límites de tamaño tienen que sumar `kSealOverheadBytes`.
- El `Set-KitDefaultColor` del kit en `play.ps1` pisaba el color por jugador de nuestro lanzador: el color de
  `coop.ini` se exporta como `LICHII_COOP_COLOR`.
- `dumpcheck.py` no conocía a Link Oscuro, así que un `.nup` perdía las marionetas de fijar ("Fijar aliados").

### Comportamientos del juego que medimos (útiles sea cual sea el código que los use)

- El juego solo arranca un evento por nombre si tiene fila en `RSDB/EventSetting`; un subflujo como
  `ReplaceEquipmentForEraChange` se rechaza (`returned 0`). `CrumblingGanonCastle_Arrive`,
  `KokiriForest_Meet_DekuTree_MeetDekuTree_01`, `WaterTemple_WaterLevelChange0x` y `Market_Child_Field_ChildToAdult`
  se tomaron (`returned 0x1`).
- Un Link parado lee el mismo fotograma de animación, así que un fotograma quieto no es una pausa; `Sequence_IsPause`
  se pone unos 0,5 s después de abrir el menú.
- Tocar la ocarina es `DemoOcarina` (ranuras 4-5 `DemoOcarinaFace/Finger`); la comprobación de canciones compara las
  últimas notas de un anillo de 8; tras una canción de viaje viene una pregunta; la llegada del viaje puede ser
  `Dm_SY_Ocarina_move_Ed`.
- Los cofres no tienen orden de "obtener objeto": el que abre sigue en `DemoOpenTreasureBox`; el `FrontOpen` de la tapa
  empieza en el mismo fotograma que el movimiento de Link; la bandera de "abierto" llega cuando acaba el evento del
  cofre.
- El nivel del Templo del Agua se aplica en la siguiente carga de la sala, no en vivo.
- Una copia física que se lleva sigue cayendo entre colocaciones y recibe un golpe de caída de 1.000.000.000.
- El Fuego de Din y el Amor de Nayru acaban en `ExecutePlayerFailAction` (`ItemEmpty`) en nuestras ejecuciones; el
  Viento de Farore solo pone su punto dentro de una mazmorra (fuera acaba en `EvtCaution`).
- Cada juego emulado reserva hasta 14 GB de memoria del invitado: dos juegos bajan un PC de 32 GB de 16-18 GB libres a
  menos de 1 GB.

### Defectos de nuestras propias ramas, encontrados al integrarlas (no de upstream)

Listados por completar (`T9_INTEGRADO.md`): un enemigo que se paraba no mandaba muestra, así que las copias seguían 150
ms (0,5 m) y volvían de un salto (una parada cuenta ahora como cambio); la lente de la verdad se leía después de la
armadura puesta y la tapaba; un espacio de nombres `orm::` ambiguo en una prueba de servidor; nombres de prueba con `;`
que ctest nunca ejecutaba, un `<ostream>` que faltaba y otras pruebas que nunca se habían compilado; el gancho pedía
`on()` a dos clases que no lo tenían (encontrado compilando el gancho con las banderas del emulador, ya que ctest no
compila el gancho); y tres ramas usaban el mismo bit de clave en las copias del director de aparición.
