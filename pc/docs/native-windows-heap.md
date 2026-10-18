# Heap de proceso guest32

`windows_heap.inc`, incluido por `windows_thread.c`, incorpora ocho exports
propios: GetProcessHeap, HeapAlloc, HeapReAlloc, HeapFree, HeapSize, HeapValidate,
HeapCreate y HeapDestroy. Seis aparecen en la IAT normal del EXE Steam auditado.
Esto no completa los servicios Windows ni arranca el motor.

El host configura un arena RW ya comprometido dentro del backing CPU y separado
de imagen, PEB, pool VirtualAlloc, TEB, TLS y pilas. El presupuesto de bytes y
de bloques es explicito. Configuracion cero mantiene el modulo TLS sin heap;
invocar los exports del heap sin proveedor detiene la ejecucion con diagnostico.
El PEB32 ProcessHeap (offset 0x18) recibe el handle guest y se limpia al destruir
el proceso si aun conserva ese valor. No se publica un puntero host.

Las asignaciones tienen alineacion x86 de ocho bytes. Se conserva el tamano
solicitado, incluido cero; los huecos liberados se reutilizan. Realloc reduce o
crece in situ cuando cabe; en otro caso copia a otro bloque, salvo IN_PLACE_ONLY.
Un fallo conserva la asignacion original. ZERO_MEMORY borra solo los bytes
solicitados nuevos; nunca borra el prefijo existente durante el crecimiento.
Alloc y ReAlloc conservan LastError incluso al agotar el presupuesto. NULL en
HeapFree es un no-op exitoso; las direcciones interiores/liberadas no se aceptan.

Un mutex bloqueante de libnx/SRWLOCK/pthread protege los metadatos y la copia.
Los diagnosticos y validacion del contexto suceden antes de tomarlo. El programa
debe sincronizar el uso de sus propios bloques frente a frees concurrentes.
Los metadatos viven en host y no se escriben dentro de los buffers del juego.
HeapValidate comprueba pertenencia a la tabla de asignaciones; no detecta
escrituras del juego fuera de un bloque ni entrega excepciones SEH.

Es un primer asignador de presupuesto fijo: busqueda lineal de huecos y tabla
ordenada, no LFH, arena final del juego, paging ni optimizacion acreditada.
No implementa HeapWalk, HeapLock, informacion de heap, expansion del presupuesto
host ni HEAP_GENERATE_EXCEPTIONS. Los flags fuera
del subconjunto (ZERO_MEMORY, IN_PLACE_ONLY en realloc) detienen el runtime;
NO_SERIALIZE no se acepta para este heap de proceso.

## Heaps privados sobre memoria virtual

El host habilita `private_heap_reserve` (techo por heap) y `private_heap_limit`
(cantidad simultanea) junto al gestor `memory`. HeapCreate reserva un rango
independiente del pool Virtual*, con base de 64 KiB, y compromete paginas RW
reales: al menos 4 KiB o el initial size redondeado. Un maximo no cero se
redondea a pagina y limita la reserva; maximo cero permite crecer hasta el
techo host explicito. Pedidos fuera del presupuesto fallan, sin inventar RAM.
El heap fijo rechaza asignaciones de 0x7fff8 bytes o mas (limite x86).

Las asignaciones y realloc comprometen paginas adicionales antes de publicar
el bloque o modificar sus metadatos. Un fallo normal conserva los datos previos.
Si falla el primer commit, se libera la reserva nueva. Si falla su rollback o el
backend queda corrupto, se detiene el runtime despues de soltar el mutex. Los
flags de memoria ejecutable, GENERATE_EXCEPTIONS y NO_SERIALIZE se rechazan con
diagnostico estricto; no hay codigo x86 dinamico ni SEH simulado.

HeapDestroy invalida el handle y libera su reserva, incluso con bloques vivos;
no permite destruir el heap de proceso. Free, Size y Realloc validan pertenencia
al heap solicitado. No se prometen direcciones iguales a Windows ni handles
irreutilizables para siempre. El llamador no debe liberar o reproteger por su
cuenta las reservas de estos heaps mediante Virtual*.

La destruccion host `fnv_windows_destroy` exige retirar los hilos y destruir
primero los heaps privados; devuelve BUSY mientras haya heaps vivos. La memoria
virtual usa el backend eager: release revoca permisos y libera el rango logico,
pero no devuelve al kernel el backing fisico preasignado. No es un asignador
sparse ni una medida de memoria fisica final del juego.

## Verificacion

`test_windows_heap.py` genera ocho callers x86 sinteticos con imports por nombre
y los recompila mediante el SDK real. Verifica IAT y pila stdcall, PEB/handle,
600 secuencias de asignacion/realloc/copia comparadas con HeapAlloc/ReAlloc/Free
de Windows, 2000 ciclos en dos hilos, limites, reuso, fallos sin perder datos y
una parada estricta ante flags de excepciones no implementados. No compara las
direcciones ni exige que el asignador elija el mismo bloque que Windows.
La referencia Windows puede devolver mas bytes en HeapSize; este heap devuelve
el tamano solicitado. Se prueban ademas las regresiones TLS y Virtual*.

`test_windows_private_heaps.py` agrega HeapCreate/Destroy al PE sintetico y
ejecuta diez callers traducidos con permisos reales. Compara 500 secuencias con
un heap Windows y verifica 2000 ciclos concurrentes, creacion/destruccion en
otro hilo, limites, aislamiento, crecimiento, fallo de commit/release, limpieza
total de reservas y dos paradas estrictas (flags y backend irrecuperable).
La regresion del heap de proceso conserva sus 600 secuencias y 2000 ciclos.
Ambas bibliotecas Windows pasan la compilacion ARM64 Switch con -Werror.
No hay NRO de motor ni prueba hardware de estos heaps todavia.

```sh
python pc/tests/test_windows_heap.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --output /privado/heap --cc /ruta/zig cc
```

`audit_pc_dependencies.py --game /ruta/juego --output /privado/dlls.json` conserva
el grafo de imports normales del EXE y DLLs locales. No carga ni ejecuta DLLs,
no copia binarios y no demuestra que falte o sobre una dependencia dinamica.

Contratos primarios: [HeapAlloc](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc),
[HeapReAlloc](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heaprealloc),
[HeapFree](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapfree),
[HeapSize](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapsize).
[HeapCreate](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapcreate)
y [HeapDestroy](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapdestroy).
