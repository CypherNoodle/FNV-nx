# Enumeracion de pantalla y resolucion del port PC

windows_display.c publica USER32!EnumDisplayDevicesA mediante un backend
preparado antes del attach. Callback nativo thread-safe, sin llamadas guest:
parent=NULL enumera adaptadores; DeviceName enumera sus monitores. Indices desde0,
flags0/EDD_GET_DEVICE_INTERFACE_NAME1. Un backend ausente o fallido produce un
diagnostico, no una lista vacia para aparentar que no hay hardware.

DISPLAY_DEVICEA se serializa a424 bytes: cb0,name4,description36,stateFlags164,
id168,key296. Strings ANSI de capacidad fija con NUL, nunca punteros host.
La comparacion con Windows confirmo que cb limita el prefijo escrito: primero
se limpia ese prefijo y solo se rellenan campos completos que caben. cb puede
ser menor o mayor que424; el buffer invitado completo se valida antes de tocarlo.
No se pide a Windows escribir un cb enorme en una prueba de almacenamiento
insuficiente. El cb excesivo que no cabe en el guest es un caso negativo propio.
Fin de lista devuelveFALSE y limpia el prefijo conservando cb. LastError se
conserva. Se copian parent/record antes de escribir para no usar aliases invalidados.

windows_display_win32.h es SOLO el backend del diagnostico de escritorio. Lee
EnumDisplayDevicesA real, sin simular que la GPU de Windows es la de Switch.
No se ha conectado todavia un catalogo Horizon a partir de la GPU NVK preparada.
No inventar IDs PCI/ATI ni capacidades para evitar el fallo del motor.

## Metricas de pantalla

windows_user32_create_with_metrics acepta un callback nativo inmutable. La API
create anterior mantiene las metricas extra pendientes: no tiene nuevos valores
por defecto inventados. SwapMouseButton/REMOTESESSION siguen siendo estado local
del guest. El backend Windows consulta0/1(CX/CYSCREEN),76..79(origen/tamano
virtual) y80(numero de monitores), con enteros de32 bits y LastError conservado.
Otros indices siguen pendientes si el backend no los soporta.

## Portatil, dock y superficies de Switch

windows_display_resolution.c consulta appletGetOperationMode y
appletGetDefaultDisplayResolution, junto con nwindowGetDimensions del NWindow
valido proporcionado por el caller. Registra modo0portatil/1dock, tamano de
salida informado por Horizon y tamano actual de la superficie por separado.
No fuerza1080p por detectar dock: Horizon puede informar otra resolucion y la
superficie puede seguir siendo1280x720. Un error o cambio de modo observado
durante la consulta devuelve0 y no publica un snapshot parcial. El query de
resolucion de applet requiere Horizon3.0.0+ segun libnx; no hay fallback inventado.

fnv_display_resolution_metrics adapta un snapshot publicado a las metricas del
desktop invitado: usa la superficie preparada, origen0,0 y un monitor. No hace
IPC dentro de GetSystemMetrics. El host debe retener el snapshot inmutable y
serializar su publicacion con el guest detenido. changed detecta diferencias de
modo/salida/superficie. Son hechos observados, no una transaccion global de los
servicios de Horizon. No hay hook de hot-dock, parada segura de guest/recreacion
de swapchain ni cambio de resolucion interna implementados en este cambio.

Una salida dock1080p con superficie720p mantiene GetSystemMetrics en720p hasta
que el renderer prepare la nueva superficie. nwindowSetDimensions no se llama
desde estas APIs: libnx no permite cambiar dimensiones con buffers registrados.
El renderer Vulkan actual exige extent preparado y su Reset conserva dimensiones.
El cambio de modo en caliente debe resolver eso antes de actualizar el guest.
La resolucion interna del juego es otro contrato y no se deduce de estas lecturas.

## Verificacion y frontera del motor

test_windows_display.py:46 registros completos comparados en esta maquina con
adaptadores/monitores reales, fin de lista, flags0/1, cb0..428, guardas, siete
metricas, ABI x86 y LastError. El numero de registros depende del host. Ocho
casos negativos detectan buffers/parent inaccesibles, nombre sin fin, flags,
backend ausente/fallido, record invalido y cb que no cabe. Ademas simula la
transicion portatil720p -> dock1080p/superficie720p -> superficie1080p; no es una
prueba real de dock. Puede repetirse con --x87-build para el ABI extendido.

build_switch_display.py verifica proof vigente, compila tres unidades ARM64
con Werror y enlace parcial; verifica llamadas/exports reales de libnx para modo,
resolucion applet y dimensiones de NWindow. No enlaza NRO ni ejecuta en consola.
La regresion USER32 sin backend mantiene sus diagnosticos y ventanas virtuales.

El replay privado ya supera display enumeration y screen width/height. El runner
opcional FNV_STARTUP_VULKAN registra ahora nuestro proveedor D3D9 sobre un
dispositivo Vulkan real offscreen. LoadLibraryA(d3d9.dll) y Direct3DCreate9
resuelven; GetAdapterIdentifier usa las propiedades del dispositivo seleccionado.
La traza muestra consultas de modos y GetDeviceCaps HAL con un perfil limitado;
REF sigue indisponible. Ver [capacidades](native-d3d9-caps.md).
El motor toma la salida al launcher; no se ejecuta ShellExecute ni se oculta
ese fallo. Ver [arranque con Vulkan](startup-vulkan-diagnostic.md).
No hay frame del motor, arranque frio ni nueva prueba en consola.

FNV_MODULE_TRACE es una opcion de compilacion diagnostica del namespace de DLL:
escribe cargas y consultas por nombre/ordinal en stderr, fuera de locks, con
strings acotados. Default apagado. No usar esa ejecucion para medir rendimiento.
La regresion de modulos verifica40 nombres y2000 ciclos concurrentes; el numero
de DLL fijadas se deriva del catalogo de entrada, que crecio desde la prueba vieja.

Referencias primarias:
[EnumDisplayDevicesA](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaydevicesa),
[DISPLAY_DEVICEA](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-display_devicea),
[libnx applet](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/services/applet.h),
[NWindow](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/display/native_window.h).
