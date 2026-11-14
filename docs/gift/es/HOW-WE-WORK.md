# Cómo trabajamos

[English](../HOW-WE-WORK.md) | **Español**

Este es nuestro método en palabras sencillas. "Nosotros" somos yo (S-BlackDragon) y los agentes de IA con los que
trabajo. Las herramientas que se nombran aquí están en este regalo, bajo `tools/orm/`.

| Herramienta | Dónde | Qué |
|---|---|---|
| Arnés de pareja | `tools/orm/harness/r03_pareja.py` | corre un escenario con dos juegos ocultos y escribe un resumen |
| Tandas | `tools/orm/harness/secuencia.ps1` | corre una lista de escenarios uno tras otro, una línea de resumen cada uno |
| Instancias calientes | `tools/orm/harness/caliente.py` | una pareja de juegos juega una cadena de escenarios sin reiniciar |
| Lint de escenarios | `tools/orm/harness/lint_escenarios.py` | revisa todos los escenarios sin juego |
| Escenarios | `tools/orm/harness/escenarios/` | `<nombre>.A.txt` y `<nombre>.B.txt` |
| Partidas sintéticas | `tools/orm/saves/` | `partida.py` y las recetas de `recetas/` |
| Medidor de avance | `avance.py` (en nuestro repositorio: lee nuestras notas privadas) | cuenta lo verificado en partida, solo a partir de los resúmenes |
| Comparación de plataformas | `tools/orm/plataforma/comparar.py` | la misma ruta en Hookshot y en el emulador |

Algunos nombres de archivos y de directivas están en español (`escenarios`, `partida`, `receta`, `caliente`, `avance`).
El código y las líneas de registro están en inglés.

## 1. Dos juegos ocultos por prueba

Cada prueba es un **escenario**: dos juegos (A y B) en el mismo PC, cada uno con su carpeta de instancia (su propia
copia de la carpeta de usuario del emulador, partidas y `coop.ini`), un servidor local en 127.0.0.1 y las ventanas
ocultas (nunca roban el foco ni suenan). Cada juego lee un guion pequeño por la entrada de pruebas del gancho: andar,
pulsar botones, viajar (`warp`), colocar a Link, escribir en el chat, escribir una bandera, y `mark <nombre>` para
cortar su registro en partes.

El arnés arranca el servidor y los dos juegos, espera a que los dos hayan entrado y cargado, deja correr los guiones, y
luego lee los dos registros de juego y el del servidor y escribe `<nombre>_resumen.txt` con una línea por comprobación y
una última línea `RESULT: PASS` o `RESULT: FAIL (...)`. Nadie tiene que mirar los juegos: el registro es el juez. Se
toman capturas en las marcas (`--capture-marks`) para lo que solo los ojos pueden juzgar.

En mi PC solo corre una pareja a la vez (un archivo de cerrojo), y nada arranca mientras existe un archivo `STOP` (estoy
jugando).

Un ejemplo corto (`choque-encima`, recortado):

```
# choque-encima.A.txt
#! expect A coop: bodies: colliders on within start..apart
#! expect A bodies: our Link stands on P\d's head within drop..end
#! never A coop: check: bodies: collider fallback
mark start
wait 240
goto -82.0 30.6 0.4 600
mark apart
mark drop
place -82.0 124.93 33.65
wait 180
mark end
```

## 2. Directivas de los escenarios (líneas `#!`)

Las líneas de cabecera `#!` dicen lo que tiene que pasar. La lista exacta es la que conoce `lint_escenarios.py`.

| Directiva | Significado |
|---|---|
| `#! expect A\|B <regex> [within <marca>..<marca>]` | el registro de ese juego debe tener una línea que encaje, dentro de esa parte del registro |
| `#! never A\|B <regex> [within ...]` | el registro de ese juego no debe tener nunca una línea así |
| `#! count A\|B <regex> >=\|<=\|== N [within ...]` | el número de líneas que encajan debe ser el dicho |
| `#! noload` | el guion carga la partida él mismo (sin `load.txt`) |
| `#! start <regex>` | el escenario cuenta como empezado cuando el registro encaja (p. ej. `coop: test warp done`) |
| `#! switches K=V,K=V` | interruptores del gancho para este escenario (los de la tanda ganan) |
| `#! env K=V` | un interruptor más del gancho (se puede repetir) |
| `#! timeout N` | segundos antes de cortar la corrida |
| `#! saves <bloque>\|<carpeta>\|default` | con qué partida empieza ese juego (un bloque sintético como `12-bosque`) |
| `#! players 1\|2` | cuántos juegos |
| `#! bots N` | jugadores falsos (de 0 a 5) después de que carguen los juegos |
| `#! bot-arg ARGS` | más argumentos para el bot (se puede repetir) |
| `#! transport ws\|udp` | cómo llegan los juegos al servidor |
| `#! join A-first\|B-first` | el segundo juego solo arranca cuando el primero ha entrado |
| `#! puppets` | FAIL si un juego muestra menos marionetas que jugadores hay |
| `#! min-fps N` | FAIL si la mediana de fps de un juego desde su primera marca baja de N |
| `#! caliente no [razón]`, `despues frio`, `antes <línea de guion>`, `stagger N` | opciones de instancias calientes (sección 4) |

`within boot..` significa "desde el arranque del juego", para las líneas que el gancho escribe antes de que corra ningún
guion.

**El lint.** `lint_escenarios.py` corre sin juego (el tiempo de juego es el recurso escaso). Lee cada línea `#!`,
compila cada expresión regular, comprueba cada orden del guion contra el lector de guiones del gancho, cada `warp`
contra la lista de escenas del romfs, cada marca de `within` contra las marcas que escribe el guion, y cada nombre de
interruptor contra el código. Lo más importante: comprueba que **cada regex de `expect` puede encajar con una línea de
registro que el código escribe de verdad** (lee cada cadena literal del gancho como un formato y pasa la regex por
encima). Una errata en una línea esperada se encuentra antes de una corrida de 10 minutos.

**Mutantes.** Un buen escenario también tiene que fallar cuando la función está apagada. Por eso la mayoría de los
escenarios nombran un mutante: la misma pasada con el interruptor de la función apagado (o una línea clave borrada) debe
salir en rojo.

## 3. Partidas sintéticas

Para probar la segunda mitad del juego hace falta una partida guardada allí. Una **receta**
(`tools/orm/saves/recetas/<bloque>.txt`) lista nombres de banderas y valores: objetos, canciones, pasos de la
historia, eventos ya vistos, edad, corazones, mazmorras hechas, equipo y el punto de inicio. Cada receta incluye la
anterior, así que `20-ganon` lleva todo. `partida.py` escribe la receta sobre una partida real **tuya**, vuelve a leer
cada ranura del disco y comprueba que cada valor está y que nada más cambió, byte a byte. En el regalo solo están las
recetas; las partidas se construyen en cada PC y nunca se comparten.

```
python tools/orm/saves/partida.py check     # cada receta contra el romfs (la bandera existe, tipo, valor de enumerado, punto de inicio)
python tools/orm/saves/partida.py all       # construye todos los bloques en unos segundos
```

Un escenario dice `#! saves 13-fuego` y el arnés construye esa partida (o la reconstruye si la receta cambió) antes de
arrancar los juegos. Una partida de verdad siempre gana a una sintética. Límites: a algunos momentos aún no se llega así
(Epona ganada, algunos estados de jefe; la huida final necesitó su propia receta `21-huida-arriba`); esos solo aparecen
jugando de verdad.

## 4. Instancias calientes (`caliente.py`)

Arrancar una pareja de juegos cuesta minutos. `caliente.py` agrupa los escenarios en **familias** (mismos juegos,
transporte, interruptores y partida), arranca la pareja una vez y juega seguidos los escenarios de la familia. Antes de
cada escenario deshace lo que escribió el anterior (banderas, enteros, vida, botellas) y viaja a otra escena, para que
el `warp` del escenario recargue de verdad. Cada escenario sigue teniendo su propio resumen, sacado de su parte de los
registros.

Un estado roto (un juego cerrado, un `warp` que no se toma, ninguna marca nueva a tiempo) cierra la pareja y el
escenario se juega **en frío** (juegos nuevos). Un FAIL caliente con los juegos sanos también se repite en frío, y el
resultado que cuenta es el frío: un estado heredado nunca da un aprobado. La pareja se reinicia tras matar a un jefe,
un evento de historia compartido, un Link caído, o cuando un registro crece demasiado.

## 5. Los interruptores están apagados hasta que una partida los confirma

Cada función nueva tiene un interruptor (`LICHII_ORM_*`; un solo lector acepta `0/1/true/false/yes/no/on/off`). Está
**apagado en las opciones del kit** hasta que un escenario con dos juegos reales lo muestra funcionando; las sondas lo
encienden para sí mismas. Una función que solo pasó pruebas unitarias está "sin confirmar en partida" y nunca llega
encendida a los amigos.

## 6. Cada camino arriesgado tiene una reserva y una línea de registro

Cuando el código depende de algo que no pudimos leer del juego de antemano, tiene tres cosas: un camino robusto (medir
el valor real con el juego en marcha), un camino de reserva, y **una línea de autocomprobación** que dice qué camino
corrió, una vez por cosa y camino:

```
coop: check: <cosa> ok <qué se midió o leyó>
coop: check: <cosa> fallback <por qué, y qué se usa en su lugar>
```

Los escenarios comprueban estas líneas: `ok` es el buen resultado; `fallback` pone el escenario en rojo y dice por qué.
Así un riesgo nunca es una nota en un documento: es código, una reserva y una línea que dice por dónde fue. Cuando un
arreglo depende de un valor del juego, escribimos una **sonda** corta (dos juegos, unos minutos) que registra el valor,
ponemos el valor en el código y corremos el mismo escenario otra vez para confirmarlo.

## 7. Trabajar por familias de fallos

Una tanda da muchos escenarios en rojo. No los arreglamos uno a uno. Primero leemos solo los registros (sin juego) y
agrupamos los rojos por causa en familias, por ejemplo: el segundo juego cargó la partida equivocada; el segundo juego
arrancó tarde y una espera fija era demasiado corta; una comprobación miraba en la parte equivocada del registro; el
movimiento del guion agotó su tiempo; el PC se quedó sin RAM; un momento al que las partidas sintéticas no llegan.
Luego cada familia se arregla una vez, para todos los escenarios que toca, y la tanda se vuelve a correr. Un rojo nunca
se acepta porque pasó en un segundo intento.

## 8. El medidor de avance (`avance.py`)

Nuestro `avance.py` (no va en este regalo: lee nuestras notas y ramas privadas) solo lee archivos (los resúmenes de los escenarios, los escenarios, git) y escribe una
página de avance. Cada uno de los 20 bloques de contenido del juego tiene hasta 8 casillas:

| Casilla | Qué pide |
|---|---|
| M | marionetas y enemigos: los dos jugadores se ven; los enemigos tienen un dueño y la misma vida y muerte |
| D | mundo y mazmorra: interruptores, puertas, llaves, agua y cofres llegan vivos al otro juego |
| J | jefe: entrar juntos, caído y revivido dentro, derrota grupal, entrada tardía, la muerte vista por los dos |
| C | cinemáticas: las de historia a la vez, y el mismo mundo para quien no las vio |
| P | charlas: el que habla reclama al aldeano, nadie se oculta |
| R | un recorrido entero de la zona o mazmorra con dos juegos |
| 6 | lo mismo con seis (un juego más cinco bots), y una vez con seis personas |
| A | visto por mí: capturas o una partida real con amigos |

Una casilla solo cuenta cuando sus escenarios **pasan con sus comprobaciones** en una pasada con dos juegos reales. El
medidor da cifras separadas: verificado en partida, código listo (integrado y documentado, sin jugar), esperando un
valor de una partida, y escenarios escritos. Las cifras de hoy están en [LIMITS.md](LIMITS.md).

## Contribuir, o mejor dicho: adónde va nuestro trabajo

Nuestro fork público, **github.com/S-BlackDragon/oot-remake-multiplayer**, es donde seguimos añadiendo nuestro trabajo
con el tiempo: un commit por función (los identificadores de función de este regalo), con la misma limpieza (sin rutas
de mi PC, sin servidores privados, sin datos del juego). Míralo cuando quieras, toma lo que encaje, cámbialo o déjalo.
No se espera nada de ti: ni revisión, ni fusión, ni respuesta.
