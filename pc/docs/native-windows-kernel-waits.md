# Semaforos, mutex y esperas conjuntas guest32

El proveedor propio agrega siete exports a los eventos existentes:
CreateSemaphoreA/W, ReleaseSemaphore, CreateMutexA/W, ReleaseMutex y
WaitForMultipleObjects. Este bloque llevo WindowsThread a 41 exports; con hilos y timers ahora son 50. Los nuevos
objetos usan la misma tabla de handles con generaciones, capacidad explicita y
proteccion de slots mientras una espera los referencia. No son HANDLEs host.

Solo se admiten objetos sin nombre, locales al proceso y sin atributos de
seguridad. Las variantes A/W comparten implementacion porque no reciben texto.
No hay DuplicateHandle, herencia, Open*, permisos Windows, APC, objetos de
archivos. Hay un [proveedor de hilos administrados](native-windows-workers.md),
sin scheduler completo de Windows. Los nombres o atributos
no NULL y los punteros guest fuera del contrato provocan diagnostico estricto,
sin fingir una operacion valida ni implementar SEH. Los arrays y outputs de esta
ABI deben estar alineados a cuatro bytes y conservar backing valido durante la
llamada; se comprueban permisos de lectura/escritura en el pool VM administrado.

## Adquisicion y senales

Un semaforo conserva un contador no negativo hasta un maximo LONG positivo.
Cada espera satisfecha descuenta una unidad. ReleaseSemaphore comprueba el
incremento antes de modificar el contador y escribe el valor anterior solo al
tener exito. Incrementos invalidos fallan con 87 y exceso del maximo con 298.

Un mutex registra el ID guest del propietario y la recursion. Solo ese hilo
puede liberarlo; otro obtiene error 288. Una espera recursiva requiere otra
liberacion. Tras un detach exitoso, los mutex que aun pertenecen a ese hilo se
abandonan y las esperas pueden devolver WAIT_ABANDONED. El abandono elimina
toda la recursion y el siguiente adquirente pasa a ser propietario. El detach
con una seccion critica retenida sigue rechazandose salvo CPU ya faulted.

WaitForMultipleObjects admite entre 1 y 64 handles distintos, wait-any y wait-all,
poll, timeout finito e INFINITE. Wait-any elige el indice menor disponible.
Wait-all comprueba todos los objetos antes de consumir cualquier estado. Si hay
un mutex abandonado devuelve 0x80 en wait-all, o 0x80 + indice en wait-any.
Los errores de count/duplicados se validan con 87 y los handles invalidos con 6.

Un solo mutex host protege transiciones y concesiones. Cada espera bloqueada
registra en su pila host los objetos que mantiene pinneados y su resultado. El
despachador retira y concede peticiones elegibles antes de notificarlas: ResetEvent
no revoca una concesion ya realizada y otro hilo no puede robarla. El coste de un
despacho es proporcional a peticiones pendientes por objetos consultados, no a
toda la capacidad de handles. Se atiende al waiter elegible mas antiguo,
sin prometer orden de ejecucion host ni equidad para wait-all no satisfecho.
Crear/buscar handles sigue O(1). Abandonar mutex al separar un hilo recorre la tabla.

Las condiciones nativas liberan el lock mientras esperan; deadlines monotonos y
segmentos de hasta 250 ms conservan la deteccion de fallos CPU de otros hilos.
No hay un bucle de spin. Los contadores de diagnostico cuentan una espera por
hilo, aunque espere 64 objetos, e incluyen concesiones aun pendientes de retornar.
Cerrar durante una espera pendiente devuelve WAIT_FAILED/error 6; una concesion
ya hecha conserva su resultado aunque despues se cierre alguno de sus objetos.
Windows declara ese caso indefinido. Los slots se reciclan despues del ultimo pin.

## Verificacion reproducible

`pc/tests/test_windows_kernel_waits.py` genera 15 wrappers x86 con imports reales,
los recompila con el SDK y ejecuta el proveedor propio contra Windows nativo.
Incluye 12000 operaciones mixtas comparando resultado, LastError y contador previo,
64 objetos, limites, tipos incorrectos, adquisicion atomica y mutex abandonado.
Otras 100 rondas comprueban wait-all pendiente al abandonar un mutex recursivo.
Conserva las 12000 comparaciones y 300 rondas Set/Reset de eventos, reciclaje de
generaciones, cierre pendiente y cuatro procesos de fallo estricto.

Las regresiones independientes de CS/Interlocked, heaps, TLS y Virtual* pasan,
al igual que WindowsThread/WindowsMemory ARM64 con -Werror y libnx. Las pruebas
concurrentes usan hilos host adjuntos; no demuestran CreateThread guest, arranque
del motor, ejecucion de estas APIs en Switch ni mejoras de FPS. No se genera NRO.

Contratos oficiales: [espera multiple](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects),
[semaforos](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createsemaphorew),
[ReleaseSemaphore](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-releasesemaphore),
[mutex y abandono](https://learn.microsoft.com/en-us/windows/win32/sync/mutex-objects).
