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
handles o workers. timeGetTime y las conversiones civiles de SYSTEMTIME siguen pendientes.

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

## Relojes usados por el CRT

La auditoria estatica del codigo capturado identifica llamadas directas a
GetSystemTimeAsFileTime y GetTickCount en la inicializacion de la cookie del CRT.
Ambos bridges estan en la tabla base; esto no demuestra que el CRT ya se ejecute.

GetTickCount devuelve DWORD de milisegundos desde el contador de arranque host,
con wrap a 2^32 ms. En Windows usa GetTickCount64; Horizon usa el contador ARM
monotono. No usa el epoch relativo ni el proveedor inyectable de QPC.
GetSystemTimeAsFileTime escribe UTC en ocho bytes little endian, unidades de
100 ns desde 1601. Windows usa su API publica; Horizon/POSIX CLOCK_REALTIME.
La conversion comprueba nanosegundos, epoch y overflow antes de escribir.
La hora UTC puede retroceder por ajustes del sistema: nunca se usa para waits.
El servicio de hora debe estar disponible; no se inventa una fecha al fallar.

256 consultas de cada API estan entre muestras Windows tomadas antes/despues.
Las pruebas cubren LastError, ABI stdcall, guardas, puntero invalido, fechas
anteriores a Unix, epoch 1601, ultimo instante de 9999 y overflow. Los relojes
reales siguen independientes mientras QPC simula 3/7/365 dias de uptime.
ARM64 compila CLOCK_REALTIME y verifica clock_gettime en libsysbase. La precision
y disponibilidad del reloj UTC en una ejecucion real de Horizon quedan por validar.

Contratos: [GetTickCount](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount),
[GetSystemTimeAsFileTime](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsystemtimeasfiletime).

## STARTUPINFO del proceso virtual

GetStartupInfoA/W escriben la misma estructura x86 de 68 bytes. cb=68,
dwFlags y wShowWindow vienen del snapshot de fnv_windows_config; por defecto
son cero. Solo se admite STARTF_USESHOWWINDOW (1), con wShowWindow entre 0 y 11, excepto SW_SHOWDEFAULT (10) si ese flag esta activo.
Otros flags se rechazan al crear el proceso. No hay cadenas de desktop/title,
handles stdio heredados, payload reservado ni proceso hijo: esos campos son cero.
No se copia STARTUPINFO del host de 64 bits y no se crea una ventana por esta API.
Esto proporciona los datos que consulta el CRT, no un dispatcher USER32.

Las pruebas A/W verifican bytes, offsets, stack, guardas, LastError, configuracion
copiada y rechazo de flags/valores no soportados. GetCommandLineA/W y el parser
de argumentos siguen pendientes. El perfil de arranque real tampoco esta validado.

Contrato: [GetStartupInfoA](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getstartupinfoa).
