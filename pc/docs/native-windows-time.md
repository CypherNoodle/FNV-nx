# Contador de rendimiento y Sleep

QueryPerformanceCounter y QueryPerformanceFrequency usan una representacion
virtual fija: nanosegundos monotonos y frecuencia declarada de 1000000000 cuentas
por segundo. Es una unidad de medida, no una frecuencia supuesta de la CPU ni
una precision garantizada de un nanosegundo. La resolucion efectiva depende del
contador host. No se conecta ni se equipara automaticamente con RDTSC guest.

host_clock.h convierte QPC/QPF de Windows, CLOCK_MONOTONIC de POSIX y el contador
ARM/CNTFRQ real de Horizon. Divide antes de multiplicar para evitar overflow de
ticks acumulados. La escritura LARGE_INTEGER se hace en little endian, en ocho
bytes guest; se admite alineacion de cuatro bytes y se verifican bounds/permisos
VM. Los resultados validos preservan LastError. Punteros invalidos o un reloj
inutilizable provocan diagnostico estricto; no se implementan excepciones SEH.

Sleep usa el mismo dominio monotono para su deadline; cero cede ejecucion y un
tiempo finito espera hasta el plazo. INFINITE espera hasta un fallo de la sesion.
Las llamadas nativas se segmentan a 250 ms para comprobar fallos CPU de otros
hilos sin spin. Un sistema ocupado puede despertar mas tarde; no se promete una
latencia concreta del scheduler. Esta implementacion no depende de configurar
handles o workers. GetTickCount, timeGetTime y relojes civiles siguen pendientes.

test_windows_time.py ejecuta wrappers x86 recompilados, compara 256 lecturas con
timestamps Windows tomados antes/despues y valida 256 frecuencias, LastError,
Sleep de 25 ms, cero y dos casos de punteros invalidos. Las pruebas de workers
tambien cubren la salida de Sleep(INFINITE) por un fallo en otro hilo. No hay
medicion de precision, afinidad o rendimiento del juego en Switch.

Contratos: [QPC](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter),
[QPF](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency).
