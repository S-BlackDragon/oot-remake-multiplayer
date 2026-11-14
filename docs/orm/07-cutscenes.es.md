[English](07-cutscenes.md) | **Español**

# 07-cutscenes: Escenas de historia para todos

Una escena de historia de la lista que el juego de un jugador arranca por sí solo (conocer al Gran Árbol Deku, Saria en el puente, Kaepora Gaebora...) se reproduce en todos los juegos que estén en la misma escena en el mismo momento, y un juego que ya la ha reproducido no la vuelve a reproducir. El estado final de la escena (las banderas que activa y los pasos de la historia que avanza) también se comparte, de modo que un jugador que estaba en otro sitio termina con el mismo mundo. Solo el dueño de la escena puede saltarla para todos. Esto se suma a la sincronización de salas de jefe de upstream, que conserva las introducciones y las muertes de los jefes.

## Qué cambia para los jugadores

- Entras en un disparador de historia y tus compañeros de la misma escena ven empezar la misma escena en sus pantallas en aproximadamente un segundo (cada uno con su propio Link). A un compañero que esté en otra escena, cargando, caído, o ya dentro de un evento propio no se le arrastra a ella; recibe el estado final como banderas.
- Un jugador que entra más tarde, o que vuelve a la escena, encuentra el mundo tal como lo dejó la escena (la boca del Árbol Deku abierta, el paso avanzado) y el disparador propio del juego no se activa otra vez.
- Después de un warp azul, la escena es solo para quien tomó el warp (`solo` en la lista, como en nuestro cooperativo anterior de N64); los demás reciben su resultado.
- Una escena de una sola sala de una escena (las pruebas del Castillo de Ganon) es solo para los jugadores que están cerca de donde empezó (`near=M`).
- Saltar: solo el dueño (el primer juego que la empezó) salta para todos. Cuando el dueño pulsa el botón de saltar (PLUS o MINUS, el aviso de saltar del remake y su confirmación) mientras su escena se reproduce, como máximo 5 s antes de que termine, su final sale como `skipped`, y cada juego que aún reproduce su copia fuerza el salto del resto (un salto forzado cada 0,5 s, durante un máximo de 8 s). Un compañero que salta su propia copia salta solo la suya. Un final normal nunca corta a nadie: una copia lenta se reproduce hasta su final.
- En el Templo del Tiempo, las reglas de [08-age](08-age.es.md) deciden quién toma una escena del templo.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_STORY_EVENTS` | `LICHII_ORM_STORY_EVENTS=1` | desactivado (`orm_story_events_game.cpp`: sin definir es desactivado) | 0/1/true/false/yes/no/on/off; un valor desconocido registra una línea y cuenta como sin definir | Escenas de historia para todos |
| `LICHII_COOP_STORY` (nombre antiguo) | no lo definen nuestras líneas | — | `1`/on activa nuestras escenas cuando `LICHII_ORM_STORY_EVENTS` no está definida | Los kits anteriores a test9 lo usaban. Nota: upstream lee el mismo nombre para compartir sus banderas de historia (activado salvo que sea 0), así que solo un valor de activación explícito activa las nuestras |
| `LICHII_ORM_STORY_LIST` (antes `LICHII_COOP_STORY_LIST`) | `# LICHII_ORM_STORY_LIST=shared_story.txt` (comentada) | la lista integrada | una ruta de archivo | La lista desde un archivo; un archivo que no se carga desactiva la función con una línea de error. El `shared_story.txt` del kit es la lista integrada (una prueba lo comprueba) |

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/story.hpp`, `coop/host/src/story.cpp` | El lado portable: la lista (`StoryList`) y la regla (`Story::tick`). Dueño: cuando el evento de nuestro juego empieza por sí solo y su primer nombre legible está en la lista, somos su dueño y enviamos `STORY_EVENT start`; al terminar, `end` (o `skipped`). Los demás: un inicio para nuestra escena, sin evento propio, sin estar caídos, sin estar cargando y sin haberlo reproducido ya aquí, hace que le pidamos a nuestro juego el mismo evento por nombre cada 0,5 s durante un máximo de 3 s. Un inicio para otra escena se ignora. Saltar: la ventana de pulsación, la decisión `end`/`skipped` y el salto forzado de una copia |
| `lichii/hook/orm_story_events.h`, `orm_story_events.cpp` | El lado de memoria: la petición por nombre (un malloc una vez, luego `main+0x4aa890(Link's event component, &box)`), y `Skipper`, el salto forzado del juego (`main+0x3b3af30` sobre el evento actual, la llamada que usan las salas de jefe de upstream) a través de la cueva svc |
| `lichii/hook/orm_story_events_game.cpp` | El pegamento: lee los interruptores y la lista, añade a las banderas compartidas las `HavePlayedEvent.<event>` y las banderas de final de cada evento de la lista, lee el evento en curso, los botones del mando (la pulsación de saltar), alimenta `Story::tick`, envía y recibe `STORY_EVENT` |
| `server/session.cpp`, `server/session.hpp` | El relé guarda el primer inicio de cada (escena, nombre) con su dueño, lo retransmite, acepta `end`/`skipped` solo del dueño y solo una vez, y envía a quien entra tarde cada (escena, nombre) que la sala ha iniciado como `played`, y después la última palabra del dueño. La carga de partida de un anfitrión las borra (las escenas de la partida se reproducen otra vez para todos) |
| `tools/release/kit/shared_story.txt` | La lista: un evento por línea con el nombre del juego, luego sus banderas de final: las `HavePlayedEvent.Dm_*` de las líneas de tiempo que reproduce, los Bools que activa, `step:Step_Name=V1,V2,...` para los pasos de la historia (banderas Enum, valores en el orden del juego; un paso solo avanza), `solo`, `near=M`. Se lee desde el romfs para todo el juego, niño y adulto |

No están en la lista a propósito: las introducciones y muertes de jefes (la sincronización de salas de jefe de upstream las reproduce), las escenas de mecanismos de mazmorra, los minijuegos, el caballo, los guardias gerudo que llevan a un Link a la celda, los cofres y los objetos obtenidos.

Puntos de enganche en archivos de upstream:

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_hook.cpp` | `OrmStoryEvents::AddShared` al inicio de la sesión; `OrmStoryEvents::OnEvent` en el hilo de red; `OrmStoryEvents::Frame` en cada fotograma; `OrmStoryEvents::HostReset` cuando el anfitrión carga una partida |
| `lichii/hook/coop_testinput.cpp` | Los comandos de prueba `event <name>` (nombres comprobados con `OrmStoryEvents::ValidName`) y `skip` (`OrmStoryEvents::NoteSkip("test skip")`, como lo haría una pulsación) |
| `server/session.cpp` | `on_story_event`, `send_story` (arriba) |
| `coop/protocol/include/coop/protocol/messages.hpp`, `coop/protocol/src/codec.cpp` | El cuerpo `StoryEvent` y su códec |

## Mensajes y componentes

`STORY_EVENT`, tipo `0xF003` (`orm_types.hpp`; era 63 en el protocolo 108 y 243 en el 109), canal 0. Cuerpo: número de jugador (0 desde un cliente), tipo, escena (1 a 64 caracteres), nombre del evento (1 a 127 de A-Z a-z 0-9 _), la posición de Link del dueño, hora del servidor en ms.

| Tipo | Valor | Significado |
|---|---|---|
| `start` | 1 | El juego del dueño la empezó |
| `end` | 2 | La copia del dueño terminó, sin saltarse |
| `played` | 3 | Del servidor solo a quien entra tarde: la sala la ha iniciado |
| `skipped` | 4 | Un final cuyo dueño la saltó |

El estado final viaja como banderas compartidas normales (`FLAG_SET` / instantánea), no en este mensaje.

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/host/story_test.cpp` (43 casos) | La lista integrada (todo el juego, con banderas; nunca se comparte una bandera por jugador; los warps azules son `solo`; cada hash de bandera y valor de paso es el del juego; el archivo del kit es la lista integrada; el análisis de `solo` y `near=M`; las barreras de las pruebas son `near`); dueño y seguidores; los que entran tarde; saltar: una pulsación justo antes del final envía `skipped`, un final natural o una pulsación antigua envía `end`, el comando `skip`, un `skipped` corta la copia seguida, un `end` no corta a nadie ni siquiera con un minuto de retraso, una petición pendiente se descarta, un rezagado con `skipped`, caído, el límite de 8 s, una carga de partida |
| `tests/orm/story_events_test.cpp` (7 casos) | La petición por nombre en memoria simulada, ninguna llamada sin Link o con un nombre incorrecto, el interruptor con el nombre antiguo, la llamada de salto forzado |
| `tests/lichii/story_test.cpp` | La prueba de historia de upstream, actualizada por los cambios del enganche |
| `tests/host/time_temple_test.cpp` | Un `skipped` del templo en la cadena de la espada se convierte en un final normal |
| `tests/server/session_test.cpp`, `tests/protocol/codec_test.cpp`, `tests/lichii/testinput_test.cpp` | Reglas del relé, ida y vuelta del códec de cada tipo, el comando de prueba `skip` |

Escenarios del arnés (están en `tools/orm/harness`): `historia` (A le pide a su juego por nombre el encuentro con el Árbol Deku; B, en la misma escena, debe iniciar el mismo evento en aproximadamente un segundo, con ambos Links en el lugar de la escena), muchos pares `historia-*`, uno por cada escena de la lista, la mayoría con una variante `-rezagado` para quien entra tarde, `historia-saltar` (A salta con el comando `skip`; B, detenido en un texto, debe saltar la suya) e `historia-largo` (un final natural no corta a nadie).

## Líneas de registro

Todas empiezan por `coop: story:`.

| Línea | Dice |
|---|---|
| `coop: story: on (LICHII_ORM_STORY_EVENTS): N shared story events (built in): ...` | Activada, y qué lista |
| `coop: story: LICHII_ORM_STORY_LIST <file>: <why>; story events are not shared` | El archivo no se cargó: desactivada |
| `owner: our game started '<event>' in <scene> ...` | Somos el dueño |
| `coop: story: STORY_EVENT start from P1 ('<event>' in <scene>, ...)` y `... : starting` | El inicio de un compañero, y se lo pedimos al nuestro |
| `'<event>' is solo: only we see it, its flags are shared` | Una línea `solo` |
| `'<event>' was played in this room already ...; not shared again` | Una repetición |
| `skip pressed (PLUS) during our '<event>': ...` | La pulsación del dueño |
| `'<event>' is over here, skipped by us (PLUS, 800 ms before its end); STORY_EVENT skipped to the others` / `is over here, not skipped (...)` | Qué final envió el dueño |
| `P1 skipped '<event>' in <scene>; ours is skipped too`, `force skip of our '<shot>' (...)`, `'<event>' skipped here after P1's: over after N force skips` | El salto de un seguidor |
| `P1's '<event>' in <scene> is over there (not skipped)` | Un final normal |
| `'<event>' not skipped: we are down` | No se salta estando caído |
| `", ignored: LICHII_ORM_STORY_EVENTS is off"` al final de una línea recibida | Desactivada aquí |

El relé registra `story: player N also started '<event>' ...` para un segundo que la inicia.

## Estado

- Inicio y final compartidos de la escena: **confirmado en partida en nuestra compilación basada en test9 (escenario `historia`)**: el encuentro con el Árbol Deku se reprodujo en ambos juegos, y terminó bien con uno y con dos juegos.
- Los demás pares `historia-*` y las variantes de quien entra tarde: no se cuentan aquí como confirmados.
- Salto del dueño: **todavía no confirmado en partida** (`historia-saltar`, `historia-largo` no ejecutados).

## Carencias conocidas

- Cuál es realmente el botón de saltar del remake: PLUS y MINUS son una suposición (`StorySettings::skip_buttons` es una sola constante).
- Si un salto forzado corta un solo plano o toda la línea de tiempo, si corta una copia detenida en un cuadro de texto, y que las banderas de final queden iguales en ambos juegos después de un salto.
- Texto dentro de una escena: cada jugador avanza el suyo, así que las copias se desfasan por segundos.
- El juego oculta las marionetas durante su propia escena de historia (su ajuste `TimelineEvent` pausa y oculta a todo actor que no participa); es una regla del juego, no la cambiamos aquí.
