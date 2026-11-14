[English](14-app.md) | **Español**

# 14-app: La pantalla "Cooperativo" y su protocolo con el ayudante

Una pantalla de Qt dentro de la propia ventana del emulador que sustituye a la lista de juegos: muestra si todo lo necesario para jugar está en su sitio, prepara el paquete de marionetas y los shaders, pide una sala, un nombre y una contraseña, e inicia el juego en el mismo proceso con el modo cooperativo activado. La pantalla no hace ningún trabajo por sí misma: habla con un programa **ayudante** independiente por stdin/stdout, un objeto JSON por línea. Cualquier programa que hable este protocolo puede ser el ayudante. Un segundo canal, dentro del proceso, une el gancho del juego con la pantalla (comandos de chat que necesitan al ayudante, y los interruptores de PvP y "fijar aliados" de la sala).

## Qué cambia para los jugadores

- Con un archivo `orm-app.ini` junto al exe del emulador, el emulador se abre en la pantalla "Cooperativo" en lugar de la lista de juegos. Sin ese archivo no cambia nada (primero la lista de juegos, cooperativo iniciado de inmediato, como antes).
- Filas de estado con ✓, ✗ o … (todavía no se sabe): claves, firmware, juego, paquete de marionetas, shaders. Los archivos que se sueltan sobre la pantalla se entregan al ayudante, que los coloca donde corresponden. Unos botones abren la carpeta de cada fila.
- "Preparar" construye el paquete de marionetas y después prepara los shaders, con una barra de progreso y una línea que dice qué se está haciendo. Se inicia solo cuando ya están las claves, el firmware y el juego y faltan el paquete o los shaders.
- Sala, nombre (obligatorio) y una contraseña opcional, y después "Jugar". Cambiar la sala o el nombre una vez iniciado el cooperativo pide reiniciar.
- "Quiero ser el anfitrión" (I want to host), cuando el ayudante informa de un deseo de ser anfitrión.
- Interruptores "PvP" y "Fijar aliados" (lock on mates) mientras hay conexión, que reflejan los interruptores de la sala.
- Un bloque "Versión y actualizaciones" con botones de actualización y notas de la versión, y un aviso no modal cuando el ayudante informa de una versión más nueva.
- Cuando el juego se detiene, la pantalla vuelve.
- Dentro del juego, `/registros`, `/reintentar` y `/nombre` llegan al ayudante a través de la pantalla.

## Interruptores

| Variable | Valor por defecto en el kit | Valor por defecto en el código | Valores | Qué hace |
|---|---|---|---|---|
| `orm-app.ini` junto al exe (un archivo, no una variable) | no está en el kit | ausente: pantalla desactivada | presente / ausente | Activa la pantalla (`OrmApp::Enabled`) |
| `[app] helper=` en `orm-app.ini` | | `../orm-launcher.exe` (relativo a la carpeta del exe) | una ruta, relativa o absoluta | El programa ayudante |
| `[app] helper_args=` en `orm-app.ini` | | ninguno | argumentos adicionales, separados como en una línea de comandos | Se añaden después de `--service` |
| `ORM_RESTART_WAIT_PID` | no está en el kit | | un id de proceso | Lo pone la pantalla para el programa de reinicio cuando reinicia la aplicación (un programa de reinicio antiguo lo ignora) |

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `lichii/app/orm_app.h`, `.cpp` | Puntos de entrada a los que llama la ventana principal: `Enabled()`, `Attach(...)` (pone la pantalla en la fila central en lugar de la lista de juegos; se aparta cuando aparece la ventana de renderizado o la pantalla de carga), `OnGameStopped()`. Lee `orm-app.ini` y construye la línea de comandos del ayudante |
| `lichii/app/orm_helper_client.h`, `.cpp` | `HelperClient`: el cliente de líneas JSON. Solo Qt Core, sin moc. Inicia el ayudante sin ventana (`CREATE_NO_WINDOW`), o funciona en memoria para las pruebas. Asigna ids, guarda una función de respuesta por petición, aplica tiempos de espera, une las líneas partidas y descarta las demasiado largas. `StatusPoller`: un `status` cada vez, cada 2 s, solo cuando la pantalla lo permite |
| `lichii/app/orm_app_model.h`, `.cpp` | Lectores tolerantes de las respuestas del ayudante (`ParseStatus`, `ParsePlay`, `ParseUpdate`, `ParseImport`, textos de progreso) y los textos en español. Un campo ausente deja su valor por defecto, de modo que pantalla y ayudante pueden distanciarse un poco |
| `lichii/app/orm_app_window.h`, `.cpp` | Los widgets y los flujos de la pantalla (inicio, estado, preparar, jugar, importar, actualizaciones, reinicio, interruptores) |
| `lichii/app/app.cmake` | Añade los archivos al objetivo del frontend de Qt; estampa el `git describe` del árbol cooperativo en las líneas de versión. Lo incluye `lichii/hook/qt.cmake`, así que el CMake propio de Fritchii no se toca |
| `lichii/app/tests.cmake`, `tests/orm_app_tests.cpp` | Pruebas unitarias (se compilan cuando se encuentra Qt 6). El exe de pruebas hace también de ayudante falso con `--fake-helper` |
| `lichii/hook/orm_app_channel.h`, `.cpp` | El canal dentro del proceso juego ↔ aplicación: unas pocas funciones de respuesta y una cola tras un solo mutex. Gancho → aplicación: `upload_logs`, `retry_downloads`, `save_name`; aplicación → gancho: respuestas para el chat y los interruptores (`SetSwitch`, mediante una función que registra el gancho); ambos: los interruptores de la sala con un contador de versión |
| `lichii/patches/0102-orm-app.patch` | Un archivo de Fritchii original, `src/yuzu/main_window.cpp`: `CoopWindow::Attach` solo sin `orm-app.ini`; `OrmApp::Attach` después de que exista la pantalla de carga (arrancando mediante el propio `BootGame` de la ventana); `OrmApp::OnGameStopped()` en `OnEmulationStopped` |

### Puntos de enganche en archivos originales

| Archivo original | Qué añadimos |
|---|---|
| Fritchii `src/yuzu/main_window.cpp` (parche 0102) | Las tres llamadas de arriba |
| `lichii/hook/qt.cmake` | `include(.../lichii/app/app.cmake)` |
| `lichii/hook/core.cmake` | `orm_app_channel.cpp` |
| `lichii/hook/orm_commands_game.cpp` (nuestro) | `/registros`, `/reintentar`, `/nombre` consultan a la aplicación; registra la función de los interruptores y publica los interruptores en cada fotograma |

## El protocolo del ayudante

### Inicio y cierre

- La pantalla inicia `<helper> --service [helper_args...]` sin ventana, con su carpeta de trabajo sin cambiar. stdout lleva las respuestas y los eventos; cada línea de stderr va al registro del emulador como `ayudante: <line>`.
- Un objeto JSON por línea, UTF-8, LF o CRLF. Una línea de más de 4 MiB sin salto de línea se descarta.
- Al cerrar, la pantalla envía `quit`, espera hasta 3 s a que el ayudante termine y después lo mata. Si hay una actualización esperando a aplicarse, `update_apply_on_restart` va antes de `quit`.
- Si el ayudante termina, todas las peticiones pendientes fallan con un error en español (`el ayudante se cerró ...`).

### Formas

| Tipo | Forma |
|---|---|
| Petición | `{"id":N,"cmd":"<name>", ...fields}`; `id` cuenta hacia arriba desde 1 |
| Respuesta | `{"id":N,"ok":true\|false,"error":"<text>", ...fields}` |
| Evento | `{"event":"<name>", ...fields}`, sin `id` |

Reglas que debe cumplir un ayudante:

- **Responder a las peticiones en el orden en que llegaron.** La pantalla cuenta con ello (una consulta nunca se acumula detrás de una tarea larga; `update_apply_on_restart` antes de `quit`).
- Cada petición recibe exactamente una respuesta con su `id`. Una respuesta con un `id` desconocido se anota en el registro y se ignora; una línea que no es ni respuesta ni evento se anota en el registro y se ignora.
- `error` se muestra al jugador tal cual: escríbelo en el idioma del jugador.
- Un campo que la pantalla no conoce se ignora; un campo que espera y no recibe conserva su valor por defecto.
- Un ayudante que no implementa un comando responde `{"ok":false,"error":"..."}`.

### Tiempos de espera (por parte de la pantalla)

| Petición | Tiempo de espera |
|---|---|
| `status` | 20 s |
| `game_ended`, `room`, `shortcut` | 30 s |
| `update_check`, `import_finish` | 60 s |
| `play` | 180 s (puede que antes descargue la partida guardada de la sala) |
| `build_puppet_pack`, `prepare_shaders`, `import`, `update_download`, `update_apply_on_restart`, `update_rollback`, `upload_logs`, `retry_downloads` | ninguno (tareas largas que informan con eventos `progress`) |

Una petición que agota su tiempo falla con `el ayudante no respondió a tiempo (<cmd>)`; su respuesta tardía se ignora.

### Peticiones

| `cmd` | Campos enviados | Campos de la respuesta que se leen | Cuándo |
|---|---|---|---|
| `status` | ninguno | véase "Respuesta de `status`" más abajo | Al inicio, tras cada tarea, cada 2 s mientras está inactiva |
| `room` | `room`, `name`, `password` (puede estar vacío), `host` (`me` o `auto`, solo cuando el interruptor de anfitrión se muestra y está activado); o solo `name` (desde `/nombre`) | `ok`, `error` | "Jugar", `/nombre` |
| `play` | ninguno | `game` (o `path`; si no, `-g <file>` o el último no-opción de `args`), `args` (lista), `env` (objeto: variable → valor), `fullscreen` (o `-f` en `args`) | Después de `room` |
| `game_ended` | ninguno | `ok`, `error` | El juego se detuvo |
| `build_puppet_pack` | ninguno | `ok`, `error` | "Preparar", primer paso |
| `prepare_shaders` | ninguno | `ok`, `error` | "Preparar", después del paquete |
| `import` | `paths` (lista de rutas locales soltadas sobre la pantalla) | `import_id` (cualquier valor JSON, devuelto tal cual), `imported` (lista), `skipped` (lista), `ask` (una pregunta de sí/no; vacío: ninguna), `summary` (una línea) | Se soltaron archivos |
| `import_finish` | `import_id`, `delete` (booleano: la respuesta del jugador a `ask`) | `summary` | Solo cuando `ask` no estaba vacío |
| `shortcut` | `action`: `create` o `remove` | `ok`, `error` | El botón de acceso directo (se muestra cuando `status` informa de `shortcut`) |
| `upload_logs` | ninguno | `ok`, `error` | `/registros` en el juego |
| `retry_downloads` | ninguno | `ok`, `error` | `/reintentar` en el juego, o el botón de reintentar |
| `update_check` | ninguno | `available`, `version` (o `channel_version`), `notes` (texto o lista de líneas), `ready` (o `update_ready`) | Al inicio y con el botón |
| `update_download` | ninguno | `ok`, `error` | Hay una versión más nueva disponible y sin descargar |
| `update_apply_on_restart` | ninguno | `ok`, `error` | "Actualizar ahora", "Reiniciar", o al cerrar con una actualización descargada. Tras `ok`, los archivos deben estar en su sitio: la pantalla reinicia la aplicación |
| `update_rollback` | ninguno | `ok`, `error` | "Volver a la versión anterior", tras una confirmación; después un reinicio |
| `quit` | ninguno | (cualquiera) | Al cerrar |

### Respuesta de `status`

Todos los campos son opcionales.

| Campo | Tipo | Significado |
|---|---|---|
| `keys`, `firmware`, `game`, `puppet_pack`, `shaders`, `shortcut` | booleano, una cadena con una ruta (no vacía = está), o `{"ok":bool, "detail"\|"text"\|"path"\|"version"\|"error":"...", "folder":"..."}` | Una fila cada uno. `folder` es lo que abre el botón de la fila |
| `paths` | `{"game","keys","firmware"}` (o, en el nivel superior, `game_path`, `keys_path`, `firmware_path`) | Se muestra cuando una fila no tiene detalle |
| `shaders_version` | texto | Se muestra como detalle de la fila de shaders cuando esta no tiene ninguno |
| `version` (o `app_version`) | texto | La aplicación instalada |
| `channel_version` (o `latest_version`, o `channel.version`) | texto | La más nueva que conoce el ayudante |
| `kit_version`, `protocol` | texto | Líneas de versión |
| `room` | texto, o `{"room","name"}` | La última sala (y nombre) para rellenar los campos |
| `name` | texto | El último nombre |
| `host` | `me`, `auto`, `never`, o ausente | El interruptor de anfitrión: marcado para `me`, atenuado para `never`, oculto cuando está ausente |
| `notes`, `channel_notes` (o `channel.notes`) | texto o lista de líneas | Notas de la versión |
| `update_ready` | booleano | Hay una versión más nueva descargada que espera un reinicio |
| `previous_version`, `rollback` | texto, booleano | Una versión anterior a la que volver |
| `app_dir` | texto | La carpeta de la aplicación |
| `downloads` | `{"pending":bool}` | Muestra el botón de reintentar |

### Eventos

| `event` | Campos | Qué hace la pantalla |
|---|---|---|
| `progress` | `task`, `pct` (0 a 100; lo que queda fuera se recorta; ausente o -1: ocupado sin porcentaje), `text`, `label`, `detail`, `note`; opcionales `bytes_done`, `bytes_total`, `speed_bps`, `eta_s` para una transferencia | Barra de progreso y línea. `task` la nombra: `puppet_pack`/`build_puppet_pack`, `shaders`/`prepare_shaders`, `import`, `firmware`, `check_game`, `update`/`update_download` (la línea de actualización), cualquier otra se muestra tal cual |
| `toast` | `text`, `version`, `restart` | Un aviso. Con una `version` distinta de la instalada, o con `restart: true`, lleva el botón de actualizar |
| `room_saved` | `stored` | Con `stored` vacío o `main`: "Guardado" en el HUD del juego (la partida guardada de la sala se almacenó mientras el juego corría) |
| `log` | `line` | Se escribe en el registro del emulador como `ayudante: <line>` |

### Qué debe dar `play`

La pantalla fija cada entrada de `env` en su propio proceso (se omite una clave con `=` o vacía), después inicia el cooperativo (`CoopWindow::Attach`, que lee `coop.ini` y el entorno en ese momento), y luego arranca `game` (a pantalla completa si se pide). Así que `env` es por donde el ayudante pasa los interruptores del cooperativo, `LICHII_COOP_SERVER`, `LICHII_COOP_NAME`, `LICHII_COOP_PASSWORD`, `LICHII_ORM_HOST_FILE`, `LICHII_ORM_ROOM` y similares. Las variables que el emulador lee solo cuando arranca el proceso (`nx2_switches.txt`) no pueden llegar por `play`: deben estar en ese archivo antes de que se abra la aplicación.

### Un ayudante mínimo

El ayudante falso de `lichii/app/tests/orm_app_tests.cpp` (`--fake-helper`) es un ejemplo completo en unas 60 líneas: lee una línea, la interpreta, escribe `{"id":N,"ok":true,...}` y un salto de línea, vacía el búfer y sale con `quit`.

## Mensajes y componentes

Sin mensajes de red propios. Los interruptores envían claves `ROOM_SWITCH` por la propia vía de comandos del gancho (el PvP va al PvP del original, como `/pvp on`; 09-pvp).

## Pruebas

| Archivo de pruebas | Qué comprueba |
|---|---|
| `lichii/app/tests/orm_app_tests.cpp` | Cliente: una petición es una línea JSON con id, cmd y campos; respuestas en trozos cualesquiera y con CRLF; eventos y `log`; las líneas malas se anotan y se ignoran; una petición que no se puede escribir falla de inmediato; `FailAll`; líneas demasiado largas; una función de respuesta puede enviar la siguiente petición; una petición que agota su tiempo falla y su respuesta tardía se ignora. Modelo: filas de estado a partir de booleanos, objetos y rutas; el deseo de ser anfitrión; un estado vacío deja cada fila desconocida; `play` da juego, entorno y pantalla completa; actualización, progreso, versiones y notas; la línea de una transferencia; carpetas de fila y acceso directo; URL soltadas convertidas en rutas locales; respuestas de importación; "Preparar" automático y el aviso de actualización; `room_saved`. Consultador: un estado cada vez. Con un proceso hijo real (el ayudante falso): importar y luego import_finish, un ayudante que falta, respuestas, eventos, stderr, quit, un ayudante que muere hace fallar lo pendiente |
| `tests/orm/app_channel_test.cpp` | Sin ninguna aplicación escuchando, se avisa al gancho y no se encola nada; la aplicación recibe cada petición y las respuestas vuelven para el chat; un oyente nuevo sustituye al anterior; los interruptores salen con una versión que solo avanza con un cambio, y los interruptores entran |

## Líneas de registro

| Línea | Cuándo |
|---|---|
| `orm-app: on (<ini>), helper <path>` | La pantalla está activada |
| `ayudante en marcha: <program> <args>` | El ayudante arrancó |
| `no arranca el ayudante <program>: <why>` | No arrancó |
| `ayudante: <line>` | Una línea de stderr del ayudante o un evento `log` |
| `sin respuesta del ayudante a <cmd>` | Un tiempo de espera agotado |
| `orm-app: play <game> (N variables)` | "Jugar" arrancó el juego |
| `orm-app: PvP\|Fijar aliados <on> from the screen: <sent>` | Un interruptor |
| `orm-app: /registros from the game: upload_logs` (también `/reintentar`, `/nombre`) | El juego se lo pidió a la aplicación |
| `orm-app: the game asked N but the helper is not running` | Sin ayudante |
| `coop: commands: /registros: no app in this process (fallback: nothing sent)` | En una compilación sin la pantalla |
| `orm-app: restarting ...` | Reinicio |

## Estado

- Todavía no confirmado en partida en un escenario del arnés con dos jugadores. Los flujos de la pantalla están probados con pruebas unitarias, incluso contra un proceso hijo real.

## Carencias conocidas

- Los textos de la pantalla están solo en español.
- El reinicio usa un pequeño programa auxiliar junto a la carpeta de la aplicación cuando existe (espera a que termine el proceso antiguo); sin él, la pantalla vuelve a iniciar su propio exe.
- No se ha probado el modo de ventana en el que la pantalla sigue visible mientras el juego corre en su propia ventana.
