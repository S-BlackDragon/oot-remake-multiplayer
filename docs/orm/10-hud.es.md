[English](10-hud.md) | **Español**

# 10-hud: HUD dentro de la imagen, mapa de compañeros y chat rápido

Upstream dibuja las etiquetas de nombre, el panel y el chat del cooperativo como ventanas Qt sobre la ventana del emulador y, desde test10, un HUD con aspecto de juego con los corazones de los demás como otra capa Qt. Esta función dibuja las etiquetas de nombre (con el ping), el panel, el chat, avisos cortos y una línea con las rupias de todos **dentro de la propia imagen del juego**, desde la pasada de presentación del emulador. Cada jugador puede colocar y dimensionar su propio HUD. Encima de esto van un mapa de compañeros (flechas en el borde, puntos en el minimapa y en el mapa de pausa, líneas "Ana: en <lugar>") y el chat rápido (mantén L y pulsa la cruceta).

## Qué cambia para los jugadores

- **Etiquetas de nombre dentro de la imagen**: el nombre de cada compañero en su color, con su ping en gris, sobre su cabeza, con contorno negro y tamaños de texto en números enteros que crecen con la altura de la imagen. Salen en las capturas y las grabaciones (una capa Qt no), y nunca se colocan sobre las barras negras. El nombre de un compañero caído es rojo anaranjado. Las etiquetas se ocultan durante nuestra propia escena o conversación y mientras el menú de pausa está abierto.
- **Panel (F9)**: el panel del cooperativo dibujado en la imagen, arriba a la izquierda por defecto. `LICHII_COOP_PANEL=1` lo arranca visible.
- **Chat y avisos en la imagen**: las últimas líneas de chat abajo a la izquierda (8 s cada una), el nombre del remitente en su color, avisos (entradas, salidas, caídos, conexión) en amarillo; mientras la caja de chat está abierta se muestran en su lugar las líneas Qt. Avisos cortos "<nombre> encontró <objeto>" (5 s, como máximo 3) por los cofres que abren otros.
- **Las rupias de todos** en una línea fija (arriba al centro por defecto): el nombre de cada jugador conectado en su color, una rupia verde y su cuenta (`87?` tras 1 s sin muestra, `-` mientras se desconoce). Las carteras son de cada jugador (función 06-world); esto solo las muestra.
- **Tus propios ajustes del HUD**, solo tuyos (nunca se comparan entre jugadores):
  - en `coop-options.ini`: línea de rupias activada/desactivada y dónde, lugar del panel, nombres activados/desactivados, tamaño del texto, líneas de chat;
  - mientras juegas, hasta que se cierre el juego: `/hud` (tus ajustes actuales), `/hud nombres on|off`, `/hud rupias ...`, `/hud panel <place>`, `/hud tamano <50-300>`, `/hud chat <0-6>`, `/hud reset` (vuelve al archivo), `/rupias [on|off|place]`, `/nombres [on|off]`. Lugares: `arriba-izquierda`, `arriba`, `arriba-derecha`, `abajo-izquierda`, `abajo`, `abajo-derecha` (o `top-left`... en inglés; mayúsculas, `-`, `_` y espacios no importan). Dos bloques en el mismo lugar se apilan; un bloque abajo a la izquierda va encima del chat.
- **Mapa de compañeros**:
  - un compañero en tu mismo lugar pero sin etiqueta (detrás de la cámara, lejos fuera de pantalla o a más de 40 m) es una flecha de su color en el borde de la imagen con "Ana 52 m"; a la vista pero lejos, un punto y su nombre; la nota de un compañero caído es rojo anaranjado. Esto sale de los marcadores del hook, así que también se ve en la capa Qt;
  - puntos de los compañeros en el minimapa y en el mapa de pausa, en sus colores; en una mazmorra, un anillo para un compañero en otra planta (y "Ana 2F" en el mapa de pausa). Esto necesita una tabla de mapas (`LICHII_COOP_MAP_FILE`); el mapa de pausa se activa con **F7** (el menú del juego todavía no se lee);
  - "Ana: en Bosque Kokiri": una línea por cada compañero que está en otro lugar, abajo a la izquierda (nombres de lugares en español de nuestra propia tabla).
- **Chat rápido**: mantén L y pulsa la cruceta: arriba `¡Aquí!`, derecha `¡Vamos!`, abajo `¡Espera!`, izquierda `¡Ayuda!`. Todos ven la línea en el color del remitente; `¡Aquí!` y `¡Ayuda!` además dejan durante 5 s un marcador sobre el lugar donde estaba el remitente (una flecha en el borde si está fuera de pantalla). Mientras se mantiene L, el juego no recibe la cruceta (ni objeto ni ocarina). L mantenida sola (sin cruceta) sigue siendo la pulsación mantenida de la invitación de la canción de teletransporte (06-world); una pulsación nunca hace las dos cosas.
- **Límite de ritmo del chat** en ambos lados: un remitente envía como máximo 8 líneas en 10 s, con 250 ms entre ellas (las rápidas: una por segundo, 5 en 10 s); el servidor retransmite como máximo 10 líneas en 10 s por jugador; un receptor muestra como máximo 5 líneas rápidas por remitente en 10 s.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_HUD` (nombre antiguo `LICHII_COOP_HUD`) | `LICHII_COOP_HUD=1` | desactivado (`orm_hud.cpp`) | 0/1, true/false, yes/no, on/off | nuestro HUD dentro de la imagen. Se lee primero como `LICHII_ORM_HUD` y después con el nombre antiguo `LICHII_COOP_HUD` |
| `LICHII_COOP_PANEL` | no está en el kit (comentado) | desactivado (`orm_hud.cpp`, `coop_window.cpp`) | como arriba | el panel visible al arrancar (F9 lo alterna) |
| `LICHII_ORM_HUD_RUPEES` | `1` | activado (`coop_hud_settings.h`) | como arriba | las rupias de todos en una línea fija (0: solo en el panel) |
| `LICHII_ORM_HUD_RUPEES_AT` | `arriba` | arriba | un lugar | dónde va esa línea |
| `LICHII_ORM_HUD_PANEL_AT` | `arriba-izquierda` | arriba a la izquierda | un lugar | dónde va el panel |
| `LICHII_ORM_HUD_NAMES` | `1` | activado | on/off | nombres (y ping) sobre los compañeros; 0 deja las flechas y los puntos |
| `LICHII_ORM_HUD_SIZE` | `100` | 100 | de 50 a 300 (por ciento) | tamaño del texto |
| `LICHII_ORM_HUD_CHAT_LINES` | `6` | 6 | de 0 a 6 | líneas de chat mostradas |
| `LICHII_COOP_MAP` | `LICHII_COOP_MAP=1` | activado salvo `0` (`coop_map.cpp`) | `0` lo desactiva; cualquier otro valor o sin definir lo deja activado | el mapa de compañeros; los puntos y las líneas necesitan el HUD activado |
| `LICHII_COOP_MAP_FILE` | no está en el kit | ninguno | una ruta de archivo | la tabla de mapas (plantas, vistas del minimapa y del mapa de pausa; formato en `lichii/hook/coop_map.h`). Sin ella: solo flechas y líneas, sin puntos |
| `LICHII_COOP_MAP_DEBUG` | no está en el kit | desactivado | on/off | nuestro propio punto amarillo en los mapas, para calibrar la tabla |
| `LICHII_ORM_QUICK_CHAT` | `LICHII_ORM_QUICK_CHAT=1` | desactivado (`orm_quick_chat.h`) | on/off | la combinación L + cruceta; las líneas rápidas de los demás se muestran en cualquier caso |
| `LICHII_ORM_QUICK_CHAT_BUTTON` | no está en el kit (comentado, `R` como ejemplo) | `L` | `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `LCLICK`, `RCLICK` | el botón mantenido. Otro valor: se usa L y una línea de aviso |
| `LICHII_COOP_NOTIFY` | no está en el kit | activado salvo `0` (de upstream) | `0` | los avisos de chat de upstream; nuestro HUD los dibuja |

**`LICHII_COOP_HUD` son dos interruptores.** El HUD con aspecto de juego de upstream (`coop_hud.*`, los corazones de los demás) está activado salvo que `LICHII_COOP_HUD=0`. El nuestro se llamaba `LICHII_COOP_HUD` antes de test10 y ahora es `LICHII_ORM_HUD`; un antiguo `LICHII_COOP_HUD=1` sigue activando el nuestro. El kit tal como se entrega escribe `LICHII_COOP_HUD=1`, así que ambos dicen "activado", y **gana el nuestro**: con nuestro HUD activado, `coop_window.cpp` no crea ni el panel Qt y los marcadores ni la capa Qt de corazones de upstream (`HudLayer`), por lo que los corazones de los demás de upstream no se ven. Para recuperar el aspecto de upstream: `LICHII_ORM_HUD=0` (el nuestro desactivado; el de upstream sigue activado). Escribir `LICHII_ORM_HUD=1` en el kit diría lo mismo con más claridad.

Las líneas de ajustes `LICHII_ORM_HUD_*` se leen una sola vez al arrancar. Un valor que no aceptan conserva el valor por defecto y deja en el registro, por ejemplo, `coop: switch LICHII_ORM_HUD_SIZE=1000 is not a percent from 50 to 300: 100 (its default)`.

## Cómo funciona

El dibujo es lo más barato que funciona en cualquier tarjeta Vulkan: rectángulos opacos con `vkCmdClearAttachments` dentro de un render pass propio (`loadOp LOAD`) sobre el fotograma terminado. Sin shader, sin pipeline, sin búfer. El texto es una fuente de mapa de bits de 7x13; cada fila de píxeles encendidos se convierte en tiras, y tiras iguales en filas consecutivas forman un solo rectángulo; primero se dibuja un contorno. El coste es que no hay mezcla (blending): una etiqueta se dibuja entera o no se dibuja (por debajo del 50 % de desvanecimiento se omite) y el panel tiene un contorno, no un fondo translúcido. Como máximo 6000 rectángulos por fotograma.

| Archivo | Qué hace |
|---|---|
| `lichii/hook/orm_hud.h`, `.cpp` | Modelo y disposición portables: etiquetas, panel, filas de chat, avisos, línea de rupias, puntos y líneas del mapa, flechas de borde, escalas, lugares, el interruptor `On()` y el estado del panel de F9. Se construye a partir de lo que el hook ya calcula (marcadores, lista de jugadores, líneas del panel) |
| `lichii/hook/coop_hud_game.cpp` | Pegamento en `core`: refresca la lista de jugadores y el panel cada 200 ms, los marcadores en cada llamada, aplica los ajustes en vivo y el bit de pausa en cada fotograma, añade el mapa |
| `lichii/hook/coop_hud_vk.h`, `.cpp` | El lado del emulador (`video_core`): el render pass y los `ClearAttachments` agrupados |
| `lichii/hook/coop_hud_font.h` | La fuente: ProggyClean (MIT), ASCII y Latin-1, generada por `tools/hud_font/make_font.py` a partir del `.ttf` de Dear ImGui |
| `lichii/hook/coop_hud_settings.h`, `.cpp` | Los ajustes de cada jugador: lectura de las seis líneas, los comandos `/hud`, `/rupias`, `/nombres`, el almacén en vivo |
| `lichii/hook/coop_notices.h` | Solo cabecera: los avisos cortos de "objeto encontrado" (5 s, como máximo 3) |
| `lichii/hook/coop_map.h`, `.cpp` | Matemáticas portables del mapa: del mundo a la pantalla y flechas de borde, del mundo al minimapa / mapa de pausa (centro, espejo, giro, extensión; acotado al borde del fotograma con una flecha), plantas desde la tabla, las líneas "en <lugar>" con nombres de lugares en español |
| `lichii/hook/coop_map_game.cpp` | Lee `LICHII_COOP_MAP_FILE` una vez y alimenta el mapa en cada fotograma |
| `coop/host/include/coop/host/quick_chat.hpp`, `coop/host/src/quick_chat.cpp` | Los cuatro textos rápidos, la combinación botón + cruceta (un flanco con el botón mantenido; una cruceta que ya estaba pulsada no envía nada), límites del remitente y del receptor, marcadores |
| `coop/host/include/coop/host/shoulder.hpp`, `coop/host/src/shoulder.cpp` | Un botón, dos usos: L sola es la pulsación mantenida de la invitación de teletransporte, L + cruceta un mensaje rápido que cancela esa pulsación hasta que se suelta L |
| `lichii/hook/orm_quick_chat.h`, `.cpp` | Pegamento: lee los dos interruptores, sondea el mando, fija la máscara de entrada, envía la línea como `CHAT` normal |
| `lichii/hook/orm_input_mask.h` | Los bits de la cruceta que el emulador parcheado quita de los botones del invitado mientras se mantiene el botón de chat rápido |
| `lichii/hook/video.cmake` | Añade las fuentes de `video_core` |
| `lichii/patches/0100-orm-hud.patch` | Parche de Fritchii, todo bajo `#ifdef LICHII_COOP`: `video_core/CMakeLists.txt`, `vk_blit_screen.h/.cpp`, `renderer_vulkan.cpp`: llama a nuestro dibujo justo después de la pasada de adaptación a la ventana en `BlitScreen::DrawToFrame`, de modo que las capturas también lo incluyen |
| `lichii/patches/0101-orm-quick-chat-input.patch` | Parche de Fritchii: `NPad::RequestPadStateUpdate` quita los bits de `OrmInputMask::Guest()` de lo que lee el juego. Las lecturas propias del hook (también la entrada por guion) siguen viendo todos los botones. Máscara 0 en cualquier otro caso; no cambia nada con `LICHII_COOP` desactivado |

Puntos de enganche en archivos de upstream:

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_window.cpp` | Con nuestro HUD activado: sin panel Qt, sin marcadores ni capa de corazones de upstream; F9 alterna nuestro panel; F7 alterna el mapa de pausa de compañeros (con el mapa activado) en lugar del panel de depuración de upstream; líneas de chat Qt solo mientras se escribe; sin marcadores durante la pausa |
| `lichii/hook/coop_hook.cpp`, `coop_hook.h` | `Marker::number`, `Markers::paused` (el bit de pausa de `coop_menus.cpp`, el `Sequence_IsPause` del juego, copiado en el mismo fotograma), líneas de chat con número, nombre, color y "quick"; `CurrentMates` y flechas de borde en `Place`; `SendChat` pasa las líneas `/hud` a los ajustes; líneas de cartera (`coop: wallet:`) |
| `lichii/hook/coop_chat.h` | Las líneas de chat llevan número del remitente, nombre, color, quick |
| `lichii/hook/coop_camera_probe.cpp` | El lector de cruceta del espectador caído no ve la cruceta mientras se mantiene el botón de chat rápido |
| `server/session.cpp`, `session.hpp` | El límite de chat del relé: `chat_count` 10 en `chat_window_us` 10 s por jugador (0: desactivado), contado en `chat_limited` |
| `coop/host/include/coop/host/notify.hpp` | Números de jugador hasta `kMaxPlayers` (seis) |

## Mensajes y componentes

Ninguno propio. Los mensajes rápidos viajan como el `CHAT` de upstream (tipo 9, canal 0): un juego más antiguo los muestra como chat normal, uno más nuevo reconoce el texto exacto; el receptor toma la posición del marcador de la última muestra del remitente. Las cuentas de rupias vienen de la muestra de cartera por jugador de 06-world (`PLAYER_EQUIP` componente 29, `rupees`).

## Pruebas

| Prueba | Qué comprueba |
|---|---|
| `tests/orm/orm_hud_test.cpp` | Interruptores, UTF-8, escalas, el glifo de la fuente píxel a píxel con su contorno, ping en gris, del objetivo de escena a la imagen con barras, recorte, orden de lejos a cerca, desvanecimientos y NaN, flecha y punto, color del compañero caído, `?` para caracteres desconocidos, panel y colores, el tope de rectángulos, filas de chat, avisos |
| `tests/lichii/hud_settings_test.cpp` | Lugares, tamaño, líneas, activado/desactivado; `coop-options.ini` (valores por defecto, todo definido, valores erróneos y su línea); `/hud` y su texto; cada comando y sus errores; `RupeeText`; pausa (sin etiquetas, los puntos del mapa se quedan); nombres desactivados (las flechas se quedan); chat recortado; la línea de rupias saca la lista del panel; tamaño en las escalas; colocación en cada esquina, encima del chat, dividida en dos líneas |
| `tests/lichii/map_test.cpp` | Del mundo a la pantalla y flechas de borde (detrás de la cámara en el borde inferior), del mundo al mapa con acotado y flecha, plantas, lectura de la tabla, líneas "en <lugar>", activado salvo `0` |
| `tests/orm/quick_chat_test.cpp` | La tabla, la combinación, la máscara, límites del remitente y del receptor, marcadores, lectura de interruptores, el registro del chat |
| `tests/host/shoulder_test.cpp` | L compartida entre la invitación y el chat rápido |
| `tests/server/session_test.cpp` | 10 líneas en 10 s, una línea de registro por ráfaga, `chat_count = 0` desactivado |

Escenarios del arnés (vivirán en `tools/orm/harness`): `comunicacion-aqui` (B envía `¡Aquí!`; el registro de A tiene la línea y el marcador; una ráfaga de diez pulsaciones L+arriba envía tres; A envía `¡Ayuda!`; se comprueba que L queda libre; se ejecuta con el HUD activado), `mapa-flecha` (B cerca, lejos, detrás de la cámara y en otro lugar; la prueba son capturas en cada marca), `mapa-piso` (puntos y anillos de planta con una tabla de mapas), `cartera-cada-uno` (la cartera de cada jugador registrada y listada en el panel).

## Líneas de registro

| Línea | Dice |
|---|---|
| `coop: switch LICHII_ORM_HUD=<value> is not ...` | un valor que el interruptor no acepta |
| `coop: hud: HUD: nombres sí, rupias sí (arriba), panel arriba-izquierda, tamaño 100 %, chat 6 líneas` | al arrancar, cuando `coop-options.ini` cambia un ajuste |
| `coop: hud: /rupias off -> HUD: ...` | cada comando de ajustes |
| `coop: map: no LICHII_COOP_MAP_FILE, no dots on the game's maps` | mapa activado, sin tabla |
| `coop: map: <file>: N dungeons with floors, N minimap and N pause map views, ...` / `coop: map: <file>: line N: ...` | la tabla leída, y cada línea errónea |
| `coop: quick: on (LICHII_ORM_QUICK_CHAT): hold L and press the d-pad: up '¡Aquí!', ...` / `coop: quick: off (LICHII_ORM_QUICK_CHAT=1 turns it on); quick messages from others still show` | el chat rápido al arrancar |
| `coop: quick: LICHII_ORM_QUICK_CHAT_BUTTON=<v> is not L, R, ZL, ZR, PLUS, MINUS, LCLICK or RCLICK: L is used` | botón erróneo |
| `coop: quick: sent '¡Aquí!' (L+DUP)`, `coop: quick: '¡Aquí!' (L+DUP) not sent: at most one a second and 5 in 10 s ...`, `coop: quick: P2 Ana '¡Aquí!'`, `coop: quick: marker for P2 at (...) in <scene> for 5 s` | remitente y receptor |
| `coop: chat: not sent: at most 8 lines in 10 s, 250 ms apart` | el límite de chat del remitente |
| Servidor: `player N sent chat over its rate (10 lines in 10 s): dropped until it slows down` | el límite del relé, una vez por ráfaga |
| `coop: wallet: P1 'Ana' (you) has 87 rupees` | las carteras que lista el HUD |

## Estado

| Parte | Estado |
|---|---|
| Chat rápido (combinación, límites, marcadores, cruceta retenida del juego, L libre al jugar) con el HUD activado | confirmado en partida en nuestra compilación basada en test9 (escenario `comunicacion-aqui`, 18/18) |
| Flechas de borde del mapa de compañeros y líneas "en <lugar>" | confirmado en partida en nuestra compilación basada en test9 (escenario `mapa-flecha`, 4/4) |
| Lista de carteras en el panel | confirmado en partida en nuestra compilación basada en test9 (escenario `cartera-cada-uno`) |
| Etiquetas, panel y chat dentro de la imagen | dibujados en esas ejecuciones; ningún escenario los comprueba por separado |
| Ajustes por jugador (`/hud ...`, lugares, tamaño), puntos y plantas del mapa, ocultación en el menú de pausa | todavía no confirmado en partida |

## Carencias conocidas

- Con nuestro HUD activado, los corazones Qt de los demás de upstream no se muestran (ver Interruptores).
- Solo Vulkan: con OpenGL no hay HUD dentro de la imagen (las capas Qt siguen disponibles con el interruptor desactivado).
- Los nombres fuera de Latin-1 (cirílico, CJK, emoji) se muestran como `?`.
- Las etiquetas se ocultan en nuestras escenas, conversaciones y en el menú de pausa, pero no sobre los mensajes del juego que no son un evento.
- La generación de fotogramas interpola el HUD con la imagen (posibles estelas en el texto al girar rápido). Un swapchain HDR necesitaría convertir los colores de limpieza (Fritchii hoy no tiene ninguno).
- Los puntos del mapa necesitan una tabla de mapas construida a partir de los archivos del juego de cada PC; no se entrega ninguna tabla (no hay datos del juego). El mapa de pausa se activa con F7: el menú de mapa propio del juego y su pestaña de plantas todavía no se leen. Los marcos del minimapa y del mapa de pausa aún están por calibrar en una partida con `LICHII_COOP_MAP_DEBUG=1`.
- Pulsar L y una dirección de la cruceta en el mismo fotograma puede dejar que ese fotograma de cruceta llegue al juego (la máscara se aplica desde el fotograma siguiente).
- Las líneas "en <lugar>" abajo a la izquierda pueden solaparse con un panel movido abajo a la izquierda.
- Las preferencias del HUD viven en dos sitios: `orm_stats.txt` (`/hud on|off`, `/mapa`) y `coop-options.ini` más la memoria (los ajustes).
