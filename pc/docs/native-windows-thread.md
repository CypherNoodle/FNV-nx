# Servicios Windows por hilo

`windows_thread.c/.h` implementa ocho servicios TLS/IDs de `kernel32.dll` sobre el
runtime CPU nativo. Usan el frame x86 traducido y tokens de la IAT, sin DLLs
Windows ni Wine. Las implementaciones son codigo propio.

Tambien incorpora ocho exports de [heaps opcionales](native-windows-heap.md).
Agrega doce exports de [secciones criticas y atomicos](native-windows-sync.md).
Las cifras historicas de ocho servicios y slots de esta pagina se refieren a
TLS/IDs; no incluyen los nuevos exports de heap ni Virtual* o D3D9.

| Import | Comportamiento |
| --- | --- |
| TlsAlloc | Reserva un indice de proceso e inicializa sus valores por hilo a cero. |
| TlsFree | Libera el indice y limpia su valor en los hilos vivos. |
| TlsGetValue | Lee el valor del hilo; limpia LastError al consultar un indice valido, incluso cuando el valor es cero. |
| TlsSetValue | Guarda un valor opaco de 32 bits y conserva LastError en caso de exito. |
| GetLastError / SetLastError | Lee o escribe LastError en el TEB guest. |
| GetCurrentThreadId / GetCurrentProcessId | Devuelve el ID guest suministrado al configurar el proceso/hilo. |

Los ocho nombres aparecen en el EXE Steam examinado: ocho de sus 280 slots
normales tienen ahora estos bridges; otros 272 siguen pendientes. No es un
porcentaje de trabajo restante: faltan imports dinamicos, callbacks y
vtables. La IAT real se sigue rechazando completa, sin modificaciones
parciales ni funciones vacias para el resto.

## TLS y memoria guest

Se soportan 64 slots inline y 1.024 de expansion. Los valores viven en la
memoria guest y siguen siendo de 32 bits: no se convierten en punteros ARM64.
El llamador preasigna una pagina TEB y otra de expansion por hilo. Esto
evita depender de HeapAlloc guest, pero no reproduce la asignacion lazy
de Windows ni sus fallos de heap.

Get/Set realizan la validacion minima del rango numerico, sin exigir un
bit de reserva activo. TlsFree si comprueba la reserva. Un indice invalido
falla con error 87; al agotarse los indices, TlsAlloc devuelve 0xFFFFFFFF
y error 259. No recicla un indice ocupado para simular exito.

El TEB32 inicializa estos campos compartidos entre las API y los accesos FS:

| Offset FS | Campo |
| --- | --- |
| 0x00 | Final inicial de cadena SEH (0xFFFFFFFF), sin implementar SEH. |
| 0x04 / 0x08 | Limites superior e inferior de pila. |
| 0x18 | Direccion guest del propio TEB. |
| 0x20 / 0x24 | IDs guest de proceso e hilo. |
| 0x2C | Vector TLS estatico opcional, preparado por el llamador. |
| 0x30 | PEB preparado por el llamador. |
| 0x34 | LastError. |
| 0xE10 | Los 64 slots TLS inline. |
| 0xF94 | Expansion TLS preasignada. |

Este TEB guest es diferente del TLS de libnx que conserva los registros de
CPU host. El modulo no inicializa el PEB completo ni el directorio TLS del
PE, sus plantillas o callbacks. Conservar FS:0x2C no demuestra que se hayan
ejecutado; los otros campos del TEB tampoco estan implementados.

## Ciclo de vida

`fnv_windows_create` recibe el arena, configuracion CPU, tamano de imagen,
ID de proceso y pagina PEB preparada. Posee el runtime CPU. Su funcion
bind_imports combina los ocho exports con otros exports explicitos del
llamador y conserva el enlace atomico; las colisiones se rechazan.

La configuracion puede asociar un gestor `memory` de
[memoria guest](native-windows-memory.md). El proceso conserva una referencia
hasta destruirse; su backing debe coincidir exactamente con el offset del
arena CPU y no solaparse con imagen, PEB ni reservas de los hilos. NULL
mantiene disponible el modulo TLS sin habilitar las API Virtual*.

El accessor prestado `fnv_windows_runtime` permite preparar modulos COM
antes del attach; no transfiere propiedad ni reemplaza ese ciclo de vida.
El proceso no puede destruirse hasta retirar los metodos nativos del CPU.

Cada hilo host usa `fnv_windows_thread_attach` con estado CPU, ID, TEB,
expansion y pila propios. El vector TLS estatico opcional declara tambien
su tamano completo. Se rechazan rangos fuera del arena, solapamientos con
imagen/PEB u otros hilos e IDs vivos duplicados. Rechazar una reserva no
borra memoria ni adjunta un hilo CPU.

Son reservas en memoria ordinaria: no hay guard pages, commit/decommit ni
VirtualAlloc. El propietario debe reservar tambien los bloques a los que
apunta un vector TLS estatico; el modulo valida el vector declarado, sin
deducir el tamano de sus bloques.

Terminar con `fnv_windows_thread_detach` y destruir el proceso cuando todos
los hilos se hayan separado. No mezclar este ciclo con cpu_attach/detach
directos. No se implementa CreateThread/ExitThread, cancelacion asincrona,
prioridades o afinidad.

Un bloqueo breve protege reservas, indices y reciclado, sin bloquear la
ejecucion guest. Se valida antes de tomarlo cualquier operacion que pueda
salir por diagnostico. El programa debe sincronizar la liberacion de un
indice utilizado por otros hilos; esto no reemplaza eventos o critical sections.

## Pruebas e integracion

La prueba traduce 20 rutinas x86 con el SDK real, incluidas llamadas IAT y
accesos FS. Comprueba los 1.088 slots, valores con bit alto, reciclado,
hilos nuevos, pila stdcall y LastError. Dos hilos completan 2.000 rondas y
verifican el reciclado mientras ambos siguen vivos. Se rechazan 15 reservas
invalidas y cuatro conflictos con otro hilo. Dos fallos de contexto
comprueban que la salida estricta no deje un bloqueo tomado.

En Windows pasan once comparaciones contra TlsGetValue/TlsSetValue reales,
incluidos cero y LastError. Estos hilos host no acreditan CreateThread
guest. El EXE Steam opcional se lee como datos, con hash antes/despues y
sin modificar su IAT.

```sh
python pc/tests/test_windows_thread.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --output /privado/prueba-windows-thread \
  --cc /ruta/zig cc --game /ruta/Fallout-New-Vegas
```

CMake agrega `fnv_native_windows_thread` al proporcionar FNV_X86_SDK.
La herramienta siguiente comprueba por hash los inputs y todos los objetos
del lote CPU aceptado, compila el modulo y su gestor VM con las mismas opciones
Switch y realiza el enlace relocatable. No acepta un lote modificado.

```sh
python pc/tools/build_windows_thread.py \
  --base-validation /privado/cpu-runtime-switch/revalidation.json \
  --analysis /privado/analisis-derivado --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias \
  --libnx /ruta/libnx \
  --platform-validation /privado/cpu-runtime-switch/platform-symbols.json \
  --cc /ruta/aarch64-none-elf-gcc --ld /ruta/aarch64-none-elf-ld \
  --nm /ruta/aarch64-none-elf-nm --objdump /ruta/aarch64-none-elf-objdump \
  --output /privado/windows-thread-switch
```

La integracion anterior de TLS sumaba 349 unidades. Ahora el gestor de
memoria es una dependencia adicional: el modo predeterminado agrega dos
unidades a las 348 de CPU; `--memory-exports` agrega cuatro e incluye el
backend de permisos Horizon y Virtual*. Se comprueban las definiciones de
los externos en las bibliotecas de plataforma identificadas por hash.
Los objetos derivados siguen privados; este enlace no es un NRO ni prueba
arranque o rendimiento del motor.

## Referencias de contrato

Se contrasto con [TLS de Microsoft](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-local-storage),
[TlsAlloc](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsalloc),
[TlsGetValue](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsgetvalue),
[TlsSetValue](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlssetvalue) y
[TlsFree](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-tlsfree).
El layout x86 y los casos limite se contrastaron con
[TEB32](https://github.com/wine-mirror/wine/blob/master/include/winternl.h) y
[TLS de Wine](https://github.com/wine-mirror/wine/blob/master/dlls/kernelbase/thread.c),
sin incorporar su codigo ni DLLs.
