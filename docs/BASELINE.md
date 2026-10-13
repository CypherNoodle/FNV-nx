# Punto de partida

Estado verificado el 12 de octubre de 2026: referencia Windows del ejecutable
Xbox 360 recompilado. Menú original en español observado a los segundos 20 y
90, con fondos diferentes. Ocho capturas de 1280×720, ocho hashes de píxeles
distintos. La prueba termina su propio proceso al alcanzar 95.547 segundos;
esto no acredita una salida normal del juego ni una partida jugable.

Ejecutable de referencia observado (no incluido):
`bca731fbde1d14d696a0cc7a8139cb57058aaacbe1eee22f7ec125c1bc9dcb6b`.

El trabajador inicia antes de que existan ciertos objetos compartidos. La
barrera experimental libera ese trabajador antes del primer bucle, cuando
ambos objetos ya están creados. El control sin barrera reprodujo un acceso
nulo. La causa concreta en la sincronización del SDK sigue sin confirmarse.

Límites de función confirmados y conservados en tools/codegen.ps1:

| Dirección | Tamaño | Evidencia |
| --- | ---: | --- |
| 82D1B300 | 4 | Tail thunk observado desde dos callers |
| 828F0358 | 32 | Callback indirecto, final original blr |
| 82DC1948 | 136 | Comparador con CFG acotado y dos salidas |
| 82D39950 | 24 | Forwarding virtual; referencia 82120D9C, límite siguiente 82D39968 |

No se usa force ni se alteran los bytes del XEX. Los 68 avisos de pack vectorial
siguen siendo una limitación de validación. Hay errores de rutas Music/XMA y
avisos BaseHeap pendientes; no se han ocultado ni renombrado assets para taparlos.

Pendiente: navegación de menú, gameplay, audio real completo, integración
Horizon, renderer nativo, rendimiento y estabilidad en Switch. Las imágenes,
logs y salidas generadas permanecen privados fuera del historial Git.
