[English](01-platform.md) | **Español**

# 01-platform: Capa de plataforma delgada, un lector de interruptores, registro de tiempos por fotograma

Todo lo que nuestro código de enganche necesita de lo que ejecuta el juego (memoria del invitado, el mando, llamadas al juego, carpetas, el entorno y el registro) pasa por una interfaz pequeña, de modo que pasar del emulador a Hookshot es un solo adaptador y no un cambio en cada función. Con ella vienen un único lector para cada interruptor de activado/desactivado y un registro opcional de tiempos por fotograma.

## Qué cambia para los jugadores

Nada que puedan ver. Hay dos detalles pequeños que sí pueden notar:

- Todo interruptor de ORM acepta `1`, `true`, `yes`, `on` para activarlo y `0`, `false`, `no`, `off` para desactivarlo, en mayúsculas o minúsculas. Cualquier otro valor deja el valor por defecto del interruptor y escribe una línea de registro con el nombre del interruptor, el valor y el valor por defecto. (Antes, un valor como `false` podía activar un interruptor.)
- Con `LICHII_ORM_FRAME_LOG` definido, el juego escribe una línea por fotograma presentado en un archivo CSV, para comparar la fluidez entre el emulador y Hookshot.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_FRAME_LOG` | no está en el kit | sin definir: desactivado (`coop_frame_log.cpp`, `CoopPlatform::Env`) | una ruta de archivo | Una línea CSV `frame,seconds,ms` por fotograma presentado, y cada 10 s una línea de resumen en el registro |
| `LICHII_ORM_HOOKSHOT_GATE` | no está en el kit | desactivado (`coop_downed_game.cpp`, solo compilaciones de Hookshot) | palabras de activado/desactivado | Avisa a una compilación de Hookshot de que su compuerta de fin de partida existe, de modo que el estado de caído (04-downed) pueda quedar activado allí |

El lector en sí (`coop_switch.h`) no es un interruptor. Sus reglas, para todo interruptor leído a través de él:

| Valor | Lectura |
|---|---|
| sin definir o vacío | el valor por defecto del interruptor |
| `0`, `false`, `no`, `off` (en cualquier caso, se ignoran los espacios alrededor) | desactivado |
| `1`, `true`, `yes`, `on` (en cualquier caso) | activado |
| cualquier otra cosa | el valor por defecto, y la línea de registro de más abajo |

Los interruptores que renombramos de `LICHII_COOP_*` a `LICHII_ORM_*` se leen primero con el nombre nuevo y, si este no está definido, con el antiguo, de modo que los kits y escenarios de prueba anteriores siguen funcionando (`CoopSwitch::On(name, old_name, default)`). Los interruptores propios de upstream mantienen su propia lectura.

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `lichii/hook/platform.h` | La interfaz. Por fotograma: un objeto `CoopPlatform::Platform` que crea el fotograma del enganche y entrega al `Frame` de cada función: `MainBase()`, `Memory()` (lecturas, escrituras, el compare-and-swap de 32 bits que usan las colas de banderas), `Write64()`, `PadButtons()` (bits de Npad), `NudgeRightStick()`, y dos peticiones al juego que hace el pegamento de jefes de upstream, `AskSceneChange()` y `MoveLink()`. Funciones libres de todo el proceso que el adaptador define al enlazar: `AddCaller()` (llamadas al juego en su propio hilo, mismo contrato que `CoopPuppet::Director::Step`, ejecutadas por orden), `FolderPath()` (`mods`, `config`). Ayudas: `GuestView` (la vista de bytes que usan los llamadores y el recorrido de la cámara) y `MainCall()` (una llamada a `main + offset`). Sin cabecera del emulador, así que compila en las pruebas unitarias |
| `lichii/hook/platform_env.h` | Los dos servicios que no necesitan el juego: `CoopPlatform::Env(name)` (distingue sin definir de vacío; `_dupenv_s` en MSVC) y las macros `ORM_LOG_INFO/WARNING/ERROR/DEBUG`. La plataforma se elige al compilar: `LICHII_COOP` = el emulador (las macros son `LOG_*(Core, ...)`, así que cada línea conserva su archivo y su línea), `ORM_PLATFORM_HOOKSHOT` = una compilación de Hookshot (el adaptador define `CoopPlatform::Log`), ninguna = las pruebas unitarias (las macros no hacen nada). Las dos a la vez es un error de compilación. `ORM_PLATFORM_LIVE` vale 1 cuando corre un juego |
| `lichii/hook/platform_emulator.h`, `.cpp` | El adaptador del emulador, `CoopPlatform::Emulator`: cada llamada es lo que nuestros archivos del enganche hacían por sí mismos antes de la capa, movido aquí sin cambios. `AddCaller` va a `CoopSvc::AddCaller`; `FolderPath` a las carpetas de carga y de configuración del emulador. En el módulo de Hookshot de upstream (`COOP_HOOKSHOT`) el mando viene de `CoopRuntime::PadButtons`; con un puente más antiguo lo registra una vez y no devuelve botones |
| `lichii/hook/coop_switch.h` | El único lector de interruptores (solo cabecera): `Parse`, `From`, `Value`, `Find`, `On(name, default)`, `On(name, old_name, default)`, y la línea de registro para un valor desconocido. Los interruptores selectores (más que activado/desactivado) construyen su propia línea con `Unknown(..., accepted, default)` |
| `lichii/hook/coop_frame_log.h`, `.cpp` | `CoopFrameLog::Recorder` (portable: número de fotograma, segundos desde el primer fotograma, intervalo) y `Tick()`, llamado lo primero en `CoopHook::OnFrame` para que mida las presentaciones del invitado. Una ventana de 10 s da la mediana, el peor fotograma y los fotogramas de 50 ms o más. Las columnas del CSV coinciden con `RX_VK_FRAME_LOG` de Hookshot, de modo que un mismo script puede leer ambos |
| `lichii/hook/video.cmake` | Lo incluye el CMake del núcleo de vídeo parcheado (parche 0100): añade el paso de Vulkan del HUD en la imagen (10-hud). Cambiar lo que compila no requiere un parche nuevo |

Entre nuestros archivos de funciones que reciben la plataforma en su `Frame` están, entre otros, el pegamento de edad, caído, interruptor sostenido, sonda de cámara y eventos de historia; el reloj, Epona, Continue y el lector de interruptores usan sus llamadores, el entorno y el registro. Regla que mantuvimos para el código nuevo: un archivo nuestro en `lichii/hook` no incluye `core/`, `hid_core/`, `video_core/` ni `common/`; usa `platform.h`, `ORM_LOG_*` y `CoopPlatform::Env`, y lo que falte se añade a la capa.

Los archivos de upstream que todavía hablan directamente con el emulador (entrada de prueba, objetos, aparición, el enganche, botín, pegamento de jefes, teletransporte, hora, svc, sonda de escena, guardado, banderas, seguimiento de cámara) se dejaron como están: cambiarlos entraría en conflicto en cada nueva fijación de versión. Casi todo lo que usan es memoria y llamadores, que la capa ya ofrece.

### Puntos de enganche en archivos de upstream

| Archivo de upstream | Qué añadimos |
|---|---|
| `lichii/hook/coop_hook.cpp` | Construye un `CoopPlatform::Emulator` por fotograma y lo pasa al `Frame` de nuestras funciones; llama a `CoopFrameLog::Tick()` lo primero en `OnFrame` |
| `lichii/hook/core.cmake` | Nuestras fuentes `coop_*`, `orm_*` y `platform_*`, `coop_frame_log.cpp` y `platform_emulator.cpp` |
| `lichii/hook/qt.cmake` | `orm_chat_complete.cpp`, enlaza `coop_overlay` y `coop_host` en el objetivo de Qt, incluye `lichii/app/app.cmake` (14-app) |
| `hookshot/CMakeLists.txt` | El filtro de fuentes del módulo leía solo `coop_*.cpp` de `core.cmake`; ahora lee `(coop\|orm\|platform)_*.cpp`, de modo que nuestros archivos y el adaptador se compilan dentro de `hookshot_coop.dll` contra `hookshot/compat/` |
| `lichii/hook/coop_testinput.cpp` | En Hookshot el empujón del stick derecho del espectador no tiene inyector de entrada: no hace nada y lo dice una sola vez |

### Qué le falta todavía a una compilación de Hookshot

Comprobado leyendo el árbol, sin compilar nada: la compuerta de fin de partida de 04-downed es un parche de código en `main+0x4ae6bc` (un `bl` a nuestra cueva), y Hookshot no aplica parches de código. El port sería un `ENTRY_HOOK` sobre la función llamada (`main+0x4aa890`) que devuelva 0 mientras la palabra de la compuerta esté puesta; una compilación así define entonces `LICHII_ORM_HOOKSHOT_GATE=1`. Hasta entonces el estado de caído se desactiva solo en Hookshot (línea de registro más abajo) y corre el fin de partida normal. Tampoco hay allí: el HUD en la imagen (un parche de Vulkan del emulador) y el empujón de stick del espectador (el puente no exporta el stick).

## Mensajes y componentes

Ninguno.

## Pruebas

| Archivo de prueba | Qué comprueba |
|---|---|
| `tests/orm/switch_test.cpp` | Las palabras de activado y desactivado en cualquier caso; los valores sin definir, vacíos y desconocidos toman el valor por defecto; una línea de registro con el nombre del interruptor, el valor y el valor por defecto; gana el nombre `LICHII_ORM_` y el antiguo `LICHII_COOP_` se lee cuando aquel no está definido; `Find` nombra la variable que estaba definida; la línea de un selector dice qué acepta; el selector del interruptor sostenido acepta las palabras de activado/desactivado y `press`, `log` |
| `tests/lichii/platform_test.cpp` (compilado con `coop_frame_log.cpp`) | `GuestView` lee y escribe a través de la memoria del invitado y rechaza lo que no está mapeado; `MainCall` coloca la función en main + offset; `Env` distingue sin definir de vacío y los interruptores leen vacío como sin definir; la primera línea del registro de fotogramas no tiene intervalo; una ventana da la mediana, el peor y los picos |

## Líneas de registro

| Línea | Cuándo |
|---|---|
| `coop: switch NAME=VALUE is not 0/1/true/false/yes/no/on/off: on (its default)` (u `off`) | Un interruptor recibió un valor que no acepta |
| `coop: frames: one line per presented frame to <file> (LICHII_ORM_FRAME_LOG); a summary every ...` | Arrancó el registro de fotogramas |
| `coop: frames: LICHII_ORM_FRAME_LOG <file> cannot be written: no frame log` | No se pudo abrir el archivo |
| `coop: frames: N in S s (F fps), median M ms, worst W ms, K of 50 ms or more` | Cada 10 s mientras corre el registro de fotogramas |
| `coop: check: downed fallback off on Hookshot: no game over gate in this build (needs the ENTRY_HOOK at main+0x4aa890 and LICHII_ORM_HOOKSHOT_GATE=1): the normal game over` | Compilación de Hookshot sin la compuerta |
| `coop: platform: Hookshot's bridge gives no pad buttons (an older build): our features that read the pad (revive with A, quick messages) do nothing` | Hookshot con un puente más antiguo |
| `coop: spectator: right-stick nudge not played on Hookshot (no input injector); the camera stays as it is` | Hookshot, despertar de la cámara del espectador |

## Estado

- Capa y lector de interruptores en el emulador: toda función confirmada en partida pasa por ellos (véanse los otros archivos), así que el camino del emulador está confirmado en partida en nuestra compilación basada en test9 y en test10. La capa en sí no cambia ningún comportamiento.
- Registro de fotogramas: todavía no confirmado en partida en una ejecución de dos jugadores (probado con pruebas unitarias).
- Hookshot: todavía no confirmado en partida. Nada nuestro se ha compilado ni ejecutado en `hookshot_coop.dll`; solo se cambió el filtro de fuentes.

## Carencias conocidas

- Los archivos de upstream listados arriba siguen llamando directamente al emulador y deben pasar a la capa antes de que funcione una compilación solo para Hookshot.
- En Hookshot: sin compuerta de fin de partida (caído desactivado), sin HUD en la imagen, sin empujón de stick del espectador.
