# Límites: lo que podemos y no podemos hacer, y dónde estamos

[English](../LIMITS.md) | **Español**

Soy S-BlackDragon. Conozco bien Ocarina of Time, juego tu cooperativo con mis amigos y **no soy programador**: trabajo
con agentes de IA ("nosotros" en estos documentos somos ellos y yo). Esta página dice con honestidad qué permite nuestro
equipo, por qué probar es lento y hasta dónde hemos llegado.

## Nuestro equipo

| Qué | Detalle |
|---|---|
| Un PC de casa | Ryzen 9 7900X (24 hilos), 31 GB de RAM, RTX 4060 Ti de 8 GB |
| Se usa para | todo: compilar, probar, y mi uso y mis partidas de cada día |
| Otras máquinas | una más que corre otros servicios en producción; nunca la usamos para compilar ni para juegos |
| Nube | ninguna (miramos máquinas virtuales con GPU y descartamos la idea) |

## Por qué validar es lento

- **Cada prueba necesita dos juegos ocultos a la vez.** Una pareja ocupa unos 14-15 GB de RAM y de 10 a 14 núcleos. Así
  que en este PC **solo cabe una pareja**: con una pareja en marcha, la RAM libre baja de 1 GB. Dos parejas a la vez, o
  una pareja y una compilación, no caben.
- **Cada escenario tarda de 5 a 10 minutos** (arrancar el servidor y dos juegos, cargar, jugar el guion, leer los
  registros).
- **Una pasada completa son unas 50 horas.** Nuestro plan necesita unos 429 escenarios; a ese ritmo, una pasada
  completa son unas 50 horas de corridas seguidas. Las instancias calientes (una pareja juega una cadena de escenarios)
  ayudan, pero no en un orden de magnitud.
- **Las pruebas paran mientras juego.** Es el mismo PC: un archivo `STOP` lo pausa todo cuando me siento a jugar.
- **A algunos momentos no se llega con partidas sintéticas** (Epona, la huida final, algunos estados de jefe). Esos
  solo aparecen jugando de verdad.
- **Seis jugadores solo se prueban con bots** (un juego más cinco jugadores falsos). Las partidas reales de seis
  dependen del tiempo libre de mis amigos.
- **El análisis y los arreglos también tienen límite.** Trabajo con agentes de IA dentro de un presupuesto de uso, así
  que el tiempo para leer registros y arreglar cosas es limitado, no solo el tiempo de máquina.

## Dónde estamos (medidor de avance, 2026-11-13)

De nuestro medidor de avance `avance.py` (no va en este regalo: lee nuestras notas privadas), ejecutado el 2026-11-13. El medidor cuenta 145 casillas (20 bloques de contenido,
hasta 8 casillas cada uno; ver [HOW-WE-WORK.md](HOW-WE-WORK.md)).

| Cifra | % | Casillas |
|---|---|---|
| **Verificado en partida** (escenarios en verde con todas sus comprobaciones, dos juegos reales) | **1,3 %** | 1,9 de 145 |
| **Código listo** (integrado y documentado, aún sin jugar) | **51,6 %** | 74,8 de 145 |
| Esperando un valor de una partida (integrado, pero necesita el valor de una sonda) | 6,7 % | 9,7 de 145 |
| **Escenarios escritos** (existen y tienen comprobaciones `#! expect`) | **81,7 %** | 118,4 de 145 |

En escenarios: **10 en verde de los 429 necesarios.**

Es decir: la mayor parte del código existe, la mayor parte de los escenarios existen, y muy poco ha pasado en una
partida real. Lo que falta es tiempo de pruebas, no ideas.

## Tiempo estimado hasta un cooperativo probado al 100 % de punta a punta

A este ritmo, unas **8 a 12 semanas**. Lo limita sobre todo el ritmo de pruebas (una pareja a la vez, unas 50 horas
por pasada completa) y arreglar las familias de fallos que destape la primera pasada completa. Revisaré esta
estimación después de esa primera pasada. Las partidas reales con amigos lo aceleran, porque llegan a momentos que los
guiones y las partidas sintéticas no alcanzan.

## Qué lo aceleraría (para quien lo lea)

- **Más RAM, o un segundo PC**, para que dos o más parejas de juegos corran en paralelo.
- **Jugadores reales que informen con registros**: un registro de una partida real muestra en minutos lo que a un
  guion le puede costar días alcanzar.
