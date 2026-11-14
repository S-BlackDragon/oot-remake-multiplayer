[English](06-world.md) | **Español**

# 06-world: Un mundo para el grupo

Todo lo que hace que el mundo sea el mismo en cada partida más allá de las banderas compartidas del upstream: niveles de agua, interruptores de suelo mantenidos, cofres y puertas que se ven abrirse en todas las pantallas, lo que cambia una charla, la invitación de las canciones de teletransporte, las listas de banderas del kit para cada parte del juego, las reglas de objetos con una cartera por jugador, la protección de la capacidad de corazones, un solo reloj, la Epona del grupo y el estado de pausa y de pantalla de título. Cada parte tiene su propio interruptor, y cada una puede usarse por separado.

La regla que hay detrás de todo (nuestro cooperativo anterior en lockstep para el juego de N64 sobre un fork de Ship of Harkinian la tenía por construcción): lo que el grupo hizo para siempre es del grupo; lo que un jugador lleva en las manos, su vida, sus rupias y sus saludos siguen siendo suyos.

## Qué cambia para los jugadores

| Parte | Qué ve un jugador |
|---|---|
| Niveles de agua | Un nivel de agua que fija otro jugador es el mismo en tu partida (los tres niveles del Templo del Agua, el sótano del Gran Árbol Deku, el desagüe del Fondo del Pozo, dos salas del Templo del Agua, el pozo del Templo del Bosque; la sala 14 de Jabu-Jabu, si se pide). Nunca se mueve bajo tu Link mientras nada, cae o trepa en esa agua: espera a que esté de pie (20 s como máximo). |
| Interruptores de suelo mantenidos | Un jugador está sobre un interruptor y otro cruza la puerta que este mantiene abierta (el muro de fuego del Templo del Fuego, las barras del Templo del Espíritu). |
| Cofres | Cuando un compañero abre un cofre ves abrirse la tapa de su cofre en tu pantalla con su patada, y lo que encontró aparece sobre la cabeza de su marioneta mientras lo sostiene en alto, con un breve aviso "<nombre> encontró <objeto>" (el aviso lo dibuja [10-hud](10-hud.es.md)). |
| Puertas | Una puerta que empuja otro jugador se abre y se cierra también en tu pantalla, en lugar de verlo atravesar una puerta cerrada. Un jugador que sale o llega por una zona de carga se aleja andando hasta perderse de vista, en lugar de quedarse congelado o deslizarse por la sala. |
| Charlas | Lo que una charla cambia en el mundo es igual para todos (Mido dejándote pasar, un Kokiri que se movió, un trato hecho una sola vez); los saludos y el "ya habló" siguen siendo de cada jugador. Los demás lo ven cuando ya no pueden verlo saltar (fuera de la escena, o con el habitante lejos o ausente). El habitante con quien hablas es tuyo mientras hablas, así que los demás ven tu charla en su copia. |
| Canciones de teletransporte | Cuando un compañero se va con una canción de teletransporte, un aviso dice adónde; mantener L durante los 10 s que la invitación sigue abierta te lleva allí también. |
| Listas del kit | Rocas, muros, tapas de grutas, bloqueos, Gorons de los templos, premios de minijuegos, cadenas de trueque, botellas, judías y el resto del estado del mundo exterior se comparten por partes del juego (tabla más abajo). |
| Objetos | Los objetos, las mejoras, las bolsas, las carteras (su tamaño) y las botellas se comparten; las rupias, la munición, el contenido de las botellas, el equipo puesto, los botones de objeto, la vida y la magia actuales son de cada jugador. El medidor de magia se llena para todos cuando crece. Las rupias de todos se muestran en el HUD. |
| Corazones | Un contenedor de corazón que coge cualquiera sube los corazones de todos los Link, y se conserva el valor más alto visto aunque el juego escriba uno más bajo. |
| Reloj | Un solo reloj para el grupo, propiedad del anfitrión; los relojes de los demás se acercan suavemente a él (el cielo corre un poco más rápido o más lento), sin saltos. La Canción del Sol o dormir una noche saltan para todos. |
| Epona | Un jugador la gana en Lon Lon y todas las partidas la tienen; cada jugador llama y monta la suya; la carrera de Ingo espera al grupo. |
| Pausa | Un compañero en un menú se queda quieto con "(pausa)" en su etiqueta y renuncia a sus enemigos; a uno en la pantalla de título no se le dan objetos. |

## Interruptores

Valor del kit es la línea tal como se entrega en `tools/release/kit/coop-options.ini`. Valor del código es lo que hace el código con la variable sin definir. Los interruptores de encendido/apagado aceptan 0/1/true/false/yes/no/on/off (en mayúsculas o minúsculas); un valor desconocido mantiene el valor por defecto y registra `coop: switch NAME=VALUE is not ...`. Los nombres antiguos `LICHII_COOP_*` se leen cuando el nombre `LICHII_ORM_*` no está definido, donde se indica.

### Niveles de agua

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_WATER` (old `LICHII_COOP_WATER`) | `LICHII_ORM_WATER=1` | off (`coop_hook.cpp`) | on/off | Comparte los niveles de agua, con la espera mientras se nada |
| `LICHII_ORM_WATER_MAX_WAIT_S` (old `LICHII_COOP_WATER_MAX_WAIT_S`) | comentada (`# ...=20`) | 20 | 0 a 3600; 0 espera para siempre | Espera máxima bajo un Link que nada |
| `LICHII_ORM_WATER_JABU` (old `LICHII_COOP_WATER_JABU`) | comentada | off | on/off | También la sala 14 de Jabu-Jabu (el upstream la dejó fuera: sus interruptores de escalón la ponen en ambos sentidos) |

### Interruptores de suelo mantenidos

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_HELD_SWITCHES` (old `LICHII_COOP_HELD_SWITCHES`) | comentada (sin definir) | `flag` (`coop_held_switch_game.cpp`) | `flag`; `log` (solo observar y registrar); `1`/on/`press` (la bandera y las palabras de actor de una lista); `0`/off | Cómo se pulsa aquí un interruptor sobre el que está otro jugador |
| `LICHII_ORM_HELD_SWITCH_LIST` (old `..._HELD_SWITCH_LIST`) | comentada | la lista integrada | una ruta de archivo | Más interruptores y sus palabras de actor |
| `LICHII_ORM_HELD_SWITCH_PROBE` (old `..._HELD_SWITCH_PROBE`) | comentada | off | on/off | Depuración: registra las palabras de un interruptor que nuestro Link pisa o deja de pisar, como líneas de lista |
| `LICHII_ORM_FLAG_PROBE`, `LICHII_ORM_FLAG_PROBE_NAMES` | no está en el kit | off | on/off; nombres de banderas | Depuración: registra cada cambio de las banderas elegidas y avisa cuando una rebota entre partidas (una candidata a `held:`) |

### Cofres y puertas

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_CHEST_OPEN` | no está en el kit | on (`coop_chest_open.cpp`) | on/off | La tapa en todas las pantallas (`CHEST_OPEN`) y el hallazgo sobre la marioneta |
| `LICHII_COOP_DOORS` | no está en el kit | on; solo `0` lo apaga (`coop_doors_game.cpp`) | `0` o cualquier otro | El espejo de puertas |
| `LICHII_COOP_PASSAGES` | no está en el kit | on; solo `0` lo apaga (`coop_puppet_config.cpp`, `coop_fairy.cpp`) | `0` o cualquier otro | Marionetas que salen de las zonas de carga y entran en ellas |

### Charlas

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_COOP_TALK` | `LICHII_COOP_TALK=shared_talk.txt` | sin definir: no se comparte ninguna bandera de charla (`coop_flags.cpp`) | una ruta de archivo | Las banderas de charla que se comparten, con la regla que las retiene mientras se ven |
| `LICHII_COOP_FLAGS_DEFER` | comentada | off; se activa sola con `LICHII_COOP_TALK` (`coop_hook.cpp`) | on/off | La regla de "esperar mientras se ve" solo para las reglas de charla integradas |
| `LICHII_COOP_FLAGS_DEFER_LIST` | comentada | ninguna | una ruta de archivo | Más reglas de espera, o que sustituyen a las existentes |
| `LICHII_COOP_NPC` | `LICHII_COOP_NPC=1` | off (`coop_objects.cpp`) | on/off | El reclamo de charla: el habitante con quien hablas es tuyo mientras hablas y 1,5 s después |
| `LICHII_COOP_FLAG_BURST_MS` | no está en el kit | 100 | milisegundos; 0 envía cada fotograma | Los cambios de bandera de una ráfaga salen como un solo mensaje |

La postura y la mirada del habitante en las pantallas de los demás (`LICHII_COOP_NPC_POSE`, `LICHII_COOP_NPC_GAZE`) forman parte de [11-puppets](11-puppets.es.md).

### Canciones de teletransporte

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_COOP_WARP_INVITE` | `LICHII_COOP_WARP_INVITE=1` | off (`coop_songs_game.cpp`) | on/off | La invitación de las canciones de teletransporte |
| `LICHII_COOP_WARP_INVITE_BUTTON` | sin definir (el comentario nombra `R`) | `L` | `L`, `R`, `ZL`, `ZR`, `PLUS`, `MINUS`, `LCLICK`, `RCLICK` | El botón que hay que mantener |
| `LICHII_COOP_WARP_INVITE_FALLBACK` | no está en el kit | el gemelo del otro lado del mando | los mismos nombres, no el botón principal | Se usa si una comprobación descubre que el juego usa el botón principal |
| `LICHII_ORM_LIVE_CHECK` | no está en el kit | on (`coop_songs_game.cpp`) | on/off | Comprobación de solo lectura de que una bandera del mundo mueve a su actor en vivo (primer usuario: los bloques de la Canción del Tiempo) |

### Listas del kit y objetos

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_COOP_DUNGEON_LISTS` | los nueve archivos `shared_dungeon_*.txt`, separados por comas | ninguna (`coop_hook.cpp`) | rutas separadas por comas, o una carpeta (`.`: todos los `shared_dungeon_*.txt` de ella) | Listas de banderas por parte del juego, sobre las listas integradas del upstream |
| `LICHII_ORM_ITEM_RULES` (old `LICHII_COOP_ITEM_RULES`) | `LICHII_ORM_ITEM_RULES=shared_items.txt` | ninguna: las reglas de objetos del upstream (`coop_flags.cpp`) | una ruta de archivo | Cambios a las reglas de objetos |
| `LICHII_ORM_TRADE_GUARD` | no está en el kit | on (`coop_hook.cpp`) | on/off | El paso de la cadena de Biggoron nunca retrocede bajo un jugador que lleva un objeto de trueque con temporizador |
| `LICHII_ORM_BEAN_CHECK` | no está en el kit | on | on/off | Solo lectura: comprueba en cada visita que el Bool compartido de un terreno significa "hay una judía plantada" |
| `LICHII_ORM_PRIZE_ARBITER` | no está en el kit | on | on/off | Dos jugadores ganando un mismo premio a la vez: se lo queda el de número de jugador más bajo y el otro devuelve el suyo |
| `LICHII_ORM_HEARTS` | `LICHII_ORM_HEARTS=1` | off (`coop_hook.cpp`) | on/off | La protección de la capacidad de corazones (la subida de `LICHII_COOP_HEARTS_TO_LINK` del upstream se queda como está) |

### Reloj, Epona, pausa

| Variable | Valor del kit | Valor del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_CLOCK` (old `LICHII_COOP_CLOCK`) | comentada (se muestra `# ...=0`) | on (`coop_clock.cpp`) | on/off | Un solo reloj compartido en lugar de las reglas de tiempo del upstream |
| `LICHII_ORM_CLOCK_NUDGE` (old `..._CLOCK_NUDGE`) | comentada | on | on/off | Apagado: el reloj se fija, no se acerca poco a poco |
| `LICHII_ORM_CLOCK_TEST_OFF`, `LICHII_ORM_CLOCK_TEST_JUMP` (se leen los nombres antiguos) | comentada | ninguna | `N@D`: N segundos, D segundos después del inicio | Solo pruebas: mueve nuestro reloj sin avisar, o lo salta como lo haría la Canción del Sol |
| `LICHII_ORM_CLOCK_TEST_PUT_BACK` | no está en el kit | off | on/off | Solo pruebas: deshace cada acercamiento, como una partida que lleva su propia cuenta |
| `LICHII_ORM_EPONA` (old `LICHII_COOP_EPONA`) | comentada (se muestra `# ...=0`) | on (`coop_epona.cpp`) | on/off | Epona es del grupo; la carrera espera |
| `LICHII_ORM_MENUS` | no está en el kit | on (`coop_menus.cpp`) | on/off | Nuestra pausa y pantalla de título salen en `PLAYER_STATUS` |
| `LICHII_ORM_PAUSE_DETECT` | no está en el kit | primero la bandera | `flag` (solo la bandera), `either` (la bandera o un fotograma congelado hasta que se vea la bandera); cualquier otro: primero la bandera | Cómo se detecta nuestra propia pausa |

## Cómo funciona

La mayoría de las partes son una regla pura en `coop/host` (sin memoria, sin sockets, con pruebas unitarias) y un archivo de enlace en `lichii/hook` que lee el juego y lleva a cabo lo que la regla pide. Nada de lo que sigue cambia el protocolo de red salvo `CHEST_OPEN`, el componente de equipo `rupees` y el bit de estado `at_title`.

### Niveles de agua

| Archivo | Qué hace |
|---|---|
| `coop/host/water.hpp`, `water.cpp` | La tabla de masas de agua (escena, bandera de nivel, Int o Bool); `merge_water` añade las banderas de nivel a la lista compartida y quita los Bool de un solo valor activo (one-hot) del Templo del Agua; `WaterGuard` retiene un nivel de otro jugador mientras nuestro Link, en la escena de esa agua, está en un evento, nada, bucea, cae, salta o trepa, lo escribe cuando está de pie o pasado el límite, y deriva los Bool one-hot a partir del Int de nivel en cada partida (nunca se envían: si dos jugadores cambiaran el nivel a la vez, podrían quedar el Int de uno y los Bool del otro). También observa si un nivel escrito movió los planos de agua cargados ("live") o se aplica en la siguiente carga |
| `lichii/hook/coop_hook.cpp` | Inicio de sesión: lee los interruptores y fusiona las banderas; cada fotograma: `received`, `derive` y las notas de la protección al registro |

### Interruptores de suelo mantenidos

| Archivo | Qué hace |
|---|---|
| `coop/host/held_switch.hpp`, `held_switch.cpp` | `HeldSwitchList` (los interruptores: hash de colocación, lugar, actor, área; integrada más un archivo de lista), `HeldSwitchRule` (quién está sobre qué, a partir de las posiciones de los demás tal como las tiene la presencia; sin mensaje nuevo), `HeldFlagApply` (la propia bandera "Link encima" del interruptor, las líneas `held:` de `shared_dungeon_adult.txt`, escrita a 1 mientras otros lo mantienen), `HeldWordApply` (palabras de actor, solo en modo `press`) |
| `lichii/hook/coop_held_switch.cpp`, `.h`, `coop_held_switch_game.cpp` | Enlace: los actores interruptor desde el recorrido de objetos, el modo, la sonda |

Un interruptor mantenido solo está activo mientras algo está encima, y el juego lo guarda en el actor, no en una bandera. Las marionetas no tienen física, así que sin esto nada pisa el interruptor de B mientras el Link de A pisa el de A.

### Cofres y puertas

| Archivo | Qué hace |
|---|---|
| `coop/host/chest_open.hpp`, `chest_open.cpp`, `chest_list.inc` | La tabla de cofres (cada colocación de cofre en el romfs, qué da, las palabras del aviso, la bandera compartida de cada hallazgo, el modelo sustituto sobre la cabeza de la marioneta); `ChestLids` (lo nuestro: cuando nuestro Link mueve un cofre y el cofre de al lado pasa a un comando de apertura, se avisa una vez como `CHEST_OPEN`; lo de ellos: reproduce el mismo comando en nuestro cofre de esa colocación, con su fotograma continuado según el tiempo transcurrido); `ChestFinds` (quién encontró qué, el sustituto mientras su marioneta lo sostiene en alto, el aviso una sola vez) |
| `lichii/hook/coop_chest_open.cpp`, `.h`, `coop_chest_open_game.cpp` | Enlace: envía y recibe `CHEST_OPEN`, reproduce la tapa, muestra el hallazgo |
| `coop/host/doors.hpp`, `doors.cpp` | La regla del espejo de puertas: cuando el comando AS de un jugador remoto (ya en `PLAYER_SAMPLE`) pasa a ser un comando de puerta o de cofre del AS de Link, la puerta o el cofre más cercano del tipo correcto junto a su marioneta reproduce su propio comando de apertura y luego el de cierre. Nunca en uno que esté junto a nuestro propio Link, ni en uno que ya se vea abierto |
| `lichii/hook/coop_doors.cpp`, `.h`, `coop_doors_game.cpp` | Enlace: lee los actores puerta y hace las llamadas; con `CHEST_OPEN` activado, los cofres se dejan a él |
| `coop/host/passages.hpp`, `passages.cpp` | Un jugador que sale de nuestro lugar se dibuja caminando hacia la salida, luego se aparca fuera de la vista y después se suelta; uno que llega se mantiene fuera de la vista hasta que se ha alejado del lugar donde estaba en otra parte (sin deslizarse por la sala). Lo llaman los planificadores de marionetas y de hadas |

Lo que da un hallazgo sigue viajando como las banderas de objeto compartidas y la bandera de abierto del cofre, como hace el upstream.

### Charlas

| Archivo | Qué hace |
|---|---|
| `coop/host/npc_talk.hpp`, `npc_talk.cpp` | `NpcTalk` encuentra al habitante con quien habla nuestro jugador (a partir de "se ejecuta un evento", el lugar de nuestro Link y los actores compartidos; los grandes o lejanos, como Biggoron o el Rey Zora, mediante su componente de evento o un alcance por nombre). Entonces el hook lo reclama (`OBJECT_PRESENT` lo marca como dirigido a nosotros, de modo que el servidor nos lo entrega) y deja de escribir la postura del antiguo dueño en nuestra copia |
| `coop/host/flag_defer.hpp`, `flag_defer.cpp` | Reglas de las banderas de charla: una bandera de charla de otra partida espera mientras su habitante está a la vista aquí, y se escribe cuando él está fuera de la vista, lejos o ausente (sin habitantes que salten delante de ti). Reglas desde texto: nombres, hashes, opciones |
| `lichii/hook/coop_flags.cpp` | Carga `LICHII_COOP_TALK` (la lista de charlas: banderas y sus reglas) |
| `tools/release/kit/shared_talk.txt` | Las banderas de charla que son del mundo |
| `coop/host/flag_burst.hpp`, `flag_burst.cpp` | Ancho de banda: una carga de escena fija decenas de banderas en unos pocos fotogramas; el primer cambio sale de inmediato y el resto de una ventana de 100 ms, junto |

### Canciones de teletransporte

| Archivo | Qué hace |
|---|---|
| `coop/host/songs.hpp`, `songs.cpp` | Las trece canciones como datos (el objeto que indica que una se conoce, el evento que la enseña, dónde aterriza una canción de teletransporte), lo que cada canción cambia para siempre (la lista de banderas `shared_dungeon_songs.txt`, comprobada contra ella en las pruebas) y la invitación: a un compañero que sale de nuestra escena con una canción de teletransporte se le puede seguir durante 10 s manteniendo un botón |
| `lichii/hook/coop_songs_game.cpp`, `.h` | Enlace: ve a un compañero aterrizar en una plataforma de teletransporte, ofrece el viaje, lee el botón mantenido, inicia el viaje; comprueba que el botón no mueva nada de nuestro Link (`comunicacion-aqui` lo comprueba), con un botón de reserva |
| `coop/host/live_check.hpp`, `live_check.cpp` | Solo lectura: tras cambiar una bandera vigilada, cuenta los actores vigilados para decir si la bandera los movió de inmediato ("live"), solo en la siguiente carga, o que falta una clave ("unexplained") |

Una canción de teletransporte solo lleva a quien la toca (la regla de nuestro cooperativo de N64); el viaje del seguidor empieza durante un evento, ya que el juego solo admite un cambio de escena mientras un evento se ejecuta.

### Listas del kit

`coop/host/dungeon_lists.hpp`, `dungeon_lists.cpp` leen archivos de texto plano que se aplican sobre las listas integradas del upstream (`coop_dungeon.cpp`, `coop_chests.cpp`). Una línea es una bandera: `Name` (compartida, en vivo), `key64:0x...` (una clave de una sola vez), `once:Name` (un 1 nunca lo deshace el 0 de otra partida ni una carga), `hold:Name Place` (retenida mientras nuestro Link está en Place, de modo que un artilugio ya jugado no llegue a una partida cuya área está cargada), `unshare:Name` (quitada de una lista integrada: dice dónde está un jugador, no lo que hizo el grupo), `toggle64:0x... Place` (una clave que va en ambos sentidos, retenida mientras estamos en Place), `held:Name Dungeon` (la bandera de un interruptor mantenido, véase arriba), `enum:` (una bandera Enum con sus valores en orden). Una bandera tiene un solo tipo en todos los archivos; una bandera bajo dos tipos es un error.

| Archivo | Qué comparte |
|---|---|
| `shared_dungeon_child.txt` | Los bloqueos de niño: Mido y los bloqueadores Kokiri, la puerta de la Pradera, rocas con bombas, muros y tapas de grutas del mundo exterior de niño, las antorchas de la Ciudad Goron y la puerta de Darunia, la cascada del Río Zora; con los sensores de mazmorra de un solo jugador quitados |
| `shared_dungeon_adult.txt` | El mundo exterior de adulto (carpinteras Gerudo, puerta del desierto, recompensas de la Casa Skulltula, Puerta del Tiempo), los sensores de un solo jugador de las mazmorras de adulto quitados, y las líneas `held:` de los interruptores mantenidos |
| `shared_dungeon_minigames.txt` | Cada jugador juega un minijuego en su propia partida; un premio dado una sola vez (pieza de corazón, bolsa, carcaj, escama, botella) se da una sola vez para el grupo; el paso de trueque de Biggoron va con el objeto de trueque |
| `shared_dungeon_songs.txt` | Lo que las canciones cambian para siempre: las ranas, los bloques de la Canción del Tiempo que desaparecen para siempre, Pierre |
| `shared_dungeon_temples.txt` | Templos del Fuego, del Bosque y del Agua: un Goron del Templo del Fuego liberado por un jugador desaparece de su celda para todos |
| `shared_dungeon_montana.txt` | Montaña de la Muerte, Ciudad Goron, Caverna de Hielo, Templo de las Sombras, Fondo del Pozo: rocas de adulto, la gran vasija, los Gorons rodantes, Link el Goron detenido una vez |
| `shared_dungeon_towns.txt` | La gruta de los ReDead de Kakariko, las Poes y la tumba real del Cementerio, la carta de Ruto, el agua del lago retenida mientras estamos en el Campo de Hyrule, el hielo del Rey Zora retenido mientras estamos en el Dominio, la puerta del castillo |
| `shared_dungeon_collectibles.txt` | Una pieza de corazón suelta que coge un jugador desaparece para todos; una judía que planta un niño queda plantada para todos; las dos mejoras del mercader de los Bosques Perdidos se venden una sola vez |
| `shared_dungeon_trades.txt` | Una botella entregada una vez se entrega una vez para el grupo; el contenido de las botellas sigue siendo de cada jugador; las cadenas de la Tienda de Máscaras y de Biggoron son una para el grupo; el Hongo Extraño, la Rana Ojo y las Gotas para los Ojos se quedan con quien los consiguió (su temporizador corre en su partida) |
| `shared_items.txt` | Reglas de objetos (abajo), leídas por `LICHII_ORM_ITEM_RULES` |
| `shared_talk.txt` | Banderas de charla (arriba), leídas por `LICHII_COOP_TALK` |
| `shared_counts.txt` | Ints contados para el final del juego, leídos por `LICHII_ORM_COUNTS`: véase [05-bosses](05-bosses.es.md) |
| `shared_story.txt` | Escenas de la historia: véase [07-cutscenes](07-cutscenes.es.md) |
| `shared_time_temple.txt` | Reglas del Templo del Tiempo: véase [08-age](08-age.es.md) |

Las listas las escribieron nuestros scripts a partir del romfs (los nombres se comprueban contra `GameData/GameDataList`). `make_kit.ps1` copia cada `shared_*.txt` por patrón.

### Objetos, cartera, corazones

| Archivo | Qué hace |
|---|---|
| `coop/host/item_rules.hpp`, `item_rules.cpp` | Cambios a la tabla de objetos del upstream desde texto: `RULE FLAG` (max, any, mirror, count o player), `cap COUNT FLAG` (una ganancia se detiene en otro Int, una cartera llena), `fill FROM TO` (un máximo compartido subido por otro jugador llena un Int por jugador: el medidor de magia) |
| `coop/host/src/game_items.cpp` (archivo del upstream) | Aplica las reglas, los topes y los llenados |
| `coop/host/wallets.hpp`, `wallets.cpp` | La cartera de cada jugador sigue siendo suya (`PlayerStatus.CurrentRupee` por jugador); la cuenta propia de una partida sale en el componente `rupees` de `PLAYER_EQUIP`; los receptores solo la muestran en el HUD, nunca la escriben en su partida. La lista del kit trae la cartera común como dos líneas comentadas para quien quiera recuperarla |
| `coop/host/trades.hpp`, `trades.cpp` | La protección del paso de trueque (`LICHII_ORM_TRADE_GUARD`) |
| `coop/host/beans.hpp`, `beans.cpp` | La comprobación del terreno de judías (`LICHII_ORM_BEAN_CHECK`) |
| `coop/host/prizes.hpp`, `prizes.cpp` | El árbitro de doble victoria (`LICHII_ORM_PRIZE_ARBITER`); el vigilante conserva para él el valor igual de otra partida (`FlagWatcher::take_agreed`) |
| `coop/host/hearts.hpp`, `hearts.cpp` | El juego dibuja los corazones a partir del componente Life de Link, no de la bandera `MaxLife` (una copia escrita de vuelta en menos de un cuarto de segundo). Esto conserva el más alto entre el máximo de nuestro Link, la bandera y todo valor enviado por otro jugador, y sube el componente de Link hasta él |

### Reloj

| Archivo | Qué hace |
|---|---|
| `coop/host/clock.hpp`, `clock.cpp` | El anfitrión es dueño del reloj compartido (o el compañero de número más bajo al aire libre mientras el reloj del anfitrión está parado); lo envía en el `TIME_OF_DAY` del upstream una vez por segundo aproximadamente, y cada otra partida informa del suyo. Los seguidores acercan su reloj unos pocos segundos de juego por tic (como mucho una desviación fija de su ritmo); el fijador del juego se usa solo al unirse, al llegar al aire libre tras estar parado, como último recurso, y para un salto (Canción del Sol, dormir una noche, una escena que fija la hora) |
| `lichii/hook/coop_clock.cpp`, `.h` | Enlace: los acercamientos a través de la cueva svc, los interruptores de prueba |
| `lichii/hook/coop_time.cpp` (archivo del upstream) | Usa nuestro reloj en lugar de las reglas del upstream cuando `CoopClock::On()` |

La forma del upstream fijaba el reloj de los demás con el fijador del juego, que ejecuta el cambio de hora del juego (la hierba y los objetos del área se rehacen, el reloj se detiene unos segundos; incidencia 159 del upstream).

### Epona

| Archivo | Qué hace |
|---|---|
| `coop/host/epona.hpp`, `epona.cpp` | `Step_Get_Epona` compartido solo hacia delante y solo su último valor (Complete); una partida cuya propia carrera o escape está en marcha retiene un Complete llegado de otra hasta que termine; todos los caballos, Ingo y el tiro con arco a caballo siguen siendo de cada partida |
| `lichii/hook/coop_epona.cpp`, `.h` | Enlace (el dibujo del caballo del jinete forma parte de [11-puppets](11-puppets.es.md)) |

### Pausa y pantalla de título

| Archivo | Qué hace |
|---|---|
| `coop/host/menus.hpp`, `menus.cpp` | A partir de `Sequence_IsPause` (puesta por la pausa del juego, el mapa, la rueda de objetos y la pantalla de guardado) y de si hay un Link y una escena: nuestro `PLAYER_STATUS` (en pausa, o en la pantalla de título); para los estados de los demás, la etiqueta "(pausa)" y los avisos del chat |
| `coop/host/pause_detect.hpp`, `pause_detect.cpp` | Un segundo detector para nuestra propia pausa: un fotograma congelado, usado solo mientras la bandera no se puede leer (una ejecución del juego mostró que la bandera se pone 0,5 s después de abrirse el menú, y que un Link inactivo parece congelado) |
| `lichii/hook/coop_menus.cpp`, `.h` | Enlace |
| `server/session.cpp`, `server/enemies.cpp` | Un dueño en pausa cede su enemigo a un jugador que esté quieto tras 1 s en un combate |

### Puntos de enganche en archivos del upstream

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_hook.cpp` | Inicio de sesión: listas de mazmorras, agua, lista de charlas y reglas de espera, ráfaga de banderas, el paso de Epona, la línea de registro de las tres protecciones; cada fotograma: agua, interruptores mantenidos (después de `CoopObjects::Share`), puertas, tapas de cofres, bloques de canciones, menús, Epona, protecciones de trueque/judías/premios, corazones; hilo de red: `CoopChestOpen::OnEvent`, `CoopEpona::OnEvent` |
| `lichii/hook/coop_flags.cpp` | Carga `LICHII_ORM_ITEM_RULES` y `LICHII_COOP_TALK` |
| `lichii/hook/coop_objects.cpp`, `coop_objects_sync.cpp` | El reclamo de charla, la lista de actores que leen los interruptores mantenidos y las puertas, las tapas de cofres que se mantienen abiertas cuando llega la bandera de abierto |
| `lichii/hook/coop_time.cpp` | El reloj compartido en lugar de las reglas de tiempo del upstream |
| `lichii/hook/coop_world.cpp` | `Stepswitch_` (con s minúscula) en la sonda de artilugios del upstream, que nunca coincidía con los interruptores mantenidos |
| `coop/host/src/flag_watcher.cpp`, `flags.cpp`, `game_flags.cpp`, `game_items.cpp` | Tipos de líneas de lista (`once`, `hold`, `toggle64`, `held`, `enum`), escrituras rechazadas que se reintentan en cada fotograma (las protecciones), valores acordados para el árbitro de premios, topes y llenados de objetos |

## Mensajes y componentes

| Nombre | Número | Uso |
|---|---|---|
| `CHEST_OPEN` | type `0xF009` (`orm_types.hpp`), channel 0 | Número de jugador, hash de colocación del cofre, escena del emisor, la animación del cofre (comando, fotograma, ritmo), hora del servidor. Se retransmite a todos los demás, no se guarda nada |
| `PLAYER_EQUIP` component `rupees` | 29 (`equip_component::rupees`) | La cartera propia de un jugador, u32 |
| `PLAYER_STATUS` bit `at_title` | bit 7 (`orm_status::at_title`) | Pantalla de título o selección de archivo; se envía con el bit `loading` del upstream |
| `TIME_OF_DAY` (upstream) | el tipo del upstream, con sus bits de bandera | El reloj compartido y los saltos |
| `OBJECT_PRESENT` (upstream) | el del upstream | El reclamo de charla |

Todo lo demás de esta función viaja como banderas compartidas normales.

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/host/water_test.cpp` (24 cases) | Hashes de la tabla; la lista compartida recibe los niveles y pierde las banderas one-hot; las esperas por nadar, evento, caída y trepada; la espera máxima y 0; Jabu solo cuando se pide; banderas one-hot derivadas; líneas live o de siguiente carga; un jugador que se une tarde; las botas de hierro en el fondo cuentan como estar de pie; un mutante sin la regla |
| `tests/host/held_switch_test.cpp` (21), `tests/lichii/held_switch_test.cpp` (3) | La lista integrada y cada bandera `held:` de la lista de adulto; el área girada; dos y tres jugadores; nuestro Link también encima; desconexiones; otra escena; recargas; la bandera escrita y soltada; las palabras; el enlace que lee la presencia |
| `tests/host/chest_open_test.cpp` (18), `tests/lichii/chest_open_test.cpp` (7) | Cada cofre una vez con lo que da, palabras y sustituto para cada hallazgo; la tapa abierta una vez en el fotograma del emisor |
| `tests/host/doors_test.cpp` (10), `tests/lichii/doors_test.cpp` (11), `tests/host/passages_test.cpp` (6) | Comandos de puerta y de cofre desde el AS de Link y el AS de los actores puerta; el empujón del espejo y luego el cierre; sin puerta, un actor desaparecido, un cambio de escena; salir y entrar caminando |
| `tests/host/npc_talk_test.cpp` (27), `tests/host/flag_defer_test.cpp` (17), `tests/host/flag_burst_test.cpp` (5) | Quién cuenta como habitante, el más cercano al alcance, los grandes y los lejanos; reglas de espera desde texto y las reglas Kokiri integradas; ráfagas |
| `tests/host/songs_test.cpp` (18), `tests/host/live_check_test.cpp` (8) | Las trece canciones, efectos y reglas, la lista de canciones del kit dice lo mismo, la invitación; live, no live, unexplained |
| `tests/host/dungeon_lists_test.cpp` (13), `tests/host/kit_lists_test.cpp` (5), `tests/lichii/adult_list_test.cpp`, `collectible_list_test.cpp`, `minigame_list_test.cpp`, `montana_list_test.cpp`, `temples_list_test.cpp`, `town_list_test.cpp`, `trade_list_test.cpp` | Cada tipo de línea; un solo tipo por bandera; cada lista del kit tiene lector y se interpreta; ninguna bandera con dos reglas entre listas; el contenido de cada lista |
| `tests/host/item_rules_test.cpp` (9), `tests/orm/wallet_test.cpp` (11), `tests/host/trades_test.cpp` (5), `tests/host/beans_test.cpp` (4), `tests/host/prizes_test.cpp` (7), `tests/host/hearts_test.cpp` (12) | Reglas, topes, llenados; la lista del kit deja cada cartera como propia, tres jugadores a la vez; la protección del paso de trueque; los casos del terreno; dobles victorias; la capacidad de corazones mantenida en un solo sentido |
| `tests/host/clock_test.cpp` (18), `tests/host/epona_test.cpp` (9) | Relojes de anfitrión e invitado, acercamientos, saltos, medianoche; solo se comparte Complete, la espera de la carrera |
| `tests/host/menus_test.cpp` (14), `tests/server/menus_test.cpp` (7), `tests/host/pause_detect_test.cpp` (7) | Bits de estado por estado; un dueño en pausa cede su enemigo tras 1 s; los modos del detector |

Escenarios del arnés (viven en `tools/orm/harness`): `meca-agua-templo` (A toca los tres eventos de la trifuerza; B recibe el nivel, deriva los Bool one-hot, y la sonda dice si los planos de agua de B se movieron), los demás `meca-agua-*`; `meca-sostenido-fuego`, `-espiritu`, `-agua`, `-dodongo`, `-jabu`, `-deku`, `-bosque` (A está sobre el interruptor 15 s, B mira la puerta desde 2,5 m); `efectos-cofre` (A abre el cofre de la resortera del Gran Árbol Deku, B ve la tapa, la resortera sobre la marioneta de A y el aviso); `puertas-*`; `charla-dos` (dos jugadores hablan con el mismo Kokiri), `charla-mido` (la bandera del bloqueador de Mido llega a B una vez que Mido es copia de A), `charla-saria` y los demás `charla-*`; `viaje-cancion` (B toca el Minueto en Kakariko, A mantiene L y aterriza a su lado), `comunicacion-aqui` (L queda libre mientras se juega); `cartera-cada-uno` (A y B fijan sus propias carteras, cada uno ve ambas); `corazon` (un contenedor de corazón sube ambos Link); `hora-salto`, `hora-sigue`, `hora-sin-empujon`; `epona-*`; `menu-pausa` (la marioneta de A se queda quieta, no oculta, mientras el menú de A está abierto; un cambio de escudo aún se ve), `menu-salir`; `pueblo-*`, `templo-*`, `montana-*`, `trueque-*`, `botella-*` para las listas.

## Líneas de registro

| Línea | Qué dice |
|---|---|
| `coop: water: on: N level flags added, N taken off (the one-hot flags are derived here); a level waits while our Link swims or is off his feet, at most 20 s` | Agua activada |
| `coop: water: <flag> (<what>) waited N s: written, ...`, `coop: water: <flag>: written in <scene> after the wait, live (...)` / `...; its mover reads it at load ...` | La espera y si se movió en vivo |
| `coop: held switch: on: switches others stand on are pressed here with their flag (...)` / `on: switches others stand on are pressed here` / `watching only (...)` | Modo de interruptor mantenido |
| `coop: held switch: <name> held by another player (P2): pressed here` / `would be pressed here (watching only)`; `... no other player on it: let go here` | Cada pulsación y cada suelta |
| `coop: held switch: flag 0x........ = 1 ...` | Escribió la vía de la bandera |
| `coop: chests: a chest's lid opens on every screen (CHEST_OPEN) ...`, `coop: chests: CHEST_OPEN from P2: ..., '<command>' at frame N` | Tapas de cofres activadas, una tapa recibida |
| `coop: doors and chests ...` | Espejo de puertas |
| `coop: talk: sharing N talk flags (N new)`, `coop: flags: talk flags wait while seen, N rules` | Lista de charlas y reglas de espera |
| `coop: npc: talking to <name> 0x... (...), found <how>` / `... talks with its owner P2: not claimed, our talk ...` | El reclamo de charla |
| `coop: songs: warp-song invitation on (LICHII_COOP_WARP_INVITE=1): hold L to follow a mate who leaves by a warp song` / `off (...)` | Invitación activada o desactivada |
| `coop: songs: our Link arrives by a warp song in <scene> at x y z (event ...)` | Un viaje aterrizó |
| `coop: lists: LICHII_COOP_DUNGEON_LISTS <files>: <why>; the lists are not used` | Falló un archivo de lista |
| `coop: items: rules from <file>: N changed, N as built, N caps, N fills` / `coop: items: LICHII_ORM_ITEM_RULES ...; the built-in item rules are used` | Reglas de objetos |
| `coop: wallet: P1 'Ana' (you) has 87 rupees` | Cada cartera que conoce una partida |
| `coop: riesgos: trade step guard on, bean check on, prize arbiter on` | Las tres protecciones |
| `coop: hearts: the group's capacity kept one way up (LICHII_ORM_HEARTS), before the object walk's raise` | Protección de corazones activada |
| `coop: clock: ours ... shared ... off N s, ...` y las líneas de prueba `coop: clock: test: ...` | Estado del reloj |
| `coop: epona: Step_Get_Epona = Complete shared forward (the group's Epona): ...` / `... not shared (LICHII_ORM_EPONA=0): each game wins its own`; `coop: epona: our race with Ingo runs: the group's Epona waits until it is over` | Epona |
| `coop: menus: on (LICHII_ORM_MENUS): our pause and title screen are sent, a paused mate stands frozen with '(pausa)' on the tag`; `pause detector: ...` | Pausa |

## Estado

| Parte | Estado |
|---|---|
| Niveles de agua | **confirmado en partida en nuestra compilación basada en test9 (escenario `meca-agua-templo`)** |
| Interruptores de suelo mantenidos, vía de la bandera | **confirmado en partida en nuestra compilación basada en test9 (escenarios `meca-sostenido-fuego`: la bandera sola abre el muro de fuego del Templo del Fuego; `meca-sostenido-espiritu`)** |
| Interruptores mantenidos, palabras `press` | todavía no confirmado en partida |
| Tapa y hallazgo del cofre | **confirmado en partida en nuestra compilación basada en test9 (escenario `efectos-cofre`)** |
| Espejo de puertas, pasajes | todavía no confirmado en partida |
| Banderas de charla y reclamo de charla | **confirmado en partida en nuestra compilación basada en test9 (escenarios `charla-dos`, `charla-mido`, `charla-saria`)** |
| Invitación de las canciones de teletransporte | **confirmado en partida en nuestra compilación basada en test9 (escenarios `viaje-cancion`, `comunicacion-aqui`)** |
| Listas del kit (child, adult, minigames, songs, temples, montana, towns, collectibles, trades) | todavía no confirmado en partida como listas (con pruebas unitarias; las líneas `held:` se ejecutaron con los interruptores mantenidos) |
| Reglas de objetos, llenado de magia | todavía no confirmado en partida |
| Cartera por jugador | **confirmado en partida en nuestra compilación basada en test9 (escenario `cartera-cada-uno`)** |
| Protección de corazones | **confirmado en partida en nuestra compilación basada en test9 (escenario `corazon`)** |
| Protección de trueque, comprobación de judías, árbitro de premios | todavía no confirmado en partida |
| Reloj compartido | todavía no confirmado en partida |
| Epona del grupo | todavía no confirmado en partida |
| Estado de pausa | **confirmado en partida en nuestra compilación basada en test9 (escenario `menu-pausa`)** |

## Carencias conocidas

- Los interruptores mantenidos que necesitan el estado propio del actor (sala 08 del Templo del Espíritu: la bandera no mueve sus barras) necesitan las palabras halladas con la sonda; se vieron palabras candidatas pero no están programadas (escribir un contacto sin su puntero podría bloquear el juego).
- El interruptor de la sala 14 del Templo del Agua se pulsa con las botas de hierro; la otra partida no sabe qué botas lleva un compañero. La piedra de la sala 17 de Jabu-Jabu sigue fuera.
- Si cada bandera de lista mueve su actor en vivo o solo en la siguiente carga es cosa de cada bandera; `LICHII_ORM_LIVE_CHECK` y las líneas "live" del agua lo dicen en una ejecución.
- Los bloques de la Canción del Tiempo: la sonda `meca-cancion-tiempo` todavía no movió el bloque.
- Significado de las judías: el Bool del terreno leído como "plantada" es una deducción, comprobada por la comprobación de judías en una ejecución.
- Reloj: el reloj del anfitrión solo corre al aire libre; si nadie está al aire libre, se detiene.
