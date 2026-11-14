[English](12-bodies.md) | **Español**

# 12-bodies: cuerpos reales entre los Links

La marioneta de cada compañero recibe un cuerpo real en tu juego: tu Link se detiene ante ella como ante un aldeano y puede subirse a su cabeza. Antes, dos Links simplemente se atravesaban, y un compañero que pulsaba Continuar en el mismo punto que tú se dibujaba dentro de tu Link. Los cuerpos son un interruptor de sala (`/choque`), encendido por defecto. Cuando los cuerpos no pueden funcionar (paquete de marionetas antiguo, una creación que el juego rechaza, un interruptor en off), la marioneta del compañero solo se dibuja apartada, que es la capa más antigua de esta función.

## Qué cambia para los jugadores

- Al chocar con un compañero, tu Link se detiene a su lado. Las paredes y las barandillas siguen contando, porque quien te detiene es la física del propio juego, no nuestro código.
- Puedes subirte a la cabeza de un compañero (un disco fino sobre el que se puede estar de pie, encima de su cuerpo). Desde los lados nadie sube: resbalas, como con un aldeano.
- Nadie se queda atascado. El cuerpo de un compañero es un "fantasma" (no sólido) hasta que ambos estáis a 0,8 m de distancia, o uno encima del otro, durante 0,3 s. Dos jugadores que pulsan Continuar en un mismo punto empiezan, por tanto, separados. Mientras un cuerpo es un fantasma, su marioneta se dibuja apartada (1 m como máximo, solo presentación).
- Un cuerpo también es un fantasma mientras ese compañero está caído (para que quien lo reviva pueda arrodillarse junto a él), montado, oculto o en otro lugar, y después de que sus muestras salten más de 3 m (un salto de teletransporte).
- Cualquiera de la sala puede escribir `/choque on` o `/choque off` (alias `/cuerpos`, `/bodies`; `/choque` solo dice cómo está; Tab completa `on`/`off`; aparece en `/ayuda`). Todos ven el aviso "Choque entre jugadores activado por <name>", y quien entra tarde ve "En esta sala Choque entre jugadores está activado (lo activó <name>)".
- Esta función nunca escribe la posición de tu propio Link.

## Interruptores

| Variable | Valor por defecto del kit | Valor por defecto del código | Valores | Qué hace |
|---|---|---|---|---|
| `/choque` (interruptor de sala, chat) | on | on (`kBodiesDefault = true`, `coop/host/include/coop/host/orm_pvp.hpp`) | `on`, `off` | La elección de la sala: cuerpos reales para todos, o ninguno. El servidor lo conserva como cualquier interruptor de sala |
| `LICHII_ORM_BODIES` | `LICHII_ORM_BODIES=1` | on (`coop_puppet_config.cpp`, `CoopSwitch::On`) | 0/1/true/false/yes/no/on/off | La capa de dibujo apartado: la marioneta de un compañero que está sobre tu Link se dibuja a dos radios de cuerpo de distancia. 0: las marionetas se dibujan en su muestra aunque se solapen. También se nombra en la línea de arranque de los comandos `/pvp` |
| `LICHII_ORM_BODY_COLLIDER` | no está en el kit | `room` (`coop_bodies.cpp`, `CoopSwitch::Value`) | `room`, o una palabra on/off | `room`: decide el `/choque` de la sala. On: cuerpos digan lo que digan en la sala (usado por las sondas). Off: nunca hay cuerpos en este PC, los compañeros solo se dibujan apartados. Cualquier otro valor: `room` y una línea de registro |
| `LICHII_ORM_BODY_TOP` | no está en el kit | on (`coop_bodies.cpp`) | 0/1/true/false/yes/no/on/off | El disco de la cabeza. 0 usa los actores `_BodySide` (solo los lados: resbalas de una cabeza) |

Nada de esto funciona sin `LICHII_COOP_PUPPET=1` (los cuerpos siguen a las marionetas) y un paquete de marionetas construido con este kit (debe incluir `CoopPuppet_*_Body`).

## Cómo funciona

| Archivo | Qué hace |
|---|---|
| `tools/puppet_pack/build.py` (`BODY_KINDS`) | Construye cuatro actores extra a partir del romfs de cada jugador: `CoopPuppet_Child_Body`, `CoopPuppet_Adult_Body` (lados más disco de la cabeza) y `CoopPuppet_Child_BodySide`, `CoopPuppet_Adult_BodySide` (solo lados). Cada uno son dos cuerpos rígidos cinemáticos que siguen al actor y solo chocan con la capa `Player`: los lados son el prisma `Player_cct` del propio jugador (radio 0,23 / cabeza 1,17 m de niño, 0,35 / 1,68 m de adulto) en la capa `Character`/`NPC` con el material de personaje no montable; el disco tiene 0,12 m de grosor sobre la cabeza, en la capa `GroundObject` con un material sobre el que se puede estar de pie. Sin modelo, animación, vida ni IA: la misma forma que los muros invisibles `FldObj_AirWall_Npc` del juego. Los lista bajo `"bodies"` en `coop_puppet.json` |
| `coop/host/include/coop/host/orm_bodies.hpp`, `coop/host/src/orm_bodies.cpp` | Lógica portable, sin emulador. `PuppetAside`: el paso de dibujo apartado (dirección desde nuestro Link hacia la marioneta; si están uno encima del otro, una dirección fija según el número del compañero; suavizado a 2 m/s). `ReadBack`: registra cuándo el juego movió una marioneta que habíamos dibujado. `BodyGate`: sólido o fantasma, plano a plano (`kClearM` 0,8 m durante `kClearForMs` 300 ms para volverse sólido; otra vez fantasma a menos de `kDeepM` 0,25 m, caído, montado, no mostrado, o un salto de más de `kJumpM` 3 m), más las líneas de autocomprobación para las sondas |
| `lichii/hook/coop_bodies.h`, `lichii/hook/coop_bodies.cpp` | El pegamento con el juego. Un cuarto director de marionetas (orden de llamada del svc 26: después de las marionetas, la aparición, las hadas y los caballos; antes del movedor de objetos 30) crea y coloca los actores de cuerpo. En cada fotograma el cuerpo va exactamente donde el director de las marionetas dibujó a ese compañero en ese fotograma (`CoopPuppet::LastSmoothed`), en vertical, y entonces corre el establecedor de matriz del propio juego (`SetMatrixCall`) para que su física ocupe el lugar. Los cuerpos se crean aparcados bajo el suelo (`SetCreateParked`): nunca sobre nadie. `Choose` elige on/off/alternativa; `CreateTally` cuenta las líneas de creación y recurre a la alternativa tras dos creaciones fallidas de un actor sin ninguna lograda |
| `coop/host/src/orm_commands.cpp` | La entrada del comando `/choque` (`Id::bodies`, alias, `Who::anyone`) |
| `coop/host/src/orm_pvp.cpp` | Nombres y valores por defecto de los interruptores de sala (`default_on`: `bodies` es la única clave encendida por defecto), y los avisos |
| `lichii/hook/orm_commands_game.cpp` | Guarda el estado actual del `/choque` de la sala (`BodiesOn`) y el indicador de alternativa (`SetBodiesFallback`); cuando nuestro juego no puede crear cuerpos, la respuesta de `/choque` añade "(en tu juego los compañeros solo se apartan: mira el registro, 'bodies: collider')" |

Puntos de enganche en archivos de upstream:

| Archivo | Enganche |
|---|---|
| `lichii/hook/coop_puppet.cpp` | El planificador ejecuta `PuppetAside` (cuando `settings_.bodies`) y el `BodyGate` (cuando los colisionadores están activos): la marioneta de un cuerpo sólido se dibuja en su muestra, la de un fantasma, apartada. Las líneas de la puerta y del apartado se suman a las líneas de registro de las marionetas |
| `lichii/hook/coop_puppet_config.cpp` | `OrmSettings` lee `LICHII_ORM_BODIES`; `ConfigurePuppets` llama a `CoopBodies::Use()` después de configurar las marionetas |
| `tools/puppet_pack/build.py` | Los actores de cuerpo se construyen junto con los demás actores de marioneta (`BODY_KINDS`) |

## Mensajes y componentes

- `ROOM_SWITCH` (0xF008), clave `bodies` = 3 (`RoomSwitchKey` en `coop/protocol/include/coop/protocol/messages.hpp`). Las claves 1 y 2 son el PvP y la fijación de aliados (09-pvp). El servidor guarda y retransmite cualquier clave; no tiene código propio para los cuerpos.
- Ningún otro mensaje: los cuerpos se colocan a partir de las muestras ya existentes de las marionetas.

## Pruebas

| Archivo | Qué comprueba |
|---|---|
| `tests/orm/bodies_test.cpp` | Dibujo apartado: el caso del punto de Continuar (la marioneta dibujada fuera de inmediato, nuestro Link sin moverse nunca), el paso suavizado, la dirección fija por número a pesar del temblor, caído / otro piso / no mostrado / ningún Link dibujado en la muestra, la lectura de vuelta una vez por contacto, el planificador con `LICHII_ORM_BODIES` encendido y apagado. Cuerpos: fantasma hasta estar despejado y luego sólido, un compañero nuevo empieza como fantasma incluso lejos, salto de teletransporte / caído / montado / oculto / sin Link lo vuelven fantasma, uno encima del otro cuenta como despejado (estar de pie sobre una cabeza), un cuerpo sólido dibujado en su muestra, la puerta del planificador (`SetColliders`), las líneas de autocomprobación de las sondas |
| `tests/lichii/bodies_hook_test.cpp` | El análisis de `LICHII_ORM_BODY_COLLIDER` (sala por defecto, palabras on/off, cualquier otra cosa sala con una línea), nombres de actores por edad y tipo y cuáles lista el paquete, `Choose` (interruptor de sala, modos forzados, paquete, creación rota), el plan de los cuerpos a partir del plan de las marionetas (colocar menos el apartado, sólido mostrado, fantasma aparcado), solo la pose del mismo fotograma, el recuento de creaciones |
| `tools/puppet_pack/tests/test_puppet_pack.py` (`BodyTest`, prueba de construcción completa) | Los archivos Phive de los actores de cuerpo: dos cuerpos rígidos `CoopBody` y `CoopTop`, tamaños a partir del prisma del jugador |
| `tests/orm/commands_test.cpp` | `/choque`, `/cuerpos` se interpretan como el interruptor de cuerpos de la sala |

Escenarios del arnés (viven en `tools/orm/harness`): `choque-bloqueo` (B escribe `/choque on` y se queda quieto; A camina hacia él y debe detenerse, luego alejarse sin quedar atascado), `choque-encima` (A cae sobre la cabeza de B y debe quedarse ahí; con `LICHII_ORM_BODY_TOP=0` debe resbalar), `choque-continuar` (ambos aparecen en un mismo punto de Continuar, entrando A en segundo lugar; ninguna caminata agota el tiempo, el Link de nadie se mueve). `cartel` y `charla` cubren el caso más antiguo de dibujo apartado en el punto de Continuar.

## Líneas de registro

- Al empezar: `coop: bodies: colliders by the room's /choque; the head disc on; the puppet pack has CoopPuppet_*_Body, CoopPuppet_*_BodySide` (las partes del modo y del paquete cambian con los interruptores y el paquete).
- En cada cambio: `coop: bodies: colliders on (the room's /choque, CoopPuppet_*_Body, with the head disc): each mate's body waits until clear of our Link`, o `coop: bodies: colliders off: <why>` (por ejemplo `the room's /choque is off: the mates are drawn aside`).
- Una vez por vía: `coop: check: bodies: collider ok CoopPuppet_Child_Body made: kinematic, placed with its puppet each frame, hitting only our Link`, o `coop: check: bodies: collider fallback <why>` (ningún cuerpo en el paquete: vuelve a ejecutar make-puppet-pack; un cuerpo no se pudo crear 2 veces; `LICHII_ORM_BODY_COLLIDER=0`).
- La puerta: `bodies: P2's body a ghost until clear: on our Link (0.17 m)`, `bodies: P2's body solid (1.00 m from our Link)`, `bodies: P2's body a ghost: <why>` (caído, montado, no mostrado, nuestro Link sin leer, sus muestras saltaron).
- Autocomprobaciones de las sondas: `bodies: our Link against P2's body (0.47 m apart, the bodies 0.46)` tras 0,5 s tocándose, `bodies: our Link stands on P2's head (1.17 m over his feet)` tras 1 s encima.
- Dibujo apartado: `bodies: P1's puppet on our Link (...): drawn ... m aside..., our Link untouched`, `bodies: P1's puppet drawn on its sample again`, `bodies: P1's puppet read back where we drew it aside ...`.
- Las líneas del propio director de cuerpos llevan un prefijo `body `, por ejemplo `body coop: puppet: P2: create CoopPuppet_Child_Body requested ...`.

## Estado

Confirmado en partida en test10, dos jugadores, WebSocket, protocolo sellado 110, entrando A en segundo lugar: escenario `choque-bloqueo` (14/14: `collider ok CoopPuppet_Child_Body`, A se detiene a 0,44 m de B con los cuerpos a 0,46 de distancia, luego se aleja), `choque-encima` (10/10: `our Link stands on P1's head`), `choque-continuar` (9/9: la marioneta dibujada 1 m apartada en el punto de Continuar compartido, el cuerpo sólido allí, ninguna caminata agotó el tiempo, ningún Link se movió). `cartel` y `charla` (4/4 cada uno) pasaron en la misma ejecución, así que el atasco anterior en el punto de Continuar no volvió.

## Carencias conocidas

- Todavía no se ha visto con un ping alto. Cada juego detiene a su propio Link ante el cuerpo del otro; con mucho retraso, el juego puede dar un pequeño empujón. Si se ve mal, subir `kDeepM` o bajar el radio.
- Sin comprobar: qué ve B mientras A está de pie sobre su cabeza (el cuerpo de A toca la cabeza de B desde arriba y puede apartarlo), si el disco arrastra a un Link cuando el compañero de abajo camina, y si tu propia espada, las flechas o la cámara notan los cuerpos (no deberían: los cuerpos solo chocan con la capa `Player`).
- Los cuerpos están aparcados 500 m bajo el suelo mientras son fantasmas; si alguna vez el juego borra un cuerpo aparcado, el director lo vuelve a crear (se vería en el registro).
- La pantalla "Cooperativo" de la app no tiene casilla para `/choque`; solo se maneja por chat.
- No probado en una compilación de Hookshot.
