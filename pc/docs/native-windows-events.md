# Eventos guest32 y handles de generacion

El runtime incorpora CreateEventA/W, SetEvent, ResetEvent, WaitForSingleObject y
CloseHandle para eventos **sin nombre, locales al proceso**. Cinco son imports
normales del EXE Steam auditado; CreateEventW queda disponible para otros callers.
No se cargan DLLs Windows en Switch. Son seis exports propios sobre el runtime.

El host configura `kernel_handle_capacity` antes del attach. Se preasignan tabla,
mutex y condiciones; cero deshabilita este proveedor. Hasta 65536 slots usan una
lista libre O(1), sin buscar desde el principio al crear cada evento. Los handles
guest32 contienen prefijo, indice y generacion; nunca contienen un puntero host.
Lookup es O(1). Al cerrar, un slot solo se reutiliza tras retirar sus waiters.
Una generacion agotada retira ese slot en vez de volver valido un handle viejo;
hay hasta 4095 creaciones por slot. Agotar capacidad/generaciones falla con
NULL y error 8. El presupuesto se define explicitamente, no es ilimitado.

## Senales y esperas

Un evento manual queda senalizado hasta ResetEvent; un automatico libera un
waiter o conserva una senal si no hay ninguno. Senales repetidas no acumulan
un contador cuando el evento ya esta senalizado. Los BOOL se normalizan y se
respeta el estado inicial.

Cada llamada bloqueada registra un waiter en la pila host. SetEvent concede
la senal retirando peticiones elegibles (una por senal automatica) de la lista
compartida del proceso. Una espera conjunta solo es elegible si cumple su condicion. Esa
concesion sobrevive a ResetEvent aunque el hilo no haya vuelto a ejecutarse.
Un nuevo waiter no puede robar la senal ya concedida a otro. WakeAll notifica
los waiters; cada uno verifica su concesion bajo mutex. Puede haber despertares
sin concesion; no se promete FIFO ni rendimiento definitivo del scheduler.

WaitForSingleObject soporta poll cero, timeout finito e INFINITE. Devuelve
WAIT_OBJECT_0 al adquirir, WAIT_TIMEOUT al agotar el plazo y WAIT_FAILED con
error 6 para handles invalidos/cerrados. Conserva LastError en las esperas
normales. Las condiciones liberan el mutex al dormir y lo recuperan al volver.

`host_clock.h` usa QPC en Windows, CLOCK_MONOTONIC en pthread y el contador
arquitectural y su frecuencia real en Horizon. La conversion a ns evita el
overflow de multiplicar directamente todos los ticks acumulados. El limite de
frecuencia admitido es UINT32_MAX Hz. No usa frecuencia CPU/GPU, guest RDTSC ni
presume que overclock cambie ese contador. No modifica los temporizadores del
juego ni implementa aun QueryPerformanceCounter guest o Sleep.

Los plazos se fijan una vez por llamada y se reevalua el tiempo restante tras
notificaciones espurias. Se usan esperas relativas de hasta 250 ms para detectar
un peer CPU fault; un fallo termina con diagnostico despues de retirar el waiter
y soltar el mutex. No se devuelve una adquisicion exitosa ni se espera en spin.
No se modelan todavia las diferencias de suspension de Windows segun su version.

Cerrar un handle durante una espera es indefinido en Windows. Esta implementacion
lo cancela con WAIT_FAILED/error 6 y mantiene el slot vivo para el waiter hasta
que se retire; ese caso se prueba por seguridad, no como oraculo Windows.
La destruccion host del proceso requiere que se hayan separado todos los hilos;
puede cerrar los eventos restantes al destruir su tabla. El snapshot prestado
`fnv_windows_handle_stats` expone handles vivos, waiters y contadores de espera,
timeout, Set y Reset mientras el llamador conserva el proceso.

## Limites y verificacion

Nombre o SECURITY_ATTRIBUTES no NULL detiene el runtime con diagnostico pendiente.
Faltan objetos nombrados/compartidos, herencia, permisos, DuplicateHandle, OpenEvent,
PulseEvent y archivos como handles. Los semaforos, mutex kernel y waits
multiples ahora estan implementados: ver [esperas conjuntas](native-windows-kernel-waits.md).
WaitForSingleObject y CloseHandle no son aun implementaciones para todos esos
tipos. CreateThread/ExitThread tienen ahora un [proveedor administrado](native-windows-workers.md);
SEH y el arranque del motor siguen pendientes.

`test_windows_events.py` genera ocho callers x86 sinteticos y los recompila con
el SDK real. Ejecuta 12000 comparaciones con Windows, 300 rondas entre dos hilos
para eventos automaticos/manuales con Set inmediatamente seguido de Reset,
timeouts reales, capacidad, handles obsoletos y agotamiento sin wrap de generacion.
GetThreadTimes no avanzo durante los 150 ms de medicion de cada grupo bloqueado
en el escritorio. No es una medida Switch ni una mejora de FPS del juego.

Cuatro procesos negativos cubren nombres, atributos de seguridad, proveedor
deshabilitado y salida de un waiter INFINITE tras un fallo CPU de otro hilo.
Se conservan regresiones de CS/Interlocked, heaps, TLS y Virtual*. La compilacion
ARM64 WindowsThread/WindowsMemory pasa con -Werror. No hay prueba HW de este
bloque ni NRO que ejecute el motor.

Contratos: [CreateEventA](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createeventa),
[WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject),
[CloseHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle).
