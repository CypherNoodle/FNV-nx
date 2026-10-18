# Memoria virtual guest y servicios Windows

El modulo propio `guest_memory.c/.h` gestiona reservas y paginas de datos
dentro de un pool respaldado por memoria host. `windows_memory.c/.h`
expone VirtualAlloc, VirtualFree, VirtualQuery y VirtualProtect al codigo
x86 recompilado mediante el runtime CPU y el contexto Windows por hilo.
No requiere DLLs copiadas ni ejecuta codigo x86 dinamico.

## Contrato del pool

El llamador suministra backing host alineado a 4 KiB, direccion guest base
alineada a 64 KiB, tamano multiplo de pagina y limite explicito de commit.
El pool ocupa un rango del mismo arena plano que usan los accesos MEM del
SDK. No es compatible con un mapper paginado que cambie esa representacion.
El proceso Windows comprueba la correspondencia exacta y evita solapamientos
con imagen, PEB, TEB, expansion TLS, pila y vector TLS estatico declarado.

Las reservas tienen base de 64 KiB y extremos redondeados a 4 KiB; el commit
opera por paginas de 4 KiB y comprueba la reserva y el presupuesto completos
antes de cambiar permisos. Solo se admiten NOACCESS, READONLY y READWRITE.
Paginas nuevas quedan a cero; recommit conserva los datos ya comprometidos
y aplica la proteccion solicitada. RELEASE requiere base original y tamano
cero. DECOMMIT conserva la reserva; recomprometer sus paginas vuelve a cero.
No se permiten operaciones que crucen dos reservas contiguas distintas.

VirtualQuery serializa los siete DWORD de MEMORY_BASIC_INFORMATION32:
28 bytes independientemente del ABI host. Agrupa hacia delante por reserva,
estado y proteccion desde la pagina consultada. El bridge solo conoce el
pool: consultar la imagen, pila u otro rango detiene la ejecucion con un
diagnostico de funcion pendiente, sin inventar que ese rango esta libre.

Los cuatro exports usan argumentos y retorno x86 stdcall, conservan LastError
en exito y lo actualizan al fallar. Tres nombres aparecen en la IAT normal
del Steam examinado; VirtualProtect queda disponible para un futuro resolver
dinamico. Junto a los ocho servicios TLS/IDs son once slots normales con
bridge, de 280; los 269 restantes y las llamadas dinamicas siguen pendientes.
No se permite enlazar parcialmente la IAT del juego.

## Permisos y transacciones

`memory_horizon.c` implementa un backend eager con permisos reales. Su
adaptador Switch llama a `svcSetMemoryPermission`; el host de pruebas usa
VirtualProtect de Windows. El backing completo pertenece al llamador y
debe admitir reproteccion. **Decommit no devuelve RAM al kernel**: es un
primer backend acotado, no un asignador sparse ni un presupuesto final del
juego. El commit logico tampoco mide toda la RAM fisica del proceso.

El backend cambia rangos completos, agrupa paginas nuevas al inicializarlas
y agrupa los permisos antiguos si necesita restaurarlos. Un fallo no altera
los metadatos ni los datos anteriormente comprometidos. Si la restauracion
falla, el gestor queda invalidado y el runtime CPU se detiene; no continua
con permisos incoherentes. Destruir el gestor restaura RW al backing antes
de devolverlo; si falla, el llamador conserva el gestor para reintentar.

Un mutex bloqueante serializa metadatos y operaciones de permisos, incluido
el borrado de regiones grandes: libnx Mutex en Switch, SRWLOCK en Windows
y pthread mutex en otros hosts. El callback no puede reentrar, lanzar
excepciones ni salir por longjmp mientras mantiene ese bloqueo. No se
mantiene durante la ejecucion guest ordinaria. El llamador sincroniza la
vida de las regiones que usan otros hilos; comprobar un puntero no lo fija
frente a una liberacion concurrente. Destruir requiere ausencia de usuarios
concurrentes y de referencias de procesos Windows.

## Limites explicitos

El [heap de proceso opcional](native-windows-heap.md) usa un arena separado del
pool Virtual*. No se implementan PEB completo, SEH, guard/COW, memoria ejecutable,
MEM_RESET, TOP_DOWN, grandes paginas, archivos mapeados o paging. Los flags
no soportados fallan, sin simular exito. VirtualProtect rechaza con error 50
que su puntero de salida comparta paginas con el rango a proteger. Las salidas
del bridge se comprueban contra los permisos del pool; fuera de el solo se
comprueban los limites del arena bootstrap del CPU.

Los accesos MEM generados siguen siendo accesos directos. Un fallo host de
proteccion no se convierte aun en una excepcion Windows guest. El VEH de las
pruebas solo observa ese fallo; no es un crash handler del port. Tampoco se
ha probado este backend de permisos en hardware Switch ni dimensionado el
pool del motor real.

## Verificacion reproducible

```sh
python pc/tests/test_guest_memory.py --output /privado/vm-host --cc /ruta/zig cc
python pc/tests/test_windows_memory.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --output /privado/windows-memory --cc /ruta/zig cc
```

El oraculo requiere Windows y solo asigna memoria del test: compara 1.600
operaciones aleatorias y 25.609 consultas contra sus API, ademas de reservar
200 MiB y comprometer 85 MiB. Dos hilos completan 500 ciclos de asignacion,
escritura, proteccion y liberacion sin compartir sus reservas. Se inyectan
fallos del backend, incluida una restauracion rota. Procesos hijos confirman
violaciones reales al leer NOACCESS o escribir READONLY.

La prueba de bridges traduce ocho rutinas x86 sinteticas con el SDK real,
verifica pila/IAT/MBI32/LastError y accesos MEM con los permisos efectivos.
Comprueba dos paradas estrictas y dos violaciones de acceso generadas. La
regresion TLS mantiene sus 2.000 rondas y 1.088 slots. Ninguna de estas
pruebas ejecuta el motor, acredita el cold-entry ni mide FPS.

CMake agrega `fnv_guest_memory` y, con FNV_X86_SDK, el target
`fnv_native_windows_memory`. Para la integracion ARM64 de cuatro unidades,
usar [build_windows_thread.py](../tools/build_windows_thread.py) con
`--memory-exports`; verifica antes/despues los inputs, los 348 objetos CPU
reutilizados y las bibliotecas de plataforma. No genera un ejecutable final.

## Fuentes de contrato

[VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc),
[VirtualFree](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree),
[VirtualQuery](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery),
[VirtualProtect](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect)
y [MBI32](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-memory_basic_information)
de Microsoft; permisos admitidos por [libnx](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/kernel/svc.h)
y restricciones de [SetMemoryPermission](https://switchbrew.org/wiki/SVC#SetMemoryPermission).
