[English](08-age.md) | **Español**

# 08-age: Cambio de edad y el Templo del Tiempo

En este juego la época forma parte de la partida guardada (`Sequence_IsChildhood`), y los mundos de niño y de adulto son escenas distintas (`KokiriForest_Child` / `KokiriForest_Adult`). Cuando un jugador desenvaina o devuelve la Espada Maestra, los demás se quedarían en la otra época sin nada que compartir. Esta función hace que el grupo siga el cambio de edad de un jugador mediante el cambio de época propio del juego, y reserva las escenas del Templo del Tiempo para los jugadores que están en el templo. Está **desactivada en el kit** hasta que una partida confirme que el juego acepta, lejos del pedestal, el cambio de época que se le pide.

## Qué cambia para los jugadores

Con `LICHII_ORM_AGE=1`:

- Cuando un compañero crece o vuelve a ser niño en el Templo del Tiempo, todos los demás juegos lo siguen con el cambio de época propio del juego (`Market_Child_Field_ChildToAdult` o `Market_Adult_Field_AdultToChild`): el juego cambia el equipo, fija la época y carga él mismo el Templo del Tiempo. Todos llegan al templo ya cambiados.
- A nadie se le mueve por sorpresa. Un aviso grande en el centro de la imagen (más grande que los avisos de los cofres; el más nuevo sustituye al anterior) le dice a cada jugador lo que ocurre. Los textos son las líneas en español del HUD:

| Quién | Aviso | También una línea de chat |
|---|---|---|
| Quien desenvainó o devolvió la espada | `Moviendo al grupo contigo...` | sí |
| Los demás, 2 s después de ver el cambio | `<Nombre> ha cambiado de época: moviendo al grupo al Templo del Tiempo...` | sí |
| Los demás, cuando ya es seguro moverlos | el mismo aviso con `3`, `2`, `1` debajo, uno por segundo; luego el cambio de época | no |
| Quien todavía no puede moverse | el mismo aviso con `Te moverás cuando termine la escena` / `cuando acabe el combate` / `cuando te reanimen` | la primera vez |
| Los que ya han cambiado mientras un compañero espera | `Esperando a <Nombre> para cambiar de época...` (y `Está caído` o `Está en una escena` cuando se sabe), otra vez cada 15 s | la primera vez |
| Quien llega al templo | `Grupo reunido en el Templo del Tiempo` cuando todos están allí; si no, `En el Templo del Tiempo` / `Esperando al resto del grupo...` | sí (reunido) |
| Cuando el cambio no se realiza dos veces seguidas | `No se pudo cambiar de época: inténtalo en el pedestal` | sí |

- El movimiento nunca se ejecuta en mitad de otra cosa: ni durante un evento o escena (una conversación cuenta), ni mientras una sala de jefe retiene al jugador (introducción, combate o salida), ni mientras el jugador está caído (tampoco tumbado: el movimiento lo alejaría de quien puede reanimarlo). Cuando el motivo termina, la cuenta 3, 2, 1 vuelve a empezar.
- El Templo del Tiempo (con las escenas de historia activadas, [07-cutscenes](07-cutscenes.es.md)): el templo forma parte de la escena del mercado (`Market_Child` / `Market_Adult`), así que sus escenas (la Puerta del Tiempo, el pedestal, Sheik en el templo) se reproducen solo para los jugadores que están dentro del templo, no para uno que compra en la plaza. Quien está dentro cuando se desenvaina la espada viaja con esa escena; los demás siguen con el cambio de época de arriba cuando nada se ejecute en su juego.

Con `LICHII_ORM_AGE=0` (la línea del kit) cada juego conserva su propia época, como en upstream.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_AGE` (el nombre antiguo `LICHII_COOP_AGE` todavía se lee) | `LICHII_ORM_AGE=0` | desactivado (`coop_age_game.cpp`, `CoopSwitch::On`) | 0/1/true/false/yes/no/on/off | Seguir el cambio de edad de un compañero |
| `LICHII_ORM_AGE_EVENT` (antes `LICHII_COOP_AGE_EVENT`) | `# LICHII_ORM_AGE_EVENT=` (comentada) | sin definir: el cambio de época propio del juego | un nombre de evento (A-Z, a-z, 0-9, _) | Solo para pruebas: recarga nuestra propia escena en el sitio mientras ese evento se ejecuta, en lugar del cambio de época; sin avisos, sin cuenta atrás. `ReplaceEquipmentForEraChange` se ignora (el juego nunca lo inicia por nombre, véase abajo) |
| `LICHII_ORM_TIME` | sin definir (solo un comentario que muestra `LICHII_ORM_TIME=0`) | activado (`coop_time_temple_game.cpp`) | 0/1/... | Las reglas del Templo del Tiempo; solo funcionan con `LICHII_ORM_STORY_EVENTS` activado |
| `LICHII_ORM_TIME_LIST` | sin definir (el comentario muestra `shared_time_temple.txt`) | la lista integrada | una ruta de archivo (relativa a la carpeta del kit) | Las reglas del templo desde un archivo; el `shared_time_temple.txt` del kit es el mismo texto que la lista integrada (una prueba lo comprueba) |

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/age.hpp`, `coop/host/src/age.cpp` | `AgeFollow`, la regla portable (sin memoria, sin sockets). La edad de un compañero cuenta una vez que ha sido la misma durante 2 s, no está cargando y coincide con el sufijo de su escena (la primera lectura de un compañero nunca es un cambio). Un cambio a la otra edad deja el nuestro pendiente; con calma pide el cambio de época del juego en la dirección correcta, no escribe nada antes de que ese evento se ejecute, y da un límite de 120 s (escena, carga y llegada; un hueco de hasta 1 s sin evento es el traspaso de la escena, no el final). También genera los avisos (`AgeNotice`: `moving_you`, `announce`, `countdown`, `waiting_mate`, `gathered`, `arrived`, `failed`), las esperas con su motivo (`AgeInput::boss_fight`, downed, event) y el abandono tras `era_fail_limit` (2) fallos. `AgeInput::wait` (el Templo del Tiempo) conserva un cambio visto para más tarde; `AgeInput::held` (el final del juego) lo descarta |
| `lichii/hook/coop_age_game.cpp`, `.h` | El pegamento: lee el interruptor, las edades de los compañeros desde la presencia, nuestra época, si una sala de jefe nos retiene (`CoopBoss`), si estamos caídos (`CoopDowned`), y lleva a cabo lo que pide `AgeFollow`: el evento por nombre a través de la petición de la capa de historia (`OrmStoryEvents::Ask`), los avisos (`coop_notices.h` `Banner`, dibujado por `orm_hud` `Model::banner`), las líneas de chat (`TakeLines`) y nuestro bit de estado `age_waits` |
| `coop/host/include/coop/host/time_temple.hpp`, `coop/host/src/time_temple.cpp` | La lista del templo (reglas `temple`, `travel`, `chain`, `sage`, `scene`; `area`, `noage`, `flag`), `check_time_list` (la lista debe coincidir con la lista de historia y los objetos compartidos) y `TimeTemple`: `filter` (el inicio de una escena del templo solo se acepta en el templo; una no aceptada cuenta como reproducida) y `tick` (el motivo por el que la edad de la sala espera mientras la historia de la espada se ejecuta aquí) |
| `lichii/hook/coop_time_temple.h`, `coop_time_temple_game.cpp` | Pegamento: lee el interruptor y la lista, añade las banderas del templo a las banderas compartidas, y le dice a `CoopAge` que espere (`HoldsAge`) mientras nuestro juego ejecuta la historia de la espada |
| `tools/release/kit/shared_time_temple.txt` | Las reglas del templo como archivo del kit |

La caja del templo se leyó del romfs (`LocationArea` "TimeTemple" de ambas escenas del mercado, con un margen): x 58..160, y 95..125, z -56..-4. Contiene el pedestal, el altar y la entrada, y deja fuera la plaza.

Por qué el cambio de época del juego: el primer diseño le pedía al juego `ReplaceEquipmentForEraChange` por nombre y recargaba la escena en el sitio. El juego lo rechazaba siempre: es un subflujo de los flujos del pedestal sin fila en `RSDB/EventSetting`, y el juego solo inicia por nombre un evento que tenga fila. Los flujos propios del pedestal sí la tienen, así que ahora el cambio de época se pide con esos nombres.

Nunca dejar la época cambiada: no se escribe nada antes de que se ejecute uno de nuestros eventos, y si la recarga no se realiza después de que se ejecutó, `Sequence_IsChildhood` se vuelve a poner a la edad de nuestro Link.

Puntos de enganche en archivos de upstream:

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_hook.cpp` | `CoopAge::Frame` en cada fotograma después del fotograma de la capa de historia, y sus líneas de chat (`CoopAge::TakeLines`) al chat |
| `lichii/hook/coop_testinput.cpp` | El comando de prueba `age adult|child` (este juego sigue como si un compañero hubiera cambiado) |
| `coop/host/include/coop/host/sampler.hpp`, `session.hpp` (y `sampler.cpp`) | `set_age_waits`: nuestro `PLAYER_STATUS` lleva el bit 6 mientras nuestro movimiento espera |

## Mensajes y componentes

| Nombre | Número | Uso |
|---|---|---|
| `PLAYER_STATUS` bit `age_waits` (`orm_status::age_waits`) | bit 6 del byte de banderas (`orm_types.hpp` `kStatusAgeWaits`) | Nuestro movimiento al Templo del Tiempo espera; los demás muestran "Esperando a..." |
| `PLAYER_SAMPLE` `age` (upstream) | campo de upstream | La edad de cada compañero, como antes |

Ningún tipo de mensaje nuevo. Un servidor que no conoce el bit 6 lo retransmite intacto.

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/host/age_test.cpp` (26 casos) | Nombres de escena por edad; el cambio de un compañero seguido; ningún cambio pedido para un compañero que pasa a nuestra edad o vuelve; reintentos y el reintento de 10 s; el evento que termina sin carga; un compañero que se va y vuelve visto de nuevo; la retención del final del juego; la espera del Templo del Tiempo; lugares de inicio del Templo del Espíritu; nuestra propia lectura mientras carga; tumbado y la recarga; el cambio de época del juego por defecto sin que nosotros escribamos nada; el camino de vuelta a niño; un cambio rechazado que nunca toca la época; época repuesta tras un cambio sin carga; los casos de grupo: orden de avisos y 3, 2, 1, las esperas (escena, sala de jefe, caído) con su motivo, "Esperando a..." cada 15 s, "Moviendo al grupo contigo...", abandono tras dos fallos, la recarga en el sitio que no dice nada |
| `tests/host/time_temple_test.cpp` (9 casos) | La lista integrada es igual al archivo del kit; sus reglas, áreas y banderas; coincidencia con la lista de historia y los objetos compartidos; líneas incorrectas rechazadas con su número; el filtro (los inicios del templo solo en el templo, la cadena de la espada, Sheik en otros sitios, los sabios); la retención de edad |
| `tests/orm/orm_hud_test.cpp` | El aviso grande centrado (`Banner`) |
| `tests/orm/host_sampler_test.cpp` | El bit `age_waits` |

Escenarios del arnés (están en `tools/orm/harness`): `edad` (la recarga en el sitio más antigua, con el comando de prueba `age`), `edad-grupo-cerca` (B en la plaza del mercado) y `edad-grupo-lejos` (B en el Bosque Kokiri): A desenvaina la espada, B recibe el aviso, cuenta y llega adulto al templo, ambos ven "Grupo reunido". `tiempo-espada` y `tiempo-medallon` cubren las reglas del templo.

## Líneas de registro

Todas empiezan por `coop: age:` o `coop: temple:`.

| Línea | Dice |
|---|---|
| `coop: age: on (LICHII_ORM_AGE): a mate's age change is followed by the game's own era change ('Market_Child_Field_ChildToAdult' / 'Market_Adult_Field_AdultToChild': ...)` | Activada, camino por defecto |
| `coop: age: on (LICHII_ORM_AGE): a mate's age change is followed (the reload asks the event '...' and reloads our scene in place; ...)` | Activada, camino de prueba (`LICHII_ORM_AGE_EVENT`) |
| `we changed age ourselves (child -> adult): the group follows us to the Temple of Time ...` | Desenvainamos la espada nosotros |
| `told: '<Nombre> ha cambiado de época: ...'`, `countdown 3 before the move to the Temple of Time` (2, 1) | El anuncio y la cuenta atrás |
| `coop: age: notice <kind>: <text> / <second line>` | Cada aviso mostrado |
| `coop: age: our PLAYER_STATUS carries age_waits (bit 6)` / `drops age_waits` | Nuestro movimiento espera / continúa |
| `every mate is our age: the group is gathered (told 'Grupo reunido en el Templo del Tiempo')` | Reunidos |
| `the reload as adult did not take: ...; tried again in 10 s` / `...; 2 in a row: given up, no move (the era stays our Link's; ...)` | Un fallo, el abandono |
| `Sequence_IsChildhood put back to child (our Link's age): the era stays his` | La época restaurada tras un fallo |
| `reloads wait: the Temple of Time: our game runs '...' (travel)`, `reloads may begin again` | La retención del templo |
| `coop: temple: on (...): N Temple of Time events, N temple areas, N scenes with no age ...` / `coop: temple: off (LICHII_ORM_TIME=0)` | Reglas del templo activadas o desactivadas |
| `coop: temple: list: agrees with the story list and the shared items` | La comprobación de la lista |
| `coop: temple: the room's age waits: ...` / `coop: temple: the room's age is followed again` | La retención del templo, desde el lado del templo |

## Estado

- Cambio de edad: **todavía no confirmado en partida.** La recarga en el sitio más antigua se ejecutó en el escenario `edad` en nuestra compilación basada en test9, pero el juego rechazó su evento (`ReplaceEquipmentForEraChange`) siempre, que es la razón por la que el kit incluye `LICHII_ORM_AGE=0` y por la que el código ahora pide el cambio de época propio del juego. Ese camino, los avisos y el movimiento del grupo solo tienen pruebas unitarias (`edad-grupo-cerca` y `edad-grupo-lejos` están escritos, no ejecutados).
- Reglas del Templo del Tiempo: **todavía no confirmadas en partida** (`tiempo-espada`, `tiempo-medallon` no ejecutados).

## Carencias conocidas

- Una ejecución en partida tiene que mostrar que el juego acepta `Market_Child_Field_ChildToAdult` pedido lejos del templo (la línea `the game took '...'`), que su escena se ve bien allí, que B llega adulto a `Market_Adult` con la Espada Maestra y el escudo de su edad, y el camino de vuelta a niño.
- Si el aviso grande se lee bien y no tapa nada importante.
- La Puerta del Tiempo vista desde la plaza: su aspecto lo decide su propio grafo leyendo `Market_Child_TimeTempleDoor_Opened`, no la presencia de una bandera en la escena; no se ha visto si una bandera escrita desde fuera la muestra abierta sin recargar.
- En Hookshot (el siguiente objetivo de upstream) nada de esto se ha compilado ni ejecutado.
