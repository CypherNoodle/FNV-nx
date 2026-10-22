# Temporizador multimedia PC

windows_multimedia.c exporta winmm.dll!timeGetTime como proveedor opcional.
Agregar fnv_windows_multimedia_exports al binder y catalogo; target CMake
fnv_native_windows_multimedia. No implementa waveOut, DirectShow, callbacks,
timeBeginPeriod/EndPeriod ni modifica la resolucion global del reloj del host.

Usa fnv_windows_uptime_ms, el mismo helper y epoch monotono que GetTickCount.
Devuelve DWORD con wrap a2^32 milisegundos. En Switch el helper deriva ms de los
ticks ARM; no usa UTC ni resta el inicio del proceso. QPC sigue siendo otro
contrato, relativo al proceso a10MHz. La precision de la muestra depende del
host; en Windows el helper de uptime es grueso, no se anuncia precision1ms.
La funcion exige hilo invitado adjunto y conserva LastError; no duerme ni
procesa mensajes. Un fallo del reloj produce un diagnostico explicito.

test_windows_multimedia.py traduce4 callers sinteticos y hace2000 comparaciones
con Windows timeGetTime/GetTickCount, tolerando hasta32ms de diferencia por el
reloj grueso del host. Comprueba epoch, orden, ABI stdcall y LastError. No es una
prueba de wrap mediante reloj inyectado ni de precision de scheduling/audio.
Ambas unidades multimedia y windows_thread compilan ARM64/libnx con Werror.
El replay privado supera esta importacion y llega a EnumDisplayDevicesA; no
implica arranque frio del EXE, un frame ni temporizacion del motor en hardware.

Contrato primario:
[timeGetTime](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timegettime).
