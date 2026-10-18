# Contador de rendimiento y Sleep

QueryPerformanceCounter y QueryPerformanceFrequency usan una representacion
virtual fija: tiempo transcurrido desde crear el proceso, en unidades de 100 ns
y frecuencia declarada de 10000000 cuentas por segundo. Es una unidad de medida, no una frecuencia supuesta de la CPU ni
una precision garantizada de un nanosegundo. La resolucion efectiva depende del
contador host. No se conecta ni se equipara automaticamente con RDTSC guest.

host_clock.h convierte QPC/QPF de Windows, CLOCK_MONOTONIC de POSIX y el contador
ARM/CNTFRQ real de Horizon. Divide antes de multiplicar para evitar overflow de
ticks acumulados. La escritura LARGE_INTEGER se hace en little endian, en ocho
bytes guest; se admite alineacion de cuatro bytes y se verifican bounds/permisos
VM. Los resultados validos preservan LastError. Punteros invalidos o un reloj
inutilizable provocan diagnostico estricto; no se implementan excepciones SEH.

Sleep usa directamente el reloj monotono host para su deadline; cero cede ejecucion y un
tiempo finito espera hasta el plazo. INFINITE espera hasta un fallo de la sesion.
Las llamadas nativas se segmentan a 250 ms para comprobar fallos CPU de otros
hilos sin spin. Un sistema ocupado puede despertar mas tarde; no se promete una
latencia concreta del scheduler. Esta implementacion no depende de configurar
handles o workers. GetTickCount, timeGetTime y relojes civiles siguen pendientes.

test_windows_time.py ejecuta wrappers x86 recompilados, compara 256 lecturas con
timestamps Windows tomados antes/despues y valida 256 frecuencias, LastError,
Sleep de 25 ms, cero, dos punteros invalidos y fallos/retrocesos del reloj.
Un proveedor de prueba simula 3, 7 y 365 dias de uptime: el primer QPC es cero,
y se comprueban incrementos, redondeo y una conversion tras una hora. Las pruebas de workers
tambien cubren la salida de Sleep(INFINITE) por un fallo en otro hilo. No hay
medicion de precision, afinidad o rendimiento del juego en Switch.

Contratos: [QPC](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter),
[QPF](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancefrequency).

## Epoch y magnitud

La frecuencia de Windows no es siempre 10 MHz; esta es nuestra frecuencia virtual.
El epoch relativo es una decision para este proceso aislado, distinta del dominio
compartido entre procesos de Windows. Todos sus hilos comparten epoch y lock de
muestreo; un fallo o retroceso se diagnostica despues de liberar el lock y antes
de escribir el resultado. La inicializacion fallida libera sus recursos.

Restar epoch y reducir magnitud evita que el uptime previo de la consola entre
en productos del cliente. No demuestra que FNV use counter*1000000, ni hace ese
producto seguro indefinidamente: con uint64 y 10 MHz alcanza aproximadamente
21 dias de ejecucion continua. El consumidor debe restar timestamps antes de
multiplicar o usar aritmetica sin overflow. La fuente inyectable de configuracion
solo afecta QPC; no debe reentrar ni hacer longjmp. Sleep y waits usan reloj real.

Referencia: [consideraciones QPC de Microsoft](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).
