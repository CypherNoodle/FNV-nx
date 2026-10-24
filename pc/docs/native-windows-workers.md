# Hilos guest32 sobre hilos nativos

La capacidad configurada es un limite de hilos vivos, no de nucleos. El runtime
no fija un limite de ocho: el diagnostico privado lo habia configurado asi.
Al agotarlo, CreateThread devuelve0/error8 y conserva intacto el destino del ID;
no se debe usar ese ID si fallo la creacion. `capacity_denials` cuenta esos
rechazos por falta de slot, separados de fallos del trabajador y paradas de peer.
La prueba de capacidad verifica el contador y un ID de salida centinela intacto.

Una ejecucion del arranque necesitaba12 trabajadores: con ocho, MoviePlayer
recibia un ID de pila sin inicializar despues de una creacion fallida. El
diagnostico ahora reserva32 slots, con pilas/TEB/TLS en un arena dedicada fuera
del pool VM. Ampliar el numero de hilos no aumenta los nucleos anunciados ni
constituye una optimizacion de rendimiento. Tampoco modifica automaticamente
el layout del NRO: ese layout aun debe validarse por separado.

Los nombres de depuracion pueden dirigirse a otro hilo adjunto o a un trabajador
suspendido antes de empezar a ejecutar. Se copian bajo el guard correspondiente;
los slots reutilizados pierden el nombre anterior. El lector recibe un snapshot
TLS, para evitar carreras con renombrados externos. IDs desconocidos/finalizados
siguen fallando; una notificacion para otro hilo nunca renombra al principal.
Solo se modela la notificacion MSVC406D1388 validada, no excepciones generales.
La prueba traduce200 notificaciones cotejadas con un observador VEH de Windows y
16 trabajadores suspendidos alternando nombre/slot limpio. Conserva nueve
controles negativos y las285 ejecuciones de ThreadProc del test de trabajadores.

Contrato de la notificacion: [documentacion de Microsoft](https://learn.microsoft.com/en-us/visualstudio/debugger/tips-for-debugging-threads?tabs=csharp&view=vs-2022).

WindowsThread incorpora CreateThread, ExitThread, GetExitCodeThread,
GetCurrentThread, GetThreadId y ResumeThread, ademas de Sleep y los contadores
de rendimiento. La tabla contiene ahora 73 exports; esto no completa Windows.
Cada ThreadProc se ejecuta como codigo x86 recompilado estaticamente, sobre un
hilo del host, con registros, TEB, LastError y TLS dinamico separados.

## Memoria y ciclo de vida

El host configura una arena guest RW comprometida y exclusiva mediante
worker_address/worker_bytes, worker_stack_bytes, worker_capacity y
worker_host_stack_bytes. Capacidad cero deshabilita CreateThread. Se admiten
hasta 256 slots. Cada slot reserva dos paginas para TEB/TLS y una pila guest;
la pila nativa de C/ARM64 es otra asignacion. La arena no puede solaparse con
imagen, PEB, heap, pool Virtual* ni regiones de hilos externos. Los IDs altos,
desde 0x80000001, quedan reservados y no se reutilizan.

El presupuesto de pila guest es fijo y esta completamente comprometido por el
llamador: no hay crecimiento por demanda ni pagina de guarda. Una solicitud de
pila que exceda el presupuesto falla con error 8. La pila nativa se configura
por separado, con al menos 64 KiB; esa cifra minima no es una recomendacion para
las funciones del motor. Las pruebas usan 1 MiB nativo y 64 KiB guest.

CreateThread admite flags 0 y CREATE_SUSPENDED. El segundo bloquea el inicio de
codigo guest con una condicion nativa hasta ResumeThread, sin suspender de forma
asincrona el hilo host. Resume devuelve el contador anterior (1 al primer resume,
0 al repetir). STACK_SIZE_PARAM_IS_A_RESERVATION y atributos de seguridad siguen
sin implementarse y generan diagnostico estricto. En exito se conserva LastError.

Retornar de ThreadProc utiliza EAX como codigo de salida y exige ret 4/stdcall.
ExitThread desenrolla hasta el limite fnv_cpu_execute mediante un resultado
FNV_CPU_THREAD_EXIT, sin marcar la sesion como fallo. Solo esta habilitado dentro
de trabajadores administrados; no termina el hilo principal del launcher. No
ejecuta destructores C++ host ni constituye una implementacion de SEH.

Antes de senalizar el handle se separa el contexto Windows y se abandonan los
mutex que poseia. El handle conserva ID y codigo, permanece senalizado y sirve
para esperas individuales/conjuntas. GetExitCodeThread devuelve STILL_ACTIVE
mientras no finaliza. El pseudohandle -2 sirve para GetCurrentThread/GetThreadId
y consultar el codigo propio; no se implementan todos los usos de pseudohandles.

CloseHandle no cancela el hilo. Un pin mantiene vivo el objeto aunque se cierre
antes del retorno. Las pilas/slots solo se reutilizan despues de terminar y hacer
join nativo. La siguiente creacion recoge trabajadores terminados; destruir el
proceso tambien los recoge y devuelve BUSY mientras queden activos. Los snapshots
fnv_windows_worker_stats exponen activos, terminados pendientes de join, creaciones,
recogidas y fallos; el llamador debe conservar la vida del proceso.

## Fallos y limites pendientes

Un fallo guest invalida la sesion y las esperas/Sleep verifican el estado cada
250 ms como maximo. Un hilo suspendido tambien puede salir por fallo de otro.
Un ThreadProc que retorna con ABI incorrecta o una seccion critica retenida se
diagnostica y envenena la sesion antes de limpiar el contexto. No se intenta
seguir con protecciones perdidas. Un bucle guest arbitrario no se interrumpe
asincronamente; no hay TerminateThread ni SuspendThread general.

El [TLS estatico de un EXE sin callbacks](native-pe-tls.md) ya se inicializa
cuando el host configura los bloques y presupuestos correspondientes. Las listas
de callbacks no vacias se rechazan antes del bind. Faltan callbacks y TLS de DLLs. Tampoco hay prioridades o
afinidades guest, scheduler de Windows ni adaptacion automatica a todos los
nucleos: libnx usa prioridad 0x2c y core predeterminado -2. Su distribucion y
costes en consola todavia requieren medicion.

## Pruebas y compilacion

test_windows_workers.py genera imports x86 y ThreadProcs propios, los traduce
con el SDK y ejecuta 285 hilos: 200 retornos/salidas comparados con Windows,
25 rondas de hilos que crean hijos, capacidad, inicio suspendido, cierre de 34
handles antes de terminar, TLS aislado y abandono de mutex recursivo. Son 1000
comparaciones con Windows. Ocho hijos negativos cubren flags, proveedor ausente,
fallo de otro hilo dormido/suspendido, entrada desconocida, ABI rota, ExitThread
fuera de trabajador y rechazo de TLS estatico declarado.

Las regresiones independientes de CPU, eventos/wait-all, CS/Interlocked, heaps,
TLS y Virtual* pasan. La compilacion ARM64 con -Werror y libnx pasa. El enlace
parcial contiene 352 unidades, sin externos inesperados y con definiciones de
plataforma verificadas. No es un enlace final del juego ni una prueba HW de hilos.

build_cpu_runtime.py permite --reuse-generated-validation: valida por hash la
generacion, SDK, dependencias, opciones e inventario de objetos; reutiliza solo
los 346 objetos generados identicos y recompila las dos unidades nativas. Rechaza
dependencias de los headers nativos cambiados en el codigo generado o headers
congelados. Objetos, dependencias e inventarios alterados se rechazan mediante
controles negativos. Las salidas derivadas permanecen privadas.

Contratos: [CreateThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread),
[ExitThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitthread),
[esperas](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects).


## Fallo propio frente a parada por otro hilo

`fnv_windows_worker_stats.failures` cuenta fallos propios de creacion, attach,
ejecucion, ABI o detach. El nuevo `peer_stops` cuenta trabajadores detenidos
porque otro hilo ya habia invalidado el runtime, incluidos inicio suspendido,
rechazo antes de ejecutar y `FNV_CPU_PEER_FAULT` durante la ejecucion. Un retorno
normal o ExitThread no se convierte en fallo si otro hilo falla justo despues.
Si un trabajador tambien tiene un fallo propio de limpieza, prevalece failures.
No cambia el fail-stop, la senalizacion, los locks ni el codigo de salida guest.

Antes se consultaba solamente el bit global al publicar FINISHED: un unico
fallo principal podia contarse de nuevo como fallo de todos sus trabajadores.
Las pruebas negativas ahora exigen cero fallos propios y una parada de peer
cuando el fallo nace en el principal; una entrada desconocida o ABI rota en
ThreadProc exige un fallo propio y cero paradas de peer. Los 285 trabajadores
normales siguen terminando sin ninguno de esos dos contadores. El ejecutable
de prueba reserva 16 MiB de stack principal host para sus estructuras de
configuracion/imports; no altera el stack de los trabajadores ni el de Switch.

La repeticion privada de arranque54 observa cuatro paradas por el rechazo
HWTL del principal y cero fallos propios. No demuestra que los trabajadores
completaran su trabajo ni que el motor arrancara. Su proceso aun conserva un
heap privado al detenerse y la limpieza devuelve BUSY; no se fuerza su borrado.
