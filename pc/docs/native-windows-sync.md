# Secciones criticas e Interlocked guest32

El runtime Windows incorpora doce exports nativos nuevos: InitializeCriticalSection,
InitializeCriticalSectionAndSpinCount, Enter/Leave/TryEnter/DeleteCriticalSection,
SetCriticalSectionSpinCount y cinco Interlocked (Exchange, CompareExchange,
Increment, Decrement y ExchangeAdd). Once son imports normales del EXE auditado;
SetCriticalSectionSpinCount queda disponible para otros callers. No usa DLLs host
como implementacion del juego ni ejecuta codigo x86 dinamico.

El host configura `critical_section_capacity` antes del attach. Se preasignan
una tabla de metadatos y condiciones; cero deshabilita las secciones criticas.
Los cinco atómicos funcionan sin esa tabla. El modulo valida direccion,
alineacion y permisos del pool VM antes de entrar al bloqueo. El llamador
mantiene la memoria de cada objeto valida y writable durante toda su vida.

Cada seccion tiene propietario guest, recursion y waiters. Enter del propietario
incrementa recursion; Try no espera y devuelve falso bajo otro propietario.
Leave libera y notifica un waiter solo al agotar recursion. No se promete FIFO.
La espera usa condiciones de libnx/Windows/pthread, liberando el mutex de
metadatos mientras duerme. SpinCount se conserva como pista de rendimiento,
pero esta implementacion bloquea directamente bajo contencion, sin ejecutar
el bucle de spin solicitado. Un snapshot host `fnv_windows_sync_stats` expone
secciones vivas/ocupadas, waiters, llamadas de espera y notificaciones.

Las condiciones se despiertan como maximo cada 250 ms para consultar el estado
atomico del runtime CPU. Un fallo de otro hilo termina la espera con diagnostico,
despues de soltar el mutex; nunca se devuelve adquisicion exitosa sin propietario.
Los timeouts de esa comprobacion no son timeouts guest de EnterCriticalSection.
En Switch y Windows la espera es relativa; pthread usa CLOCK_MONOTONIC para la
condicion. No define el reloj del juego.

El objeto guest tiene un espejo de 24 bytes con valores DWORD, sin punteros host.
Es opaco: no reproduce el algoritmo interno de bloqueo de una version concreta
de Windows. Copiar/modificar ese espejo, dejar sin ser propietario o borrar una
seccion activa causa un diagnostico estricto. Algoritmos Windows inline que
modifiquen sus campos no estan soportados. El bloque no puede trasladarse.

Un hilo host no puede separarse mientras posea secciones, salvo que la sesion CPU
ya este invalidada. Tras un fallo, se pueden retirar todos los hilos y destruir
el proceso cuando los waiters hayan salido; la destruccion no reanuda el guest.
Los diagnosticos nunca se emiten manteniendo el mutex de sincronizacion.

Los Interlocked usan operaciones de 32 bits lock-free del compilador, con orden
seq_cst y retorno de bits x86: nuevo valor en Increment/Decrement, anterior en
Exchange/ExchangeAdd/CompareExchange. La aritmetica unsigned permite wrap de
32 bits. CompareExchange devuelve el valor observado tanto al acertar como al
fallar. LastError se conserva en las operaciones normales de sincronizacion.

## Verificacion y limites

`test_windows_sync.py` genera catorce callers x86 con IAT real sintetica y los
traduce con el SDK. Compara 500 secuencias recursivas y 10000 operaciones atomicas
contra Windows; verifica 4000 incrementos compartidos, contencion real, Try
fallido, capacidad, reuso, pila, LastError y lifecycle. Durante 150 ms de espera,
el contador GetThreadTimes del hilo bloqueado no avanzo en la prueba de escritorio.
No es una medida de CPU Switch ni rendimiento del motor.

Seis procesos negativos comprueban objeto ausente, Leave ajeno, campos modificados,
Delete activo, agotamiento de tabla y salida de un waiter al fallar el propietario.
Se conservan las regresiones de heaps/TLS/Virtual*. La compilacion ARM64 de CPU,
Windows thread y Windows memory pasa con -Werror. Aun no se prueba este bloque
en hardware ni se arranca el motor.

Los [eventos sin nombre](native-windows-events.md) ya tienen handles propios,
CloseHandle y espera individual. Faltan semaforos, mutex kernel, otros handles, waits multiples,
Sleep y CreateThread/ExitThread, ademas de SEH y DLLs. No equivale a un scheduler
Windows completo ni acredita el uso eficiente de todos los nucleos del juego.

Contratos primarios: [EnterCriticalSection](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-entercriticalsection),
[InitializeCriticalSectionAndSpinCount](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-initializecriticalsectionandspincount),
[InterlockedCompareExchange](https://learn.microsoft.com/en-us/windows/win32/api/winnt/nf-winnt-interlockedcompareexchange),
y [condvar libnx](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/kernel/condvar.h).
