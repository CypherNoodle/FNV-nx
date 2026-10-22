# Inicializacion COM del invitado PC

`windows_com.c` publica `CoInitialize`, `CoInitializeEx` y `CoUninitialize` de
`ole32.dll`. Se agrega `fnv_windows_com_exports` al binder y al catalogo de
modulos. Target opcional CMake: `fnv_native_windows_com`.

El estado reside en `windows_thread`, junto al TEB propio. No se llama a COM
Windows del host ni se comparte el apartamento entre hilos invitados.
Primera inicializacion devuelve S_OK, otra compatible S_FALSE; ambas requieren
una desinicializacion. CoInitialize equivale a STA, CoInitializeEx distingue
MTA=0 y STA=2. DISABLE_OLE1DDE=4 y SPEED_OVER_MEMORY=8 no cambian el modelo.
Cambiar de modelo antes de agotar el balance devuelve RPC_E_CHANGED_MODE sin
alterarlo. Reserved distinto de cero y flags desconocidos devuelven E_INVALIDARG.
Una desinicializacion adicional es inocua. Agotamiento del contador produce un
error explicito. Detach elimina estos metadatos; no quedan objetos COM propios.

Esto NO implementa CoCreateInstance, apartamentos de objetos, proxies, RPC,
marshalling, OLE ni bombeo de mensajes. Ningun objeto se anuncia como disponible
por el mero hecho de inicializar COM. Cuando existan objetos con vida util COM,
su proveedor debera gestionar liberacion y reglas de afinidad por separado.

`test_windows_com.py` traduce callers x86 de un PE sintetico y compara 5003
inicializaciones con ole32 de Windows en un hilo nativo nuevo. Comprueba balance,
cambios de modelo, flags auxiliares/invalidos, reserved, aislamiento del hilo
principal, reinicio tras detach y limpieza stdcall. `--x87-build` repite con el
ABI extendido vigente. Hay compilacion estricta ARM64/libnx; no prueba hardware.

El replay privado del arranque PC supero CoInitialize(NULL) y se detuvo en
DSOUND.dll ordinal11, que una consulta por nombre/ordinal de la DLL del sistema
Windows identifica como DirectSoundCreate8. No demuestra arranque desde EXE frio, frame del motor ni
salida de audio. El proveedor DirectSound sigue pendiente; no se introduce una
interfaz vacia para ocultar ese bloqueo.

Contratos primarios:
[CoInitializeEx](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex),
[CoUninitialize](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-couninitialize).
