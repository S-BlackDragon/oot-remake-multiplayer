# Un regalo para el cooperativo de daurosk: nuestro trabajo sobre v0.1.0-test10

*English: [README.md](README.md)*

Hola, daurosk. No soy programador, pero conozco bien Ocarina of Time. Juego tu cooperativo con amigos y, con agentes
de IA (Claude), añadí estas cosas sobre tu test10. Es un regalo: toma lo que quieras tal cual o cámbialo; si algo no
encaja, no pasa nada. Gracias por tu trabajo.

## Qué es esta rama

- Rama `regalo-test10`: tu etiqueta `v0.1.0-test10`, sin cambios, y encima **un commit por función** (15 funciones y
  esta documentación). No se quita nada tuyo; todo archivo que añadimos o cambiamos está en inglés y comentado para un
  lector que no es el autor.
- **Compilado y probado entero** en la punta de la rama: `ctest` 2591/2591 en verde (las pruebas de la app Qt necesitan Qt 6 y aquí no se compilaron; pasaron en su propia rama) (Windows, MSVC, Ninja).
- **Cómo se repartieron los commits.** Nuestro trabajo creció en muchas ramas pequeñas. Para este regalo cada línea que
  añadimos se agrupó por la función a la que pertenece, así que cada commit lleva los archivos propios de esa función y
  sus líneas en archivos compartidos de upstream (`coop_hook.cpp`, `messages.hpp`, `codec.cpp`, `session.cpp`,
  `core.cmake`, las listas de CMake). Casi todas las funciones usan `01-platform` (el lector de interruptores y la capa
  de plataforma) y `02-network` (nuestro bloque de mensajes): para tomar una función suelta, toma esas dos primero. Un
  commit suelto puede necesitar aún alguna línea de otro; el árbol de la punta de la rama es el que compila.
- **Los interruptores conservan los valores con los que los entregamos.** Cada función tiene su interruptor (una
  variable de entorno, puesta desde `tools/release/kit/coop-options.ini`). Lo que no se ha confirmado en una partida
  real va **apagado**.
- **No está aquí, a propósito:** nuestro servidor, nuestro lanzador y su canal de actualización, cualquier descarga del
  juego, claves o firmware, y cualquier dato del juego. La pantalla "Cooperativo" (14) habla con cualquier ayudante que
  escribas; su protocolo está documentado.

## Qué probamos, qué funciona, qué es todavía una idea

| Documento | Qué cuenta |
|---|---|
| [docs/gift/es/TESTED.md](docs/gift/es/TESTED.md) | cada escenario que corrimos con dos juegos reales: qué comprueba, verde o rojo, la causa de cada rojo y su arreglo, y los fallos que encontramos en código de upstream, con líneas de registro |
| [docs/gift/es/WORKS.md](docs/gift/es/WORKS.md) | solo lo confirmado en una partida con dos jugadores, con el escenario y la prueba |
| [docs/gift/es/IDEAS.md](docs/gift/es/IDEAS.md) | ideas sin terminar o sin confirmar: cómo las haríamos, qué se sabe, la sonda que las confirmaría |
| [docs/gift/es/HOW-WE-WORK.md](docs/gift/es/HOW-WE-WORK.md) | nuestro método en palabras sencillas: dos juegos ocultos por prueba, directivas de escenario, partidas sintéticas, interruptores apagados hasta que una partida los confirma, caminos de reserva con su línea de registro |
| [docs/gift/es/LIMITS.md](docs/gift/es/LIMITS.md) | límites sinceros: un solo PC de casa, por qué validar es lento, dónde estamos, cuánto puede tardar |
| [docs/orm/README.md](docs/orm/README.md) | una página por función (en inglés): interruptores, archivos, enganches en tus archivos, mensajes, pruebas, líneas de registro, lo que falta |

## Las funciones

Estados: **confirmado en partida** = visto con dos jugadores en una partida real del escenario nombrado ("test10" = en
este árbol; los demás en nuestra versión basada en test9). **Todavía no** = solo con pruebas unitarias.

| Función | Qué cambia para los jugadores | Interruptor (valor con el que va) | Archivos principales | Estado | Cómo tomarla |
|---|---|---|---|---|---|
| [01-platform](docs/orm/01-platform.md) | Nada visible; cualquier interruptor ORM acepta 0/1, true/false, yes/no, on/off y registra un valor raro | ninguno (siempre activo) | `lichii/hook/platform*.{h,cpp}`, `coop_switch.h`, `coop_frame_log.*` | Emulador: confirmado en partida (todos los escenarios). Hookshot: todavía no | `git cherry-pick 74f2683e` |
| [02-network](docs/orm/02-network.md) | Hasta seis jugadores; entrar también por ws/wss; aviso cuando tu conexión no da abasto; un anfitrión elegido por la calidad de conexión o con `/anfitrion` | `server_websocket` (kit: encendido); transporte udp por defecto | `coop/protocol/orm_*`, `coop/net/*transport*`, `server/orm_*`, `server/host_choice.*` | Protocolo 110: confirmado en partida, test10. Seis jugadores, elección de anfitrión: todavía no | `git cherry-pick 5f0652fe` |
| [03-fixes](docs/orm/03-fixes.md) | Un anfitrión en la pantalla de título ya no manda las banderas de una partida nueva; objetos llevados y huesos se leen bien | ninguno | fixes inside existing files | Arreglo de la pantalla de título: confirmado en partida. Los demás: ver TESTED.md | `git cherry-pick 8257b784` |
| [04-downed](docs/orm/04-downed.md) | Con un compañero en la sala caes en vez de morir; un compañero te revive; todos caídos = fin de partida del grupo; cámara de espectador | `LICHII_COOP_DOWNED=1`, `LICHII_COOP_SPECTATOR=1` | `coop/host/downed.*`, `lichii/hook/coop_downed*`, `coop_spectator.*`, `coop_continue.*` | Confirmado en partida: `caer-pareja`, `caer-solo`. Continuar conservando: todavía no | `git cherry-pick 5aff2704` |
| [05-bosses](docs/orm/05-bosses.md) | Los jefes que agarran o de tenis mantienen su dueño; los enemigos van a los que siguen en pie; final y huida con seis | `LICHII_ORM_BOSS_FAMILIES` (encendido); `LICHII_ORM_ESCAPE_CLOCK` apagado | `coop/host/boss_rules.*`, `final_rules.*`, `hazards.*`, `lichii/hook/coop_enemies.*`, `coop_hits.*`, `coop_final*`, `server/enemies.cpp` | Entrada a la sala del jefe: confirmada en partida (`jefe-gohma-entrada`). Lo demás: todavía no | `git cherry-pick df8324c0` |
| [06-world](docs/orm/06-world.md) | Niveles de agua, interruptores sostenidos, cofres y puertas en todas las pantallas, banderas de charla, invitación de canción de viaje, listas del kit, tu propia cartera | `LICHII_ORM_WATER=1`, `LICHII_COOP_TALK`, `LICHII_COOP_WARP_INVITE=1`, `LICHII_COOP_DUNGEON_LISTS`, `LICHII_ORM_ITEM_RULES` | `coop/host/{water,held_switch,chest_open,doors,npc_talk,songs,dungeon_lists,...}.*`, `tools/release/kit/shared_*.txt` | Confirmado en partida: `meca-agua-templo`, `meca-sostenido-*`, `efectos-cofre`, `charla-dos`, `viaje-cancion`, `cartera-cada-uno`. La mayoría de las listas: todavía no | `git cherry-pick d88b42b7` |
| [07-cutscenes](docs/orm/07-cutscenes.md) | Una cinemática de la historia que empieza un jugador se ve en todos los juegos de la escena; solo la salta su dueño | `LICHII_ORM_STORY_EVENTS=1` | `coop/host/story.*`, `lichii/hook/orm_story_events*`, `shared_story.txt` | Confirmado en partida: `historia`. Saltar: todavía no | `git cherry-pick 594cdff2` |
| [08-age](docs/orm/08-age.md) | El grupo sigue el cambio de edad de un compañero al Templo del Tiempo tras un aviso y una cuenta atrás de 3 s | `LICHII_ORM_AGE=0` (apagado) | `coop/host/age.*`, `time_temple.*`, `lichii/hook/coop_age_game.*`, `coop_time_temple*` | Todavía no (el juego rechazó el evento del cambio de era en nuestras pruebas) | `git cherry-pick 9e28597d` |
| [09-pvp](docs/orm/09-pvp.md) | Daño real del arma, escudo, Amor de Nayru y empujón sobre tu PvP; fijar a los compañeros con Z; comandos `/` con Tab | `pvp = off` (decide el anfitrión); `LICHII_ORM_PVP` (encendido) | `coop/host/orm_pvp.*`, `orm_commands.*`, `lichii/hook/orm_commands_game.*`, `orm_chat_complete.*` | Daño, invulnerabilidad, empujón: confirmados en partida, test10 (`pvp-espada`). Escudo, fijar: todavía no | `git cherry-pick 9beae1ef` |
| [10-hud](docs/orm/10-hud.md) | Nombres, panel, chat, avisos y las rupias de todos dentro de la imagen; tus propios ajustes del HUD; compañeros en los mapas; mensajes rápidos | `LICHII_COOP_HUD=1` (nuestro HUD), `LICHII_COOP_MAP=1`, `LICHII_ORM_QUICK_CHAT=1` | `lichii/hook/orm_hud.*`, `coop_hud_*`, `coop_map*`, `orm_quick_chat.*`, `lichii/patches/0100`, `0101` | Confirmado en partida: `comunicacion-aqui`, `mapa-flecha`, `cartera-cada-uno`. Ajustes del HUD: todavía no | `git cherry-pick ee889ccd` |
| [11-puppets](docs/orm/11-puppets.md) | Los compañeros se mueven suaves y muestran lo que llevan, efectos, máscaras, ocarina, caballo y suelo que se mueve; el aldeano con el que hablan se mueve para todos | `LICHII_ORM_SMOOTH=1`, `LICHII_COOP_OCARINA_SOUND=1`, `LICHII_ORM_HORSE=1`, `LICHII_COOP_NPC_POSE=1` | `coop/host/{pose_smooth,epona,ride,worn_armor}.*`, `lichii/hook/coop_{carry,effects,epona,mascaras,npc_*}.*`, `orm_ride.*` | Confirmado en partida: `levantar-vasija`, `ocarina-mano`, `efectos-cofre`, `charla-dos`. Lo demás: todavía no | `git cherry-pick f7affb19` |
| [12-bodies](docs/orm/12-bodies.md) | Tu Link se para contra un compañero y puede subirse a su cabeza; nadie se queda atascado en un mismo sitio de Continuar | `/choque` (sala, encendido); `LICHII_ORM_BODIES=1` | `coop/host/orm_bodies.*`, `lichii/hook/coop_bodies.*`, `tools/puppet_pack/build.py` | Confirmado en partida, test10: `choque-bloqueo`, `choque-encima`, `choque-continuar` | `git cherry-pick 9313f77f` |
| [13-resume](docs/orm/13-resume.md) | Un jugador que vuelve regresa a su propia última escena y sitio; en una mazmorra, a su sala de entrada | `LICHII_ORM_RESUME` (encendido) | `lichii/hook/coop_resume*` | Caso de mazmorra: confirmado en partida, test10 (`reanudar-mazmorra-1`, `-2`). Fuera de mazmorras: todavía no | `git cherry-pick 9f84a767` |
| [14-app](docs/orm/14-app.md) | Una pantalla cooperativa dentro del emulador (estado, preparar, jugar) que habla con cualquier ayudante por líneas JSON | `orm-app.ini` junto al exe (sin él: apagado) | `lichii/app/*`, `lichii/patches/0102-orm-app.patch`, `lichii/hook/orm_app_channel.*` | Todavía no | `git cherry-pick ee22f807` |
| [15-tools](docs/orm/15-tools.md) | Nada en el juego; comprobación de alojamiento en el kit, medidor de rendimiento, órdenes de prueba, el arnés de dos juegos y partidas sintéticas | `LICHII_ORM_PERF` (apagado) | `lichii/hook/coop_perf.*`, `coop_testinput.*`, `tools/autotest/scenarios/`, `tools/orm/` | Órdenes de prueba: usadas en todas las pruebas en verde. Medidor de rendimiento: todavía no | `git cherry-pick 2295d962` |

## Cómo tomar algo

```
git remote add sbd https://github.com/S-BlackDragon/oot-remake-multiplayer
git fetch sbd regalo-test10
git cherry-pick <commit>          # por ejemplo 01-platform y 02-network primero, luego la función
```

O simplemente lee el código y la página de `docs/orm/` y hazlo a tu manera. No se espera nada de ti: este fork es donde
seguiremos añadiendo nuestro trabajo con el tiempo, igual (un commit por función, la misma limpieza), para que lo mires
cuando quieras.
