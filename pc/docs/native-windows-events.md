# Eventos guest32 y handles de generacion

El runtime incorpora CreateEventA/W, SetEvent, ResetEvent, WaitForSingleObject y
CloseHandle para eventos **sin nombre, locales al proceso**. Cinco son imports
normales del EXE Steam auditado; CreateEventW queda disponible para otros callers.
No se cargan DLLs Windows en Switch. Son seis exports propios sobre el runtime.

El host configura `kernel_handle_capacity` antes del attach. Se preasignan tabla,
mutex y condiciones; cero deshabilita este proveedor. Hasta 65536 slots usan una
lista libre FIFO O(1), sin buscar desde el principio al crear cada evento. Los handles
guest32 contienen prefijo, indice y generacion; nunca contienen un puntero host.
Lookup es O(1). Al cerrar, un slot solo se reutiliza tras retirar sus waiters.
Una generacion agotada retira ese slot en vez de volver valido un handle viejo;
la capacidad determina los bits de indice y deja los restantes para generacion. Agotar capacidad/generaciones falla con
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
sin concesion. Se concede al waiter elegible mas antiguo; se puede saltar un
wait-all no satisfecho. No se promete orden de ejecucion del scheduler ni
equivalencia con una garantia FIFO de Windows.

WaitForSingleObject soporta poll cero, timeout finito e INFINITE. Devuelve
WAIT_OBJECT_0 al adquirir, WAIT_TIMEOUT al agotar el plazo y WAIT_FAILED con
error 6 para handles invalidos/cerrados. Conserva LastError en las esperas
normales. Las condiciones liberan el mutex al dormir y lo recuperan al volver.

`host_clock.h` usa QPC en Windows, CLOCK_MONOTONIC en pthread y el contador
arquitectural y su frecuencia real en Horizon. La conversion a ns evita el
overflow de multiplicar directamente todos los ticks acumulados. El limite de
frecuencia admitido es UINT32_MAX Hz. No usa frecuencia CPU/GPU, guest RDTSC ni
presume que overclock cambie ese contador. No modifica los temporizadores del
juego. QPC guest y Sleep se describen en [temporizadores](native-windows-time.md).

Los plazos se fijan una vez por llamada y se reevalua el tiempo restante tras
notificaciones espurias. Se usan esperas relativas de hasta 250 ms para detectar
un peer CPU fault; un fallo termina con diagnostico despues de retirar el waiter
y soltar el mutex. No se devuelve una adquisicion exitosa ni se espera en spin.
No se modelan todavia las diferencias de suspension de Windows segun su version.

Cerrar un handle durante una espera es indefinido en Windows. Esta implementacion
cancela una espera todavia pendiente con WAIT_FAILED/error 6 y mantiene el slot vivo para el waiter hasta
que se retire. Si ya hubo concesion, conserva su resultado aun cuando se cierre
un objeto seleccionado o no seleccionado; esos casos son politica de seguridad,
no un oraculo de comportamiento Windows indefinido.
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

## Presupuesto y bits de etiqueta

Los tokens son multiplos de cuatro. Se ignoran sus dos bits bajos al buscar un
objeto y al detectar duplicados en WaitForMultipleObjects, como para los handles
kernel de Windows. No se generaliza a pseudo-handles ni objetos USER/GDI.
Con prefijo de 4 bits, 2 bits de etiqueta y ceil(log2(capacity)) bits de indice,
quedan 26-ceil(log2(capacity)) bits de generacion. A capacidad 1024 son 65535
asignaciones por slot (67107840 totales); a 65536, 1023 por slot. Cada generacion
solo se usa una vez. La cola libre FIFO reparte el consumo, no elimina su limite.

kernel_generation_limit=0 usa ese maximo; un limite explicito menor permite
pruebas de agotamiento acotadas. Un limite mayor o sin tabla se rechaza.
El snapshot agrega retired_slots, generation_limit, total_creations y
remaining_creations. Cuenta asignaciones de objetos, incluso si despues falla
la creacion del hilo host. Remaining es presupuesto de futuras generaciones,
no numero de handles libres actuales. No se midio aun el ritmo de FNV.

Las regresiones cubren capacidades 1, 3, 1024 y 65536, limites invalidos,
16380 asignaciones hasta retirar cuatro slots con limite 4095, orden de dos
waiters y grant seguido de cierre. Set/Reset/Wait/Close con etiquetas 0..3 y
arrays con aliases se comparan contra Windows real.

Referencia: [bits bajos de handles kernel](https://devblogs.microsoft.com/oldnewthing/20050121-00/?p=36633/).
