[English](11-puppets.md) | **Español**

# 11-puppets: Pulido de las marionetas

La marioneta (docs/puppet.md) es cómo se dibuja en tu partida el Link de otro jugador. Esta función es un conjunto de capas independientes sobre ella, cada una con su propio interruptor, para que veas a los demás hacer de todo: movimiento más suave, efectos y estados, lo que llevan, máscaras y cadenas, la armadura que de verdad visten, su Ocarina, su caballo, el suelo móvil sobre el que están, el aldeano con el que hablan y su color. Todas las capas son solo presentación: nada del estado de tu propia partida cambia.

## Qué cambia para los jugadores

| Parte | Qué ves |
|---|---|
| Suavizado | Otro Link se mueve con suavidad: sin tirones por la inestabilidad de la red, sin "caminar mientras corre" ni deslizarse tras parar. Las copias de enemigos que no controlas se mueven igual |
| Efectos y estados | Los hechizos, la natación, las volteretas y los aterrizajes de otro jugador reproducen sus efectos en su marioneta; su marioneta parpadea cuando recibe daño; el fuego, el hielo, la descarga y el cristal de Nayru se ven sobre él; el objeto que levanta de un cofre se ve en sus manos |
| Objetos cargados | Una vasija, piedra, caja, arbusto, Cucco, flor bomba o la pequeña Ruto que otro jugador levanta está en las manos de su marioneta, y se ve su lanzamiento |
| Máscaras y cadenas | Su máscara o la Lente de la Verdad en la cara de su marioneta; la cadena de su gancho y de su gancho largo y su sedal de pesca dibujados desde sus manos |
| Armadura puesta | Su marioneta lleva sus botas, guanteletes y máscara, nunca los tuyos |
| Ocarina | Mientras toca, su propia Ocarina está en la mano de su marioneta; oyes sus notas en su marioneta y ves el nombre de sus canciones |
| Caballos | Un jugador que sube a un caballo, lo monta y baja se ve sobre un caballo con su propio aspecto (caballo del rancho, el de Ingo), no oculto |
| Suelos móviles | Un jugador en un ascensor, un suelo que se mueve o flota o la barca del Templo de las Sombras se coloca sobre tu copia de ese suelo en vez de flotar encima o hundirse a través |
| Aldeanos | El aldeano con el que habla gira la cabeza y la cara también en tu pantalla; opcionalmente un aldeano mira a su marioneta como miraría a tu Link |
| Colores | Su etiqueta de nombre y su línea del panel en el color que eligió; los jugadores 5 y 6 reciben morado y naranja por defecto |

## Interruptores

Los interruptores de upstream que esta función solo lee (`LICHII_COOP_PUPPET`, `LICHII_COOP_SPAWN`, `LICHII_COOP_OBJECTS`, `LICHII_COOP_TUNICS`) no se listan. Todas las capas necesitan `LICHII_COOP_PUPPET=1`, y la mayoría necesita un paquete de marionetas construido con este kit.

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `LICHII_ORM_SMOOTH` (antes `LICHII_COOP_SMOOTH`) | `LICHII_ORM_SMOOTH=1` | activado (`coop_puppet_config.cpp`) | palabras sí/no | Suavizado de marionetas. Desactivado: se coloca desde la presencia por orden de llegada, como antes |
| `LICHII_COOP_COPY_SMOOTH` | no está en el kit | activado (`coop_objects.cpp`) | palabras sí/no | El mismo suavizador para las copias de enemigos que no controlamos. Desactivado: `Sync::PlaceAt` como antes |
| `LICHII_ORM_EFFECTS` | no está en el kit | activado (`coop_effects.cpp`) | palabras sí/no | Comandos AS permitidos que se reproducen bajo la pose copiada, solo por sus eventos de efecto y sonido |
| `LICHII_ORM_DAMAGE_FLASH` | no está en el kit | activado | palabras sí/no | El parpadeo por daño |
| `LICHII_ORM_STATUS_FX` | no está en el kit | activado | palabras sí/no | Los sustitutos de fuego, hielo, descarga y cristal de Nayru |
| `LICHII_ORM_NAYRU_S` | no está en el kit | el `NayruLoveTime` del juego escrito por el paquete de marionetas; si no, 60 | 1 a 600 (segundos) | Cuánto tiempo permanece el cristal de Nayru en una marioneta |
| `LICHII_ORM_EFFECTS_LIST` | no está en el kit | reglas incorporadas | una ruta de archivo | Reglas propias en lugar de la lista incorporada: líneas `play`, `skip`, `flash`, `noflash`, `status burn|freeze|shock`, `nayru`, `rest`; gana la primera coincidencia; `*` es el único comodín |
| `LICHII_COOP_CARRY` | no está en el kit | activado (`coop_objects.cpp`) | palabras sí/no | Los objetos cargados se reclaman y se dibujan en las manos de quien los lleva |
| `LICHII_COOP_CARRY_BONE` | no está en el kit | activado | palabras sí/no | Desactivado: la copia cargada sigue solo el marco de actor de la marioneta, no el hueso de su mano |
| `LICHII_COOP_CARRY_FLOWERS` | no está en el kit | activado | palabras sí/no | Desactivado: las flores bomba no se comparten |
| `LICHII_COOP_CARRY_STILL` | no está en el kit | activado | palabras sí/no | Desactivado: una copia cargada conserva su propia velocidad |
| `LICHII_ORM_MASK_FOLLOW` | no está en el kit | activado (`coop_puppet.cpp`) | palabras sí/no | Las máscaras y la Lente de la Verdad se colocan en la cabeza de la marioneta en cada fotograma |
| `LICHII_ORM_TETHERS` | no está en el kit | activado (`coop_spawn.cpp`) | palabras sí/no | La cadena del gancho y el sedal de pesca puestos en las copias (necesita `LICHII_COOP_SPAWN=1`) |
| `LICHII_COOP_OCARINA_SOUND` | `LICHII_COOP_OCARINA_SOUND=1` | desactivado (`orm_ocarina_sound.cpp`; vacío o `0` es desactivado, cualquier otro valor activado) | `1` / `0` | La Ocarina de los demás se oye en sus marionetas |
| `LICHII_ORM_OCARINA_NOTE` (antes `LICHII_COOP_OCARINA_NOTE`) | no está en el kit | igual que `LICHII_COOP_OCARINA_SOUND` (`coop_ocarina_note_game.cpp`) | palabras sí/no | Nuestras propias notas y canciones se envían a los demás |
| `LICHII_ORM_OCARINA_NOTES` | no está en el kit | desactivado (`orm_ocarina_sound.cpp`) | palabras sí/no | Desactivado: las notas viajan como el `OCARINA_NOTE` (140) de upstream y el nuestro lleva solo canciones. 1: el nuestro lleva notas y canciones, y el director de marionetas omite el de upstream |
| `LICHII_ORM_OCARINA_HAND` (antes `LICHII_COOP_OCARINA_HAND`) | `# LICHII_ORM_OCARINA_HAND=bow` (comentado) | `sub` (`coop_puppet_config.cpp`) | `sub` (mano izquierda), `bow` (mano derecha), una palabra de desactivado (no se dibuja en la mano) | En qué pieza de la mano se lleva la Ocarina mientras su jugador toca |
| `LICHII_ORM_HORSE` (antes `LICHII_COOP_HORSE`) | `LICHII_ORM_HORSE=1` | activado (`coop_epona.cpp`) | palabras sí/no | Un jinete dibujado sobre un caballo. Desactivado: el jinete desaparece mientras monta (su nombre se queda) |
| `LICHII_ORM_HORSE_ACTOR` (antes `LICHII_COOP_HORSE_ACTOR`) | comentado | según el caballo que monta | un nombre de actor del paquete de marionetas | Un solo actor de caballo para todos los jinetes |
| `LICHII_ORM_HORSE_SADDLE` (antes `LICHII_COOP_HORSE_SADDLE`) | comentado | `0,1.2,0` | `x,y,z` | Dónde se sienta el jinete en el caballo |
| `LICHII_ORM_DEBUG_HORSE` (antes `LICHII_COOP_DEBUG_HORSE`) | comentado | desactivado | palabras sí/no | Registra dónde se sienta tu propio Link en tu Epona (el valor para `_HORSE_SADDLE`) |
| `LICHII_ORM_RIDE` (antes `LICHII_COOP_RIDE`) | `# LICHII_ORM_RIDE=0` (comentado: activado) | activado (`orm_ride.cpp`) | palabras sí/no | Un compañero sobre un suelo móvil se dibuja en tu copia de ese suelo (necesita `LICHII_COOP_OBJECTS=1`) |
| `LICHII_COOP_NPC_POSE` | `LICHII_COOP_NPC_POSE=1` | desactivado (`coop_objects.cpp`, `coop_svc.cpp`) | palabras sí/no | La cabeza y la cara del aldeano con el que se habla se envían como `OBJECT_POSE`. Necesita `LICHII_COOP_NPC=1` (el reclamo de la charla, 06-world) |
| `LICHII_COOP_NPC_GAZE` | no está en el kit | desactivado (`coop_objects.cpp`) | palabras sí/no | Un aldeano gira la cabeza hacia la marioneta de un compañero |
| `color` (coop-options.ini) y `LICHII_COOP_COLOR` | `# color = blue` (comentado) | el color por jugador del lanzador desde `coop.ini`; si no, `slot_color` | un nombre de color o `#RRGGBB` | Tu color; los scripts del kit (`kit-paths.ps1`) exportan el color de `coop.ini` como `LICHII_COOP_COLOR`, de modo que el color por defecto propio de upstream ya no lo sobrescribe |

Los interruptores marcados como "palabras sí/no" los lee `coop_switch.h` (0/1/true/false/yes/no/on/off, mayúsculas o minúsculas; cualquier otra cosa conserva el valor por defecto y registra `coop: switch NAME=VALUE is not ...`).

## Cómo funciona

### Suavizado

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/pose_smooth.hpp`, `coop/host/src/pose_smooth.cpp` | Un búfer de interpolación: la marioneta se dibuja 100 ms por detrás del reloj estimado del servidor, entre las dos muestras que rodean ese instante (Hermite cúbico con las velocidades del emisor, acotado para que una parada no se pase de largo). Pasada la muestra más reciente extrapola con la velocidad del emisor durante 150 ms como máximo y luego se mantiene. Una muestra tardía o un flujo reanudado se suaviza durante 100 ms; un teletransporte o un cambio de escena salta directo. El reloj de dibujado nunca retrocede. La animación y el paso vienen del comando y la velocidad del emisor, nunca de la velocidad dibujada. `Timeline<T>` da a las poses del cuerpo la misma base de tiempo |
| `lichii/hook/coop_copy_smooth.h`, `lichii/hook/coop_copy_smooth.cpp` | El mismo suavizador para las copias de enemigos a partir de los `OBJECT_SAMPLE` de sus dueños, uno por copia, reiniciado cuando cambia el dueño |

### Efectos y estados

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_effects.h`, `lichii/hook/coop_effects.cpp`, `lichii/hook/coop_effects_game.cpp` | Con la copia de pose, una marioneta no reproduce ningún comando AS, así que sus eventos de animación nunca se disparaban. Una lista de permitidos (`kDefaultRules`, o `LICHII_ORM_EFFECTS_LIST`) reproduce comandos inofensivos (hechizos, natación, movimientos, volteretas, aterrizaje) bajo la pose copiada solo por sus eventos de efecto y sonido; los ataques, el daño, levantar, lanzar, los objetos, las escenas y el caballo nunca se reproducen. Un comando de daño hace parpadear a la marioneta (dibujada bajo su sitio cada 100 ms alternos durante 1,2 s). Un comando de fuego, hielo o descarga pone un sustituto (`CoopFx_Burn`, `_Freeze`, `_Shock`) en la marioneta; el hechizo de Nayru pone `CoopFx_Nayru` durante el tiempo del hechizo |
| `coop/host/include/coop/host/worn_armor.hpp`, `coop/host/src/worn_armor.cpp` | Qué pieza de armadura viste de verdad un jugador (la pieza de su equipo gana sobre la marca del menú), enviada en `PLAYER_EQUIP`; y qué piezas puede quitarse una marioneta |
| `lichii/hook/coop_detalles.h`, `lichii/hook/coop_detalles.cpp` | Una marioneta se equipa a partir de las marcas de tu partida cuando se crea; un tipo del que su jugador no lleva nada (botas, guanteletes, máscara) se quita con el propio quitado de armadura del juego, como mucho unas pocas veces por marioneta |
| `lichii/hook/coop_chest_open.*` (06-world) | El objeto del cofre levantado: un sustituto `CoopGet_<item>` en las manos de la marioneta de quien abre |

### Objetos cargados

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_carry.h`, `lichii/hook/coop_carry.cpp` | Lo nuestro: cuando el comando de nuestro Link inicia un levantamiento, se reclama el objeto compartido levantable más cercano frente a él (con la selección de `OBJECT_PRESENT`, como con un aldeano al que se habla), y mientras se sostiene sus muestras llevan un `ObjectCarry` (lugar en el marco de nuestro Link y en el marco del hueso más cercano). Lanzar, soltar o dejar caer lo termina; el reclamo dura un instante más para que el lanzamiento y la rotura sigan siendo de quien lanza. Lo de los demás: una copia cuyo dueño tiene una muestra con carga se coloca en cada fotograma en las manos de la marioneta de quien la sostiene (en su hueso cuando se lee como un marco rígido, si no en el marco del actor), con fundido; tras el lanzamiento vuelve a seguir las muestras |

### Máscaras y cadenas

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_mascaras.h`, `lichii/hook/coop_mascaras.cpp` | El modelo de una máscara se une a quien la lleva por el nombre del hueso, así que en una marioneta se dibujaba en otro sitio. En cada fotograma tras la copia de pose, la pieza de la cabeza (máscara o lente) recibe el lugar y el giro de la marioneta, y los huesos de la marioneta con el mismo nombre se copian sobre ella. La cadena del gancho (los huesos `MainLine` de `Item_Hookshot_Chain`) y un sedal de pesca procedural (`CoopShot_FishingLine`, el modelo de cuerda) se tienden desde la punta o el señuelo hasta la pieza de la mano de la marioneta de su dueño |
| `tests/lichii/skeleton_fakes.hpp` | Esqueletos y equipo falsos para esas pruebas |

### Ocarina

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_ocarina_note.h`, `lichii/hook/coop_ocarina_note.cpp`, `lichii/hook/coop_ocarina_note_game.cpp` | Lee el control de Ocarina de nuestra propia partida (estado, nota, instrumento, la canción reconocida) en el hilo de presentación, solo lee, y envía notas y canciones como nuestro `OCARINA_NOTE` (0xF007). Desde test10, por defecto solo canciones: upstream envía las notas por sí mismo (`OCARINA_NOTE` 140, desde los botones) |
| `lichii/hook/orm_ocarina_sound.h`, `lichii/hook/orm_ocarina_sound.cpp` | Reproduce las notas de otro jugador en su marioneta mediante el propio usuario de sonido "Ocarina" del juego (cada marioneta con su propio identificador), y registra sus canciones. También reproduce la nota de aviso "un compañero está caído" (04-downed). Se llama `orm_` porque test10 añadió un archivo de upstream con el nombre antiguo |
| `lichii/hook/coop_puppet.cpp` (`OcarinaInHand`, `OcarinaHandPiece`) | Mientras un jugador toca, su propia Ocarina (`Item_Ocarina_Fairy` o `_Time`) se lleva como arma secundaria (o arco) de su marioneta, y se guarda después |

### Caballos

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_epona.h`, `lichii/hook/coop_epona.cpp` | La marioneta de un jinete se dibuja donde dicen sus muestras, con su propia animación de subir, ir sentado y bajar (ya no se aparca fuera de la vista). Su partida encuentra el caballo bajo él y envía su nombre en `PLAYER_EQUIP` (`mount`); los demás eligen el caballo de marioneta para ese aspecto (`coop::host::epona::horse_look`). Un tercer director de marionetas (llamador svc 25) coloca el caballo bajo el jinete (el lugar menos el desplazamiento de la silla, girado según su rumbo) con un paso según su velocidad. Desde test10, cuando un jinete envía el `PLAYER_RIDE` de upstream sobre Epona, el `CoopRide` de upstream dibuja a Epona y el nuestro se aparta; el nuestro sigue dibujando los caballos del rancho, el de Ingo y los caballos de eventos. La propia Epona del grupo (un jugador la gana para todos, la retención de la carrera) está en el mismo archivo y se describe en 06-world |
| `coop/host/include/coop/host/epona.hpp`, `coop/host/src/epona.cpp` | Aspectos de caballo por nombre, el paso compartido de Epona y la lógica de retención de la carrera |

### Suelos móviles

| Archivo | Qué hace |
|---|---|
| `coop/host/include/coop/host/ride.hpp`, `coop/host/src/ride.cpp` | `RideDetector`: a partir del recorrido de actores de nuestra propia partida, si un suelo móvil (ascensores, suelos que suben, bajan o flotan, la barca del Templo de las Sombras; solo suelos que se trasladan) lleva a nuestro Link; su hash de colocación y nuestro lugar sobre él van en `PLAYER_SAMPLE`. `RideFollower`: un receptor con el mismo suelo cargado dibuja la marioneta sobre su propio suelo en ese lugar, solo dentro del alcance de las muestras, con fundido de entrada y salida |
| `lichii/hook/orm_ride.h`, `lichii/hook/orm_ride.cpp` | El pegamento con el juego (se llama `orm_` junto al `coop_ride` propio de upstream). También lo usan las familias de jefes (05-bosses): un Link atrapado por una parte agarradora viaja sobre ella del mismo modo |

### Aldeanos

| Archivo | Qué hace |
|---|---|
| `lichii/hook/coop_npc_pose.h`, `lichii/hook/coop_npc_pose.cpp` | La partida del dueño lee el esqueleto del aldeano con el que se habla (cabeza girada hacia quien habla, cara) y lo envía como `OBJECT_POSE`, como máximo 20 por segundo, solo los huesos que cambian, todos los huesos cada 500 ms, en partes para aldeanos con muchos huesos. Los demás lo escriben en su copia hueso a hueso, mezclando las dos más recientes |
| `lichii/hook/coop_npc_gaze.h`, `lichii/hook/coop_npc_gaze.cpp` | Para un aldeano que debe mirar a un compañero (el jugador más cercano, o el que habla con él), cada ranura de objetivo de mirada que apunta a nuestro Link se convierte en una ranura de lugar en los ojos de la marioneta del compañero, y se devuelve después. Comprueba por sí mismo que el juego dejó las ranuras como se escribieron |

### Colores

| Archivo | Qué hace |
|---|---|
| `coop/protocol` (`PLAYER_LOOK`), `coop/presence` | El color de un jugador para su etiqueta de nombre y su línea del panel, enviado tras cada `WELCOME` y al cambiar, guardado por el servidor y reenviado a quienes se unen. `slot_color` da al jugador 5 morado `#A05CE0` y al jugador 6 naranja `#F09A28` |
| `lichii/hook/coop_hook.cpp` (`OrmRgb`) | Convierte el `color` ya interpretado de upstream (`CoopColors::Parse`) para `PLAYER_LOOK` |
| `tools/release/kit/scripts/kit-paths.ps1`, `coop-ini.ps1` | Exportan el color de `coop.ini` como `LICHII_COOP_COLOR` |

Los colores de túnica propios de upstream (test10, componente 16 de `PLAYER_EQUIP`) son lo que tiñe la marioneta; nuestro tinte anterior se eliminó en favor del suyo.

### Paquete de marionetas (`tools/puppet_pack`)

`build.py` construye, además de los actores de upstream: el usuario de efectos del jugador en las marionetas por defecto (`ELinkRef`, `ELinkUserName`; `--no-effects` construye sin él), sustitutos de efectos (`EFFECT_STAND_INS`: el Fuego de Din, la onda expansiva del martillo), sustitutos de estados (`FX_STAND_INS`: `CoopFx_Burn`, `_Freeze`, `_Shock`, `_Nayru`, cada uno el usuario de efectos de un localizador de efecto siempre activo), sustitutos de flechas elementales (`ARROW_ELEMENT_STAND_INS`), sustitutos de objetos de cofre (`GET_STAND_INS`, `CoopGet_<item>`), `CoopShot_FishingLine`, los caballos (`HORSES`: `CoopPuppet_Horse` con el aspecto de Epona, `_Horse_Ranch`, `_Horse_Ingo`; sin física, sin atención, sin nombre, así que nadie puede subir a uno), las variantes de fijación de blanco `CoopPuppet_*_Lock` (09-pvp) y los actores de cuerpo (12-bodies). Escribe `nayru_s` (el `NayruLoveTime` del juego) en `coop_puppet.json`. `dumpcheck.py` extrae las fuentes opcionales (Dark Link incluido). Un paquete construido antes de esto no las tiene: cada capa recurre a una alternativa con una línea de registro (caballo de evento simple, señuelo sin sedal, tiempo estimado del cristal).

Puntos de enganche en archivos de upstream:

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_puppet.cpp`, `coop_puppet.h` | Colocación suavizada y poses con tiempo, los comandos de efectos, el parpadeo y los estados en el planificador, la Ocarina en la mano, el jinete mostrado sobre un caballo, el paso de viaje sumado al objetivo, el seguimiento de máscara (`SetHeadFollow`), el quitado de armadura sobrante, el observador de plan de los cuerpos y `LastSmoothed` (12-bodies) |
| `lichii/hook/coop_puppet_config.cpp` | `OrmSettings`: nuestros interruptores hacia los ajustes del planificador, y sus líneas de arranque |
| `lichii/hook/coop_pose.cpp` | Poses marcadas con el momento en que se leyeron, mostradas en la línea de tiempo del emisor; `OBJECT_POSE` para cualquier actor |
| `lichii/hook/coop_spawn.cpp` | Sustitutos de estados y efectos en las marionetas, cadenas en las copias |
| `lichii/hook/coop_objects.cpp`, `coop_objects_sync.cpp` | Interruptores y enrutado de suavizado de copias, carga, pose y mirada de aldeanos, suelos móviles entregados a `orm_ride` |
| `lichii/hook/coop_svc.cpp`, `coop_fairy.cpp` | Los llamadores svc de nuestros directores; el hada se queda junto a un jinete sobre su caballo |
| `lichii/hook/coop_ocarina.cpp` | Lee los nodos de nota y canción de la Ocarina (`<note>_lp`, `Melody_<name>`) del comando AS de una marioneta, omitiendo los nodos de dedos en reposo |

## Mensajes y componentes

| Nombre | Número | Uso |
|---|---|---|
| `PLAYER_LOOK` | 0xF004 | El color de un jugador |
| `OBJECT_POSE` | 0xF005 | Los huesos de un aldeano |
| `OCARINA_NOTE` (el nuestro, `MessageType::orm_ocarina_note`) | 0xF007 | Notas y canciones (solo canciones por defecto); el `OCARINA_NOTE` 140 de upstream lleva las notas |
| `PLAYER_SAMPLE` componente `ride` | 30 | El suelo sobre el que viaja un jugador y su lugar en él |
| `PLAYER_EQUIP` componente `mount` | 30 | El caballo en el que se sienta un jinete, por nombre de actor |
| `OBJECT_SAMPLE` componente `carry` | 29 | Un objeto levantado en las manos de un jugador: quien lo sostiene, lugar, hueso |
| El `PLAYER_RIDE` de upstream | 385 | Epona bajo un jinete, dibujada por upstream |

Los efectos, el parpadeo, los estados, el cristal de Nayru, las máscaras, las cadenas y la armadura puesta usan solo el comando de `PLAYER_SAMPLE` y los campos de `PLAYER_EQUIP` ya existentes: ningún mensaje nuevo.

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/host/pose_smooth_test.cpp` | El búfer de 100 ms, la inestabilidad, ráfagas, muestras perdidas y desordenadas, extrapolación acotada, correcciones suavizadas, saltos directos, un reloj que nunca retrocede, el paso según el emisor |
| `tests/orm/lichii_pose_test.cpp` | Poses con tiempo según el momento del emisor, un flujo detenido mantenido y luego mezclado hacia fuera, una pose marcada al leerse |
| `tests/lichii/hits_test.cpp` | Suavizado de copias de enemigos (con la respuesta de golpes de 05-bosses) |
| `tests/lichii/effects_test.cpp` | Las reglas (solo `*` como comodín; hechizos, natación y movimientos reproducidos, nunca ataques, daño, objetos ni caballo; los movimientos de sacudida nunca), parpadeo, fuego, hielo, descarga, el cristal de Nayru y su final, el archivo de reglas y los interruptores |
| `tests/host/worn_armor_test.cpp` | Qué piezas se muestran y cuáles puede quitarse una marioneta; la pieza antes que la marca; nombres de enlace comprobados |
| `tests/lichii/carry_test.cpp` | El reclamo de levantamiento, la muestra de carga, la copia en las manos de quien la sostiene sobre memoria falsa |
| `tests/lichii/mascaras_test.cpp` | Disposiciones de rotación, marcos a través de padres, lectura del esqueleto, máscara colocada y huesos copiados, cadena y sedal tendidos, plegado sin marioneta |
| `tests/lichii/ocarina_note_test.cpp` | Lectura del control de Ocarina y de la nota que reproduce el juego; una nota al cambiar, un soltado al parar |
| `tests/orm/orm_ocarina_sound_test.cpp`, `tests/orm/lichii_ocarina_test.cpp` | Nombres de notas, desactivado por defecto, una nota suena en la marioneta, sin doble sonido del comando y del mensaje |
| `tests/orm/lichii_puppet_test.cpp`, `tests/lichii/puppet_test.cpp` | La Ocarina en la mano y `LICHII_ORM_OCARINA_HAND`, seguimiento de máscara en cada fotograma, ajustes del planificador |
| `tests/lichii/epona_test.cpp`, `tests/host/epona_test.cpp` | El caballo bajo su jinete (erguido, desplazamiento de la silla, rumbo), paso según la velocidad, aspecto según el caballo, sonda de silla |
| `tests/host/ride_test.cpp`, `tests/lichii/ride_test.cpp`, `tests/lichii/ride_hook_test.cpp` | El componente `ride` en el cable, qué suelos transportan, inicio y fin del viaje, un Link junto a un ascensor no viaja en él, un compañero dibujado en nuestro suelo solo cuando está cerca |
| `tests/lichii/npc_pose_test.cpp` | El esqueleto del dueño llegando a la copia, 20 por segundo, solo huesos cambiados, gana la pose más reciente, otros esqueletos sin escribir |
| `tests/lichii/npc_gaze_test.cpp` | Ranuras de objetivo, la regla del jugador más cercano, gana quien habla, como mucho unos pocos aldeanos girados |
| `tests/orm/presence_test.cpp` | El color de un jugador desde `PLAYER_LOOK`, conservado hasta que se va |
| `tools/puppet_pack/tests/test_puppet_pack.py` | Se construyen los sustitutos, caballos, variantes de fijación y actores de cuerpo |

Escenarios del arnés (viven en `tools/orm/harness`): `levantar-vasija` (A levanta y lanza una vasija, B la ve en las manos de la marioneta de A y rota), `ocarina-mano` (B toca notas y la Nana de Zelda, A ve la Ocarina en la mano y oye las notas), `efectos-cofre` (el objeto del cofre levantado), `charla-dos` (la pose del aldeano enviada y mezclada de vuelta), `efectos-magia`, `efectos-dano`, `mascaras-ponerse`, `mascaras-lente`, `gancho-cadena`, `pesca-sedal`, `epona-montar`, `epona-rancho`, `epona-cancion`, `mirada-pnj`, `tunica-color`.

## Líneas de registro

- `coop: puppets: smoothing on (LICHII_ORM_SMOOTH)` o `smoothing off`; por jugador, `P<n> smooth: ...` mediciones; para las copias, `copy smooth: <s> s: <n> samples (<n> late, <n> reordered, <n> dropped), ...`; `coop: objects: share hits feedback ..., owner dedupe ..., copy smoothing on|off`.
- `coop: effects: commands under the pose on, damage flash on, burn/freeze/shock and Nayru's crystal on (60 s crystal)`; `coop: puppet: P<n> effects: '<command>' played under the copied pose ...`, `... effects: flash ('<command>')`, `... shows burn (was none)`, `... crystal on for <s> s`, `... crystal off (time|hurt|scene|downed)`; `coop: check: nayru crystal ok <s> s, the game's NayruLoveTime (PlayerGameParam, read by the puppet pack)` o `... fallback ... the estimate: the puppet pack has no nayru_s (run make-puppet-pack again)`.
- `coop: carry: on (lifted objects claimed and carried ...`; `coop: carry: lifting <actor> ... claimed)`; `coop: carry: P<n> holds <actor> ...: drawn in the puppet's hands (bone <n>|actor frame)`; `coop: carry: P<n> let go of ...: it follows the samples`; `coop: check: carry bone ok the puppet's bone <n> reads as a rigid frame`.
- `coop: puppet: P<n> head '<mask>' (actor ...) was <d> from its puppet: placed on it every frame, <n> bones of the same name copied ...`; `coop: spawn: hookshot chains and fishing lines on the copies on` (o `off (LICHII_ORM_TETHERS=0)`); `... laid to the puppet's FirePos_HookShot ...`, `... fishing line laid to the puppet's Rod_4 ...`.
- `coop: puppets: the Ocarina in the hand while its player plays: sub weapon`; `coop: puppet: P<n> wears sub weapon 'Item_Ocarina_...'`; `coop: check: ocarina hand ok P<n>'s own Ocarina is in its hand`.
- `coop: ocarina sound: on, songs only: the others' notes are upstream's (OCARINA_NOTE 140), ...` o `coop: ocarina sound: on, notes and songs by ours (LICHII_ORM_OCARINA_NOTES=1; ...` o `coop: ocarina sound: off (LICHII_COOP_OCARINA_SOUND=1 plays the others' notes)`; `coop: ocarina note: our notes are sent (LICHII_ORM_OCARINA_NOTE; ...; songs only: the notes go as upstream's OCARINA_NOTE 140)`; `coop: ocarina sound: P<n> note <note>`, `... P<n> Melody_<song>`.
- `coop: epona: riders drawn on a horse, actor <actor>, saddle (x, y, z)` o `coop: epona: riders' horses off (LICHII_ORM_HORSE=0)`; `coop: epona: P<n> on Epona: drawn by upstream's PLAYER_RIDE (CoopRide), not ours`; `coop: epona: the puppet pack has no <actor> (run make-puppet-pack again ...`; `coop: epona: saddle probe on (LICHII_ORM_DEBUG_HORSE)`.
- `coop: ride: we ride <actor> <key>, at <dx> <dy> <dz> from it`, `coop: ride: we are off ...`, `coop: ride: P<n> rides <key>: drawn on our floor`, `... our floor is <d> m from theirs, drawn where they are`, `... not loaded here, drawn where they are`, `coop: ride: P<n> off its floor`.
- `coop: npc pose: on (...`, `coop: npc pose: sending <actor> ...`, `coop: npc: copy ... blends back to its owner's pose over 600 ms`; `coop: npc gaze: ...`, `check: npc gaze ok (...)` o `check: npc gaze fallback (the game set the slots again ...)`.

## Estado

| Parte | Estado |
|---|---|
| Objetos cargados | Confirmado en partida en nuestra compilación basada en test9 (escenario `levantar-vasija`, 16/16, A uniéndose en segundo lugar) |
| Ocarina en la mano y notas oídas | Confirmado en partida en nuestra compilación basada en test9 (escenario `ocarina-mano`, 17/17), cuando nuestro mensaje aún llevaba las notas. La división de test10 (notas por el 140 de upstream, canciones por el nuestro) todavía no está confirmada en partida |
| Objeto del cofre levantado en la marioneta | Confirmado en partida en nuestra compilación basada en test9 (escenario `efectos-cofre`, 23/23) |
| Pose del aldeano | Confirmado en partida en nuestra compilación basada en test9 (escenario `charla-dos`, 8/8: pose enviada, la copia se mezcla de vuelta) |
| Suavizado | Activado en todas las ejecuciones del arnés, todavía no confirmado en partida por sí solo (sin medición con seis jugadores) |
| Efectos de hechizos, parpadeo por daño, sustitutos de estados | Todavía no confirmado en partida (`efectos-magia` 10/20, `efectos-dano` sin pasar en verde) |
| Máscaras, lente, cadena del gancho, sedal de pesca | Todavía no confirmado en partida |
| Armadura puesta quitada a una marioneta | Todavía no confirmado en partida |
| Caballos | Todavía no confirmado en partida (`epona-rancho`, `epona-cancion` 5/13: las partidas guardadas de sonda no dieron un caballo que montar) |
| Suelos móviles | Todavía no confirmado en partida |
| Mirada de aldeanos | Todavía no confirmado en partida |
| Colores (`PLAYER_LOOK`, ranuras 5 y 6) | Todavía no confirmado en partida |

## Carencias conocidas

- Efectos: si el efecto de un hechizo viene del comando (visto en la marioneta) o del propio código del jugador (no visto) lo dirá una ejecución del juego; lo mismo vale para qué localizador de efecto se ve bien en cada sustituto de estado. Congelación y descarga todavía no tienen escenario.
- Máscaras: lo que hace el código del jugador al ponerse una máscara (desplazamiento de UV de la cara, física del flequillo) no está hecho; el pelo de la marioneta podría asomar. La disposición de rotación para la cadena y el sedal se decide en tiempo de ejecución a partir de la columna de la marioneta.
- Ocarina: ahora dos mensajes llevan la Ocarina de un jugador (las notas de upstream, nuestras canciones); la división solo tiene pruebas unitarias.
- Caballos: es posible un breve momento con dos caballos al subir a Epona (el nuestro cediendo al `PLAYER_RIDE` de upstream); la altura de la silla no está medida (valor por defecto de `LICHII_ORM_HORSE_SADDLE`).
- Suelos móviles: los suelos giratorios (Campo de Entrenamiento Gerudo, Templo de las Sombras) y el pasillo retorcido del Templo del Bosque no se siguen; una marioneta allí se dibuja según sus muestras.
- Colores: el color de la túnica es el de upstream; nuestros colores de `PLAYER_LOOK` solo tiñen la etiqueta de nombre y la línea del panel, así que tras `/color` en el chat (09-pvp; envía `PLAYER_LOOK` al instante y escribe `coop.ini`) la etiqueta cambia al instante y se espera que la túnica lo siga solo en el siguiente arranque (no comprobado).
