[English](03-fixes.md) | **Español**

# 03-fixes: Correcciones de errores, incluidas las de rutas de código del upstream

Correcciones que hicimos al construir sobre test10 y que tocan código o pruebas que ya estaban en el árbol, o con las que se toparía cualquiera que compile el árbol con las opciones del emulador. Cada fila señala el archivo cambiado y la prueba que lo cubre. Las correcciones dentro de nuestras propias funciones nuevas están en los archivos de cada función.

## Qué cambia para los jugadores

- **Un invitado que pulsó Continuar antes que el anfitrión ya no recibe el estado de una partida nueva.** Antes: cuando la partida del anfitrión seguía en la pantalla de título, su tabla de banderas era la de un guardado nuevo (3 corazones, magia 0, sin bendición de hada); el servidor se queda con la primera tabla que recibe, y a un invitado que ya había cargado su guardado se le devolvía todo a esa tabla. Después el papel de anfitrión podía pasar a ese invitado y el estado roto se convertía en el del grupo (y podía guardarse). Ahora un anfitrión sin guardado cargado retiene su tabla hasta que se lee su Link, y la primera carga de un invitado que aún no tiene valor de grupo conserva su propio guardado.
- Kit: un color que ya está en `coop.ini` se conserva, y una lista separada por comas de archivos del kit en `coop-options.ini` (como `LICHII_COOP_DUNGEON_LISTS`) se convierte en rutas completas igual que un solo archivo.
- El resto son correcciones de compilación y de pruebas, sin cambio visible.

## Interruptores

Ninguno. Estas correcciones están siempre activas.

## Cómo funciona

### Correcciones en rutas de código del upstream

| Qué estaba mal | Dónde | Qué cambiamos | Prueba |
|---|---|---|---|
| Un anfitrión en la pantalla de título enviaba la tabla de banderas de un guardado nuevo; un invitado que cargó primero la recibía encima de su guardado | `coop/host/src/flag_watcher.cpp`, `flag_watcher.hpp`; `coop/host/src/session.cpp`; `lichii/hook/coop_hook.cpp` (avisa al vigilante de que se leyó un Link desde el último título) | La tabla del anfitrión se retiene hasta que su guardado está cargado, y entonces se envía. La primera carga de un invitado desde la pantalla de título sin valor de grupo todavía conserva los valores de su guardado; con valores de grupo vuelve a ellos como antes | `tests/host/flag_watcher_test.cpp`: "on the title screen the table waits until the save is in", "the title screen with only one flag back is no save load", "flags back early in the title...", "a guest's first save load with no group value yet keeps its save", "a guest's reload from a save goes back to what its game had" |
| Las matrices del mundo del esqueleto se leían como 3x4 (0x30 de separación). En el juego las 309 matrices de Link son 4x4 (0x40 de separación, la posición en los floats 12 a 14): con 0x30 solo el hueso 0 era correcto, así que la búsqueda del hueso más cercano siempre decía hueso 0 | `lichii/hook/coop_carry.cpp`, `coop_carry.h`; `lichii/hook/coop_objects.cpp` (el censo de huesos) | La disposición se deduce del hueso 0 (4x4 por filas o por columnas, a partir del propio giro del actor); la lectura antigua 3x4 queda como alternativa; el censo y la comprobación del hueso de carga nombran la disposición | `tests/lichii/carry_test.cpp` ("the skeleton's layout is told from bone 0: 4x4 by rows or columns, or the old 3x4"); simulaciones en `tests/lichii/skeleton_fakes.hpp` |
| Un recuento de objetos sumado podía bajar de 0 | `coop/host/src/game_items.cpp` | La suma se limita a 0 (y, con nuestras reglas de objetos, una ganancia se detiene en el tope de la cartera: 06-world) | `tests/orm/wallet_test.cpp`, `tests/host/game_items_test.cpp` |
| La macro de la palabra clave `slots` de Qt se comía `ObjectComponent::slots` y `ObjectSample::slots` (test10) en cualquier archivo Qt que incluya el protocolo | `coop/protocol/include/coop/protocol/messages.hpp` | `#pragma push_macro("slots")`, `#undef slots` alrededor de la cabecera | compilaciones de `coop_window.cpp` y de la aplicación |
| Variables locales que ocultaban otros nombres (MSVC C4456/C4459 son errores en la compilación del emulador) | `lichii/hook/coop_puppet.cpp` (`buf` interno de `Director::Status` → `frame_text`; la `t` de la ranura ausente → `absent`), `coop_spawn.cpp` (`age` → `anim_age`), `coop_world.cpp` (`kParts` → `kSwitchParts`), `coop_testinput.cpp` (`number` → `is_number`) | Renombradas, sin cambio de comportamiento | la compilación del emulador (`cl /Zs` de cada archivo del hook con sus opciones) |
| El color por defecto del kit (anfitrión azul, invitados rojo) pisaba un color que el lanzador había escrito en `coop.ini` | `tools/release/kit/scripts/play.ps1` | Un color que ya está en `coop.ini` se conserva y se exporta como `LICHII_COOP_COLOR` cuando `coop-options.ini` no trae ninguno | ninguna (el análisis del color que usa está en `tools/release/tests/launch_test.ps1`) |
| Solo un único archivo del kit en `coop-options.ini` se convertía en ruta completa; una lista separada por comas no | `tools/release/kit/scripts/play.ps1`, `kit-paths.ps1` | `Resolve-KitOptionValue` resuelve cada entrada de una lista separada por comas | `tools/release/tests/launch_test.ps1` |
| Las pruebas de red nuevas del upstream escuchaban en todas las interfaces, lo que provoca un aviso del cortafuegos de Windows para `coop_tests.exe` | `tests/server/relay_test.cpp`, `nat_loopback_test.cpp`, `loopback_test.cpp`; `tests/net/small_path_test.cpp`, `encrypt_loopback_test.cpp`; `tests/host/session_test.cpp` | Todos los servidores de prueba se enlazan a 127.0.0.1 | ellas mismas |
| Los archivos de prueba que imprimen resultados `std::string_view` con doctest no incluían `<ostream>` | `tests/lichii/game_build_test.cpp` y otros | `#include <ostream>` | ellas mismas |
| `lichii/README.md` nombraba nuestro parche del HUD como el `0006` del upstream | `lichii/README.md` | Filas para nuestros parches 0100, 0101, 0102, `video.cmake` y `app/` | ninguna |

### Correcciones de compilación y de pruebas en nuestro propio código (se dejan aquí porque dan problemas en una compilación limpia)

| Qué estaba mal | Dónde | Qué cambiamos |
|---|---|---|
| Nombres de prueba con `;` (ctest los parte o los rechaza al configurar, así que los casos nunca se ejecutaban) | muchos archivos bajo `tests/` (pvp, carry, resume, songs, clock, smoothing, chest, door, hazard, endgame, link watch, pruebas visuales) | `;` eliminado de todos los nombres de prueba |
| `orm::` ambiguo en una prueba del servidor una vez que existió `coop::protocol::orm` (la tabla de números) | `tests/server/orm_interest_test.cpp` | Nombra `coop::server::orm` completo |
| El hook llamaba a `on()` en dos guardas que no lo tenían (`ctest` no compila el hook, solo lo vio la compilación del emulador) | `coop/host/prizes.hpp`, `beans.hpp` | Se añadió `on()`, como lo tiene `TradeGuard` |
| `coop_window.cpp` no encontraba `coop/overlay` una vez que el mapa de compañeros lo incluyó | `lichii/hook/qt.cmake` | El objetivo Qt enlaza `coop_overlay` (y `coop_host` para las sugerencias de comandos) |
| Dos `connection_of`, un `//x` que se leía como comando, variables locales que ocultaban globales | `server/orm_session.cpp`, `session.hpp`, `coop/net/src/client.cpp`, `lichii/hook/orm_commands_game.cpp`, `coop/host/src/orm_commands.cpp` | Se conserva una sola función auxiliar; renombrados |
| Un fotograma cuya escena no se leyó contaba como cambio de escena: se perdían el ancla del caído y el suelo firme (un cuerpo en lava o en el vacío volvía a quedar fuera de alcance) | `coop/host/src/hazards.cpp` | Una lectura así se queda en la última escena leída (prueba: `tests/host/hazards_test.cpp`, "a frame whose scene was not read is not a scene change") |

### Puntos de enganche en archivos del upstream

Todos están en la primera tabla: `flag_watcher.*`, `session.cpp` (anfitrión), `coop_hook.cpp`, `coop_objects.cpp`, `game_items.cpp`, `messages.hpp`, `coop_puppet.cpp`, `coop_spawn.cpp`, `coop_world.cpp`, `coop_testinput.cpp`, `play.ps1`, `kit-paths.ps1`, las pruebas de red del upstream, `lichii/README.md`.

## Mensajes y componentes

Ninguno.

## Pruebas

Véase la columna "Prueba" de arriba. Las correcciones en pruebas del upstream (127.0.0.1, `<ostream>`) se comprueban porque la suite se ejecuta sin aviso del cortafuegos.

## Líneas de registro

| Línea | Cuándo |
|---|---|
| `coop: flags: we host with no save in (the title screen): our table is held until our save is in, so no ...` | Un anfitrión en la pantalla de título |
| `coop: flags: our table sent: our save is in now (held while our game had none)` | Su guardado ya está cargado |
| `coop: flags: our save load from the title screen: N values of our save kept (no group value had come: the host has no save in yet)` | Primera carga de un invitado antes que el anfitrión |
| `coop: flags: our save load: N values back to the group's (...)` | Carga de un invitado cuando el grupo ya es conocido (como antes) |

## Estado

- Banderas de la pantalla de título: confirmado en partida en nuestra compilación basada en test9: el camino del anfitrión en la pantalla de título (tabla del anfitrión retenida y luego enviada al cargar su guardado; ambas partidas con los corazones y la magia de su guardado) en las tandas de sondas `sondas17r6` y `sondas18r6` tras la corrección (entre ellas las parejas `ocarina-mano` y `charla-mido`), y en 50 parejas con el segundo jugador entrando primero, la primera vida de cada partida fue la de su propio guardado, sin discrepancia de guardado. El camino en que el invitado carga primero ("values of our save kept") solo se ha visto en pruebas unitarias: en las ejecuciones el anfitrión siempre cargó primero.
- Matrices de esqueleto 4x4: confirmado en partida en nuestra compilación basada en test9 (la vasija cargada y la Ocarina en la mano de la marioneta usan el hueso correcto: escenarios `levantar-vasija`, `ocarina-mano`).
- Las demás filas son correcciones de compilación y de pruebas: comprobadas por la compilación y la suite, nada que ver en partida.

## Carencias conocidas

- El camino de la corrección de la pantalla de título en que el invitado carga primero todavía no se ha visto en una partida de dos jugadores.
