# Ideas que tuvimos y no terminamos (o no confirmamos en una partida)

Estas son las ideas de nuestro trabajo que no están terminadas, o que funcionan en las pruebas unitarias pero aún no se
han confirmado en una partida real. Para cada una: qué vería el jugador, cómo la implementaríamos (o cómo está hecha
hoy), qué se sabe, qué no se sabe y la sonda exacta que la confirmaría. Toma lo que quieras, cámbialo o ignóralo.

Palabras de estado que se usan aquí:

- **confirmado en partida**: visto con dos jugadores en una partida real (dos juegos ocultos), con el nombre del
  escenario.
- **sin confirmar en partida**: solo con pruebas unitarias, o el escenario aún no ha pasado.

Los escenarios viven en `tools/orm/harness/escenarios/` (`<nombre>.A.txt` y `<nombre>.B.txt`, un guion por juego).
Cómo corre un escenario está en [HOW-WE-WORK.md](HOW-WE-WORK.md). Las partidas sintéticas (`#! saves <bloque>`) las
construye `tools/orm/saves/`.

| # | Idea | Función | Estado |
|---|---|---|---|
| 1 | Cambio de edad con aviso al grupo | 08-age | sin confirmar en partida (la primera tanda salió en rojo por sus ventanas de tiempo; las líneas del registro parecen correctas) |
| 2 | Choque real, subirse encima de un compañero | 12-bodies | confirmado en partida para bloquear, estar de pie en una cabeza y Continuar; lo demás aún no |
| 3 | Interruptores de suelo sostenidos | 06-world | confirmado en partida en una sala (Templo del Fuego, sala 10); los demás aún no |
| 4 | Cuenta atrás de la huida para el grupo | 05-bosses | sin confirmar en partida (no se ha encontrado la cuenta de verdad) |
| 5 | El Fuego de Din y el Amor de Nayru acaban en `ItemEmpty` | 11-puppets, 09-pvp | investigación abierta, sin arreglo |
| 6 | Reanudar dentro de una mazmorra con la regla del issue #197 de upstream | 13-resume | confirmado en partida (dos sesiones seguidas); faltan algunos bordes |
| 7 | Rendimiento con seis marionetas | 01-platform, 15-tools | medido solo con bots; la prueba de carga de seis nunca corrió |
| 8 | Capa fina para Hookshot | 01-platform | capa hecha; no compilada en el módulo de Hookshot, sin partida cruzada |

---

## 1. Cambio de edad con aviso al grupo (08-age)

**Qué ve el jugador.** Cuando un jugador saca o devuelve la Espada Maestra, todos los demás le siguen con el cambio de
era del propio juego y aparecen en el Templo del Tiempo con la edad nueva. Nadie se lleva una sorpresa: primero un aviso
grande en el centro de la imagen ("<Nombre> ha cambiado de época: moviendo al grupo al Templo del Tiempo..."), luego una
cuenta 3, 2, 1. El que sacó la espada ve "Moviendo al grupo contigo..." y, cuando todos tienen la edad nueva, "Grupo
reunido en el Templo del Tiempo". Los textos están en español en el código; son cadenas cortas, fáciles de cambiar.

**Cómo está hecho.**

- Cada juego ya manda su edad (`age` en `PLAYER_SAMPLE`). La edad de un compañero cuenta tras 2 s estable, sin estar
  cargando y coincidiendo con el sufijo de su escena (`KokiriForest_Child` / `_Adult`).
- Para seguirle, pedimos el flujo de cambio de era del propio juego en la dirección que toca
  (`Market_Child_Field_ChildToAdult` o `Market_Adult_Field_AdultToChild`). El juego cambia el equipo, pone la era y
  carga él mismo el Templo del Tiempo.
- Nunca en mitad de algo: nadie se mueve durante un evento, una cinemática o una charla, durante el combate de un jefe
  (sala cerrada) ni estando caído. Un jugador que espera lleva un bit nuevo de estado, `age_waits` (bit 6 de
  `PLAYER_STATUS`), y los demás muestran "Esperando a <Nombre>...".
- Si falla (el juego rechaza el evento 3 veces, la carga no llega): se reintenta a los 10 s; a la segunda vez seguida
  se abandona. Nadie se mueve, la era queda la de nuestro Link y un aviso lo dice.
- Interruptor: `LICHII_ORM_AGE` (apagado en las opciones del kit hasta que una partida lo confirme; las sondas lo
  encienden). `LICHII_ORM_AGE_EVENT=<evento>` mantiene el camino antiguo de "recargar en el sitio", solo para pruebas.

**Qué se sabe.**

- Nuestro primer enfoque pedía por nombre el evento de cambio de equipo `ReplaceEquipmentForEraChange` y recargaba la
  misma escena. El juego lo rechazó 45 veces de 45. Leer el romfs explicó por qué: el juego solo arranca por nombre un
  evento que tiene fila en `RSDB/EventSetting`, y ese es un subflujo de los flujos de cambio de era, no un evento propio.
- Peor aún, ese primer enfoque escribía `Sequence_IsChildhood` antes de pedirlo y no lo devolvía nunca: Link se quedaba
  niño con la era de adulto en su partida. Arreglado: nada se escribe antes de que corra nuestro evento, y si la recarga
  no prende después de que el evento corriera, la era se devuelve (línea de registro `Sequence_IsChildhood put back to
  child (our Link's age)`).
- En la primera tanda del camino nuevo (`edad-grupo-cerca`, B en la plaza del mercado), el registro de B muestra la
  cadena entera: el juego tomó el flujo de cambio de era pedido lejos del pedestal, la cuenta 3-2-1, `now adult in
  Market_Adult, 32.7 s after the reload began`, y los dos juegos dicen que el grupo está reunido. **El escenario sigue
  en rojo** (7 de 20 comprobaciones), porque varias comprobaciones miran en la ventana de tiempo equivocada (por
  ejemplo, la línea de "interruptor encendido" se escribe al arrancar, antes de la marca `start` del escenario). Así
  que está **sin confirmar en partida**.

**Qué no se sabe.**

- Lo mismo con B lejos (`edad-grupo-lejos`, B en el Bosque Kokiri), y la vuelta a niño.
- Si el aviso grande se lee bien y no tapa nada importante (captura `told` de B).
- Si la cinemática de llegada deja alguna vez a B a medias.
- Un jugador caído que tiene que esperar y se mueve después de que lo revivan.

**La sonda que lo confirma.**

1. Arreglar las ventanas en `edad-grupo-cerca.A/B.txt` (`within boot..` para la línea del arranque, las líneas de
   llegada dentro de las marcas que vienen después) y correrlo otra vez:
   `python -I tools/orm/harness/r03_pareja.py --scenario edad-grupo-cerca --transport ws --scen-dir tools/orm/harness/escenarios --capture-marks`
2. Después `edad-grupo-lejos`, y una pareja de vuelta a niño (A devuelve la espada).
3. Líneas esperadas: en A `coop: age: we changed age ourselves (child -> adult): the group follows us`; en B
   `coop: age: notice countdown: ... / 3` (2, 1), `the game took 'Market_Child_Field_ChildToAdult'` o
   `event running ...: the game's era change to Market_Adult runs`, y luego `now adult in Market_Adult`; en los dos
   `every mate is our age: the group is gathered`. Nunca: `given up`, ni `Sequence_IsChildhood <-` antes de que corra
   un evento.
4. Mutante: la misma pasada con `LICHII_ORM_AGE=0` debe salir en rojo.

---

## 2. Choque real y subirse encima de un compañero (12-bodies)

**Qué ve el jugador.** Con `/choque on` (cualquiera de la sala; `/choque` solo dice cómo está; `/cuerpos` y `/bodies`
valen igual) tu Link choca con tus compañeros como con un aldeano, y puedes estar de pie sobre la cabeza de un
compañero. Al Continuar en el mismo sitio, o tras un viaje, nadie queda encajado: el cuerpo del otro es un "fantasma"
hasta que estáis separados.

**Por qué existe.** Antes, cuando dos Links estaban en el mismo sitio (Continuar pone a todos en el mismo punto
guardado), nuestra regla antigua escribía la posición de nuestro Link para sacarlo. Esa escritura se salta la colisión
del juego: Link acababa al otro lado de una barandilla y andaba en el sitio un minuto. Desde entonces **la regla de
cuerpos no mueve nunca a nuestro Link**. La reserva de hoy es solo visual: la marioneta de un compañero que se nos
solapa se dibuja 0,7 m apartada (su posición real se sigue usando para todo lo demás).

**Cómo está hecho.**

- Un actor aparte por compañero, construido en el paquete de marionetas desde el romfs de cada jugador:
  `CoopPuppet_Child_Body`, `CoopPuppet_Adult_Body` (lados y un disco en la cabeza) y `CoopPuppet_*_BodySide` (solo
  lados). Es cinemático, sin modelo, sin vida y sin IA, y solo choca con la capa `Player`.
  - Lados: el propio prisma del jugador (`Player_cct`: radio 0,23, alto 1,17 de niño; 0,35 / 1,68 de adulto) en la capa
    `Character`/`NPC` con `Material_Character` (el material de los aldeanos, que es `Unridable`).
  - Disco de la cabeza: 0,12 m de grosor, en la capa `GroundObject`, con un material pisable (el que usan las
    plataformas que se mueven).
- En cada fotograma el cuerpo va exactamente donde se dibuja la marioneta, por el colocador de matrices del propio
  juego, para que la física tome el sitio. Los cuerpos se crean aparcados bajo el suelo, así que nunca aparecen encima
  de nadie.
- Regla del fantasma: un cuerpo pasa a sólido tras 0,8 m de separación (o uno encima de otro) durante 0,3 s; vuelve a
  fantasma a menos de 0,25 m, o si el compañero está caído (para revivirle de rodillas), montado, oculto, o sus
  muestras saltan más de 3 m (un viaje).
- Interruptores: `/choque` (interruptor de sala); `LICHII_ORM_BODY_COLLIDER` = `room` (por defecto), `1` (siempre
  encendido, para las sondas), `0` (nunca); `LICHII_ORM_BODY_TOP=0` quita el disco de la cabeza.
- Reserva: si el paquete no tiene cuerpos o un cuerpo no se puede crear, las marionetas se dibujan apartadas como
  antes, y `/choque` lo dice en el chat. Registro: `coop: check: bodies: collider ok ...` o `collider fallback <por
  qué>`.

**Confirmado en partida** (dos juegos ocultos, WebSocket, protocolo 110):

| Escenario | Resultado | Lo que se vio |
|---|---|---|
| `choque-bloqueo` | PASS 14/14 | A se para a 0,44 m de B (los dos cuerpos: 0,46) y se aleja sin quedar encajado |
| `choque-encima` | PASS 10/10 | `our Link stands on P1's head` |
| `choque-continuar` | PASS 9/9 | los dos en el mismo sitio de Continuar: marioneta dibujada 1 m a un lado, el cuerpo sólido ahí, nadie movido |
| `cartel`, `charla` | PASS 4/4 cada uno | el atasco antiguo de "andar en el sitio" no vuelve |

**Qué no se sabe.**

1. Dos cuerpos con mucho ping: cada juego para a su propio Link en el cuerpo del otro; con mucho retardo puede haber
   un pequeño empujón.
2. Si el disco de la cabeza te lleva cuando el compañero de abajo anda (velocidad de superficie).
3. Qué ve B con A sobre su cabeza (el cuerpo de A toca la cabeza de B desde arriba; puede empujar a B a un lado).
4. Que la espada, las flechas y la cámara ignoren el cuerpo (deberían: solo choca con `Player`).
5. Seis jugadores, y la variante que resbala con `LICHII_ORM_BODY_TOP=0`.

**Sondas que lo confirmarían.**

- `choque-encima` con `--env LICHII_ORM_BODY_TOP=0`: A debe resbalar (ninguna línea `stands on`).
- Una pareja nueva `choque-llevar` (por escribir): A sobre la cabeza de B, B anda 3 m; se espera que la posición de A
  siga a B, y nunca `moved Link to` en ninguno de los dos juegos.
- `choque-bloqueo` con retardo añadido (el guion de latencia del arnés, `tools/orm/harness/latencia.ps1`), y después
  mirar las capturas.
- Idea para más adelante: un cuerpo así también podría pulsar interruptores sostenidos (idea 3) si estuviera en una
  capa que el sensor del interruptor ve y el jugador no. Sin probar.

---

## 3. Interruptores de suelo sostenidos (06-world)

**Qué ve el jugador.** "Uno se queda de pie en el interruptor y el otro pasa por la puerta que mantiene abierta." A se
pone en un interruptor de suelo sostenido; en el juego de B el interruptor se hunde y la puerta (o el muro de fuego, o
la reja) se abre igual que en el de A. Mientras alguien siga encima, sigue pulsado en todos los juegos. Cuando el
último se baja, se va de la escena o se desconecta, se suelta en todos. Los interruptores con tiempo siguen su propio
reloj al soltarse, como en el original.

**Cómo está hecho.**

- Regla (como en nuestro cooperativo anterior en lockstep para el juego de N64): en mi juego, un interruptor está
  sostenido por otros mientras los pies de algún otro jugador de mi escena están dentro de su zona. **Sin mensaje
  nuevo, sin cambio de protocolo ni de servidor**: cada juego decide lo mismo con las posiciones que ya tiene (las
  mismas con las que se dibujan las marionetas). La salida y la desconexión salen solas.
- La zona es el sensor del propio juego (`CheckObjectOn` del paquete del actor: una caja de 0,5 x 0,5 m en `A_03`, un
  cilindro de 1 m en Jabu-Jabu), bajado hasta el botón pulsado y girado con la colocación.
- Una tabla de 14 interruptores leída del romfs (Cueva de los Dodongos 02 y 19, Templo del Fuego 08, 10 y 13, Templo
  del Espíritu 08 y 23, Templo del Agua 14, Jabu-Jabu 07, 14 y 17, Árbol Deku, Campo de Entrenamiento Gerudo). El
  Templo del Bosque y el Pozo no tienen ningún interruptor sostenido en el remake.
- Dos maneras de pulsarlo en mi juego, solo mientras mi propio Link no está encima:
  1. la bandera "Link encima" del interruptor, donde la hay (se escribe 1, se vuelve a escribir si la sala la pone a 0,
     se pone a 0 al soltar; nunca bajo mi Link);
  2. las palabras de estado del propio actor (escritas cada fotograma, solo los bits de la máscara). **Todavía no se
     conoce ninguna**: las encuentra una sonda.
- Interruptores: `LICHII_ORM_HELD_SWITCHES` = `flag` (por defecto: pulsa con la bandera y mira la puerta cercana), `log`
  (solo registra), `1` (bandera y palabras), `0` (apagado). `LICHII_ORM_HELD_SWITCH_LIST=<archivo>` añade líneas
  `switch`/`word`/`off`. `LICHII_ORM_HELD_SWITCH_PROBE=1` registra las palabras del actor que cambian cuando nuestro Link
  pisa y se baja.
- Autocomprobación tras cada pulsación, en B: `held switch: ...: door opened with the flag (...)` o
  `door did not move 4.0 s after the flag: needs the actor words`.

**Qué se sabe.**

- **Confirmado en partida: Templo del Fuego, sala 10** (`meca-sostenido-fuego`): el registro de B dice `FireTemple
  room10 held by another player (P2): pressed here`, luego `door opened with the flag
  (Obj_FireTemple_FireWallTrap_A_01 went, 3.4 s after the press)`, luego `let go here`.
- **Templo del Espíritu, sala 08** (`meca-sostenido-espiritu`): el escenario pasa sus comprobaciones (B pulsa y suelta
  con la bandera), pero B también dice `door did not move 4.0 s after the flag: needs the actor words`. Así que ahí la
  bandera sola no basta.
- La sonda de palabras no encontró nada en la copia en reposo del actor; la sonda general de objetos ve cambiar unas
  40 palabras cuando Link pisa, aún sin separar del ruido.

**Qué no se sabe.**

1. Las palabras de estado de cada tipo de interruptor (`A_03`, `A_03_Amos`, el del Templo del Agua, los de Jabu-Jabu,
   `A_02`).
2. Templo del Agua, sala 14: el interruptor tiene tres banderas (`SwitchPushed`, `SwitchPushedByBlock`,
   `SwitchPushedHeavyBoots`). En nuestras tandas Link flota y nada sobre él: el modo pesado de las botas aún no se
   alcanza por guion. Puede ser un interruptor de bloque, no de botas.
3. Jabu-Jabu sala 17 (la piedra) y la Cueva de los Dodongos 02/19, Árbol Deku: aún sin correr.

**La sonda que lo confirma.**

1. Para encontrar las palabras basta un juego: `LICHII_ORM_HELD_SWITCH_PROBE=1 LICHII_COOP_OBJECTS=1`, Link al lado de
   un interruptor de la tabla (escenario `meca-sostenido-espiritu`, partida `19-espiritu-adulto`). Pisar, esperar 1 s,
   bajarse; dos o tres veces. Cada paso deja líneas `held switch: probe:   word <actor> <sitio> on=0x.. off=0x..`.
2. Las palabras que se repiten en todos los pasos van a un archivo para `LICHII_ORM_HELD_SWITCH_LIST`; correr la pareja
   con `LICHII_ORM_HELD_SWITCHES=1`. Se espera en B `pressed here` y `door opened` (no `door did not move`).
3. Mutante: `LICHII_ORM_HELD_SWITCHES=log` (B dice "would be pressed": la comprobación de `pressed here` sale en rojo).

---

## 4. Cuenta atrás de la huida para el grupo (05-bosses)

**Qué ve el jugador.** Después de Ganondorf el castillo se derrumba y corre una cuenta atrás. Con seis jugadores todos
deberían ver **la misma** cuenta, y quien llega tarde recibe la cuenta del grupo, no una nueva.

**Cómo está hecho.**

- Regla (de nuestro cooperativo anterior): el comienzo del grupo es el primer evento de historia
  `CrumblingGanonCastle_Arrive` de la sala (la hora del servidor de su `STORY_EVENT`). Si nuestra cuenta muestra más de
  2 s por encima de la del grupo, se pone la del grupo. Nunca más tiempo, nunca 0 (en 0 valen los puntos de reinicio
  del propio juego), y nunca se toca si la nuestra es más larga que toda la duración (entonces la duración está mal, y
  el registro lo dice una vez).
- Interruptores: `LICHII_ORM_ESCAPE_CLOCK=1` (apagado por defecto);
  `LICHII_ORM_ESCAPE_CLOCK_WORD=main+0x<hex>[,0x<hex>...]:f32|u32|ms` (dónde vive la cuenta, a mano);
  `LICHII_ORM_ESCAPE_CLOCK_LENGTH_S` (180 por defecto); `LICHII_ORM_ESCAPE_CLOCK_SEARCH=0` apaga la búsqueda.
- Una búsqueda con el juego en marcha: con el interruptor encendido y sin palabra dada, tras el evento del derrumbe
  muestrea una vez por segundo las palabras del módulo GameUI (el que dibuja el temporizador) y de los objetos a los que
  apunta que se leen como segundos; se toma la que baja 1 s por segundo durante 5 s. Tres intentos fallidos: `our
  countdown was not found (...): the group's clock goes on without the game's timer` (no se escribe nada).

**Qué se sabe.**

- El romfs dice que la huida usa el temporizador de minijuegos (`EventTriggerMiniGameTimerStart` y
  `EventTriggerShowTimerUI` en el flujo `Defeat_Ganon`). Ninguna bandera GameData lleva el valor vivo:
  `MiniGame_TimerValue` y `MiniGame_TimerMinute/Second/MilliSecond` son el resultado que escribe un minijuego al acabar.
- Con la partida sintética `21-huida-arriba` (Ganondorf vencido, en lo alto del castillo que se derrumba) el juego toma
  el derrumbe.
- La búsqueda encuentra palabras que bajan, pero **son la copia de la interfaz**: una escritura se toma un fotograma y
  luego la copia vuelve a su propio valor. El arreglo de seguridad ya está: una escritura que el juego devuelve cuenta
  como no tomada, y tras 3 la palabra se deja.

**Qué no se sabe.** Dónde vive la cuenta de verdad, su duración exacta, y qué hace una cinemática de piso a un jugador
que está en otro piso.

**La sonda que lo confirma.** `final-huida-reloj` (partida `21-huida-arriba`). Se espera en B
`escape clock: ours shows N s, the group's M s (it started K s ago): set to M`; nunca en A `escape clock: ... set to`
(A la empezó). Una idea para la próxima búsqueda: partir del objeto de la acción que arranca el temporizador en el
juego (el nodo de evento que arranca el temporizador de minijuegos) en vez del módulo de la interfaz.

---

## 5. El Fuego de Din y el Amor de Nayru acaban en `ItemEmpty` (11-puppets, 09-pvp)

Nos importa porque las marionetas muestran los hechizos (el doble del Fuego de Din), y nuestras reglas de PvP usan el
Amor de Nayru como escudo. Pero en nuestras tandas con guion los hechizos no arrancan nunca.

**El síntoma.** Con una partida con magia 48 de 48, las dos bendiciones de la Gran Hada y los objetos conseguidos
(`sonda-magia`, partida `20-magia`), pulsar el Fuego de Din o el Amor de Nayru juega `ItemEmpty` (la animación de "no
pasa nada"), con un toque o manteniendo 2 s. El Viento de Farore funciona.

**Descartado con datos.** La partida (varias partidas, la barra de magia buena), la marioneta
(`sonda-magia-sin-marioneta`: igual sin marioneta en el juego), el sitio (Kakariko y el castillo), el tiempo de
pulsación, el proyectil (está en la tabla de disparos del jugador) y los atributos de verbo.

**Lectura sin juego (romfs y el código del juego).**

- `ItemEmpty` no lo elige el planificador del propio Din: es la **acción de fallo del cuerpo de arriba**, que elige el
  selector de acciones secundarias. `StartDinFire` / `StartNayruLove` no salen nunca, así que el verbo principal
  `UseDinFire` no se llega a pedir: el cerebro del jugador (código C++, no un archivo de IA) pide la acción de fallo al
  pulsar el botón.
- La diferencia de datos con Farore: Din y Nayru son `PouchCategory: Material` con `IsNotOpenStockNumDisplay` y van por
  el camino de objeto del cerebro; Farore va por su evento residente, que se salta esa comprobación.

**No se sabe.** Qué comprobación falla. No tenemos arreglo, y nada de nuestras partidas, guiones ni gancho se apoya
todavía en una causa.

**La sonda siguiente (un minuto más o menos).** Un gancho de lectura en la petición del verbo secundario que registre
la pila de llamadas cuando el verbo pasa a la acción de fallo tras pulsar el botón con el Fuego de Din: la función que
lo pide es la comprobación que falta. Después el escenario `sonda-magia` debería mostrar en A `you play '...Din...'` y
`spawn: sent ExpandFireField`, en B `spawn: got ExpandFireField`, y nunca `you play 'ItemEmpty'`. Una comprobación
barata de paso: la misma partida en Hookshot, para ver si es propio del emulador (sin probar).

---

## 6. Reanudar dentro de una mazmorra con la regla del issue #197 de upstream (13-resume)

**Qué ve el jugador.** Un jugador que vuelve regresa a su último sitio. Dentro de una mazmorra, si ese sitio estaba en
otra sala que no es la de la entrada, vuelve a la mazmorra pero **a la sala de la entrada**, en el punto donde estuvo
de pie por última vez antes de salir de ella (el paso hacia el resto de la mazmorra), con el aviso "De vuelta a la sala
de entrada, por donde saliste: tu sitio está en otra sala". Si ese punto no se guardó (otro PC, un archivo antiguo), se
queda en la entrada.

**Por qué no a la sala exacta.** Una mazmorra es una sola escena que el juego carga sala a sala. Upstream vio (issue
#197, la partida `bigrun4`) que teletransportar a otra sala deja a Link quieto con la imagen en negro (la sala no está
cargada), y desde entonces upstream no mueve a Link a otra sala de una mazmorra. Un cambio de escena solo puede
aterrizar en un punto de inicio, y en todas las mazmorras los puntos de inicio están solo en la sala de la entrada (los
demás son de cinemáticas). El cambio de sala del propio juego (una puerta) no tiene petición conocida. Así que el punto
útil más seguro es uno de la sala de la entrada.

**Cómo está hecho.** Mientras juega, el juego recuerda el último punto donde Link estuvo de pie en la sala de la
entrada de la mazmorra y lo guarda en una línea aparte del archivo local de reanudar
(`d1;sala;nombre;mazmorra;inicio;x;y;z;edad;hora`, solo en este PC). Al volver: un cambio de escena a ese punto de
inicio y, al asentarse, se mueve a Link a ese punto, y el movimiento se comprueba contra donde de verdad le dejó la
carga; si no está en la misma sala, se queda donde está. Registro: `coop: resume: the last place (...) is in another
room of this dungeon (issue 197: ...): going to the spot of its entrance room we last left it by`, luego `at the spot
of the entrance room of <mazmorra> we last stood on before leaving it`; último recurso `coop: resume: not moved to ...
staying here`.

**Confirmado en partida:** `reanudar-mazmorra-1` y después `reanudar-mazmorra-2` (dos sesiones seguidas con las mismas
instancias; `ORM_KEEP_RESUME=1` conserva el archivo de la sesión 1), las 10 comprobaciones de la sesión 2 cumplidas,
nunca un movimiento a la repisa de la otra sala, nunca `not moved to`.

**Qué no se sabe.**

- Nuestro viaje de reanudar todavía pide el cambio de escena con `start_event = true`; con el issue #197 en mente, es
  un riesgo a vigilar en partida real.
- Que la vuelta no choque con la invitación de las canciones ni con la regla de las salas de jefe.
- Una sesión real con dos PC (la tanda de arriba usa el archivo de un solo PC).
- Una idea, sin empezar: si alguien encuentra la petición que hay detrás del cambio de sala del propio juego (las
  puertas), el jugador podría volver a la sala exacta sin riesgo.

**La sonda que confirma lo demás.** La misma pareja tras jugar de verdad; el mutante es borrar la línea `d1` del
archivo de B antes de la sesión 2: se espera `no spot of its entrance room kept, going to its entrance`.

---

## 7. Rendimiento con seis marionetas (01-platform, 15-tools)

**Lo que medimos** (un juego oculto a media resolución, modo 60 fps, Bosque Kokiri, Link andando en bucles; cada
corrida tiene su propia referencia de 60 s "solo"):

| Corrida | fps solo -> con 5 bots | Lectura |
|---|---|---|
| bots a 8 m, 30 poses/s, PC libre | 61 -> 51 | la referencia buena |
| los bots no mandan pose (marionetas animadas por el juego) | 31 -> 39, 52 -> 57 | no cae |
| sin marionetas (`LICHII_COOP_PUPPET=0`), cinco jugadores en la sala | 58 -> 58 | la red, la presencia y los marcadores no cuestan |
| bots a 150 m | 59 -> 64 | las marionetas fuera de la vista no cuestan |
| el mismo juego con compilaciones en marcha en el PC | 46-53 -> 23-34 | la mayor pérdida es otra carga en el PC |

- Nuestra copia de pose de cinco marionetas cuesta 0,07 ms por fotograma (un banco sin el juego): un 2 % de lo que se
  pierde.
- Lo que cuesta es el juego animando y dibujando Links de más cuando están cerca y a la vista, bajo emulación de CPU.
- Sospechamos que la cifra de "10 fps por cinco marionetas" está inflada por los bots: mandan una pose sintética con
  todos los huesos marcados como "no identidad", lo que obliga a hacer la cuenta completa de cada hueso. Los jugadores
  reales mandan las marcas del propio juego.

**Qué está hecho.** Un registro de tiempos por fotograma (`LICHII_ORM_FRAME_LOG=<archivo.csv>`, una línea por
fotograma presentado, las mismas columnas que el registro de fotogramas de Hookshot) y un medidor
(`LICHII_ORM_PERF=1`: cada 5 s, los ms por fotograma de cada parte del gancho, `coop: perf: ...`). Una prueba de carga
de seis (`tools/orm/harness/carga6.ps1`: Kokiri, mercado, Árbol Deku, una pareja más 4 bots y una corrida de 30
minutos) está preparada pero **nunca corrió**: necesita unos 15 GB de RAM libre y nunca los tuvo.

**Ideas, ordenadas por ganancia y riesgo.**

1. Lanzar el juego con prioridad alta desde el lanzador (estimado: recupera parte de la pérdida cuando el PC está
   ocupado).
2. Recortar el trabajo del gancho en el hilo de presentación con los datos del medidor (estimado: +1 a +2 fps).
3. Una marioneta más ligera: sin las pisadas con trazado de rayos, y sin su propio grafo de animación mientras llegan
   poses (estimado: +2 a +5 fps, solo si las poses reales cuestan lo que cuestan las de los bots; riesgo: el sonido de
   las pisadas, y la vuelta a su animación propia).

**Las sondas.**

1. Un bot que repita una pose grabada de un juego real (con sus marcas reales), y después
   `tools/orm/harness/rendimiento.ps1`: dice si cinco marionetas cuestan 10 fps o casi nada.
2. La misma tanda con `LICHII_ORM_PERF=1`: la línea `coop: perf:` de cada variante dice adónde van los 3 fps del gancho.
3. `carga6.ps1` con el PC libre (sin compilaciones, nada más corriendo).
4. Un minuto de partida real con el uso de GPU registrado: si la GPU pasa del 90 %, el límite es la GPU, no esta lista.

---

## 8. Capa fina para Hookshot (01-platform)

**Qué es.** Una sola interfaz, `lichii/hook/platform.h`, para todo lo que nuestro código del gancho usa de la
plataforma: memoria del juego (lecturas, escrituras, un compare-exchange de 32 bits), llamadas al juego, el mando, el
registro y los interruptores. `platform_emulator.*` es el adaptador del emulador; `platform_env.h` es solo entorno y
registro. Regla para el código nuevo: nuestros archivos del gancho nunca incluyen cabeceras del emulador (`core/`,
`hid_core/`, `video_core/`, `common/`); lo que falte se añade a la capa y a su adaptador en el mismo commit.

**Lo que hizo upstream en test10.** Co-op en Hookshot y juego cruzado, y su `hookshot/compat/` hace que los mismos
archivos de `lichii/hook` compilen sin cambios en `hookshot_coop.dll`. Ampliamos su filtro de archivos para que
nuestros `orm_*` y `platform_*` entren también, y los botones del mando salen de su runtime. Upstream dice además que
las versiones de prueba después de test10 son solo para Hookshot.

**Lo que aún no funciona en Hookshot.**

1. **La puerta de caído.** En el emulador un pequeño parche de código frena el evento de game over mientras una palabra
   de la puerta está puesta. Hookshot no aplica parches de código. Idea: un gancho de entrada en la función que
   arranca el game over, en un archivo del parche de Hookshot, que devuelva 0 mientras la palabra está puesta; entonces
   el kit de Hookshot escribe `LICHII_ORM_HOOKSHOT_GATE=1` y caído vuelve a encenderse. Hasta entonces, caído se apaga
   solo en Hookshot con la línea `coop: check: downed fallback off on Hookshot ...` (game over normal).
2. **Nuestro HUD dentro de la imagen** es un parche Vulkan del emulador; en Hookshot se ve su propio HUD.
3. **El empujón del stick derecho del espectador** (para despertar la cámara tras una carga) necesita una exportación
   nueva del puente de Hookshot; hoy no hace nada y lo dice una vez.

**Sin hacer.** Compilar `hookshot_coop` con nuestros archivos, y cualquier partida con un juego en cada plataforma. Hay
una comparación lado a lado diseñada pero sin correr: `tools/orm/plataforma/comparar.py` juega la misma ruta de 10
minutos en los dos y decide con cinco reglas (estabilidad, fps, tirones, fallos visuales, crecimiento de memoria).

**La sonda que lo confirma.** Una pareja cruzada, un juego en Hookshot y otro en el emulador: los dos entran con
cifrado (`coop: traffic encrypted`) y se ven las marionetas. Después caído: el jugador de Hookshot muere y se queda
tumbado (sin game over), el otro mantiene A a su lado y lo levanta; los dos caídos: derrota de grupo y game over para
los dos.
