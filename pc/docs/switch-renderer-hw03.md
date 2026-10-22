# Prueba agrupada pc-renderer-hw03

Esta revision lleva a Mesa/NVK en Switch la nueva ruta de render a textura.
Sigue siendo un diagnostico sintetico: no arranca Fallout ni necesita sus
archivos. El NRO contiene solo callers y shaders propios de prueba.

Copiar `fnv-nx.nro` a `sdmc:/switch/FNV-nx/fnv-nx.nro`, reemplazando el anterior.
No hace falta copiar ninguna otra carpeta. Abrir en modo aplicacion, como las
pruebas anteriores. Durante el inicio muestra azul y ejecuta diez grupos fuera
de pantalla. Luego muestra las fases azul, roja, verde y azul; + permite salir.
El registro se reemplaza en cada inicio:
`sdmc:/switch/FNV-nx/fnv-pc-renderer-hw03.log`.

Los nueve grupos anteriores conservan Clear, BC1/2/3 con mips y alpha,
DrawPrimitiveUP/DrawIndexedPrimitiveUP, FVF, state blocks y Reset. El grupo nuevo
crea una textura GPU y obtiene su superficie por COM. Libera la referencia
publica del padre y la recupera por GetContainer, verificando su identidad.
Renderiza rojo a esa superficie y la muestrea en el target siguiente. No hay
readback ni espera del host entre ambas pasadas; el watchdog espera fences
solo si se llena el ring. Se comprueban los pixels finales y la liberacion
de memoria/recursos. El mismo codigo de checks corre primero en escritorio.

En total: 105 callers x86 sinteticos traducidos, diez grupos, once draws y
11.264 pixels comprobados, ademas de las fases visuales y Present. Un control
negativo de escritorio sustituye la textura renderizada por la fuente blanca
y debe fallar por pixels incorrectos. El patron evita valores a mitad de un
nivel UNORM para que el redondeo no produzca falsos fallos.

El constructor exige una prueba de escritorio aceptada y verifica los hashes
del catalogo, callers generados, PE sintetico, shaders, fuentes y headers
nativos. Congela los inputs de compilacion y verifica el ELF/NRO resultante.
El registro distingue cada `combined BEGIN`; `combined PASS` exige todos los
grupos. `END result=PASS` requiere completar las fases y destruir el proveedor.

No modifica clocks ni mide FPS del juego. El hardware todavia debe validar
esta revision: una compilacion ARM64 y los pixels de escritorio no demuestran
su ejecucion en Switch. El bloqueo conocido del arranque del motor por caps
HWTL/shaders no cambia con este diagnostico.

Validacion local de escritorio: `local/renderer-checks-rt03`, ambos modos y
el control negativo aceptados. Build: `local/pc-renderer-hw03-build01`, ELF
AArch64/DYN, sin simbolos fuertes pendientes, NRO con segmentos verificados.
NRO SHA256: `56de407310a2b325c238291593d6112d5ff2876f7d13af9e99092c1d1f08f758`.
Enviar el log hw03 tras completar las fases y salir con +; no hace falta
repetir las pruebas anteriores por separado.
