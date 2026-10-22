# Arranque capturado con el proveedor Vulkan propio

El runner tests/captured_startup_host.c puede compilarse con
FNV_STARTUP_VULKAN=1. Es un diagnostico privado de Windows, separado del NRO:
usa el loader Vulkan del sistema, un dispositivo real offscreen1280x720,
backbuffer/depth preparados y el puente COM propio. Agrega Direct3DCreate9 al
catalogo de DLL antes de ejecutar codigo traducido. No usa d3d9.dll del host
como renderer, Wine, FEX ni autorun. La opcion apagada conserva el runner previo.

El enlace requiere d3d9_com, d3d9_constants, d3d9_vk_backend, d3d9_geometry,
d3d9_texture_upload, d3d9_uniform_state, d3d9_uniform_pack y d3d9_vk_state,
ademas de las unidades de plataforma existentes y headers Vulkan. El catalogo
de shaders del juego sigue vacio. No crea WSI, no habilita Present ni anuncia
caps completas. El backend de consulta soporta CheckDeviceFormat, identidad
del adaptador y un D3DCAPS9 limitado que intersecta las operaciones implementadas
con los limites fisicos Vulkan. Shader versions, luces, fixed function,
StencilCaps, MRT, cube/volume y anisotropia avanzada no se anuncian.
La ausencia de un catalogo completo impide anunciar soporte general de SM2/3.

Los pools COM y buffers se reservan en el VM del invitado. Tras detener el
invitado, si todos los workers terminaron y se pudo desasociar el hilo, se
espera la GPU con watchdog, se destruyen COM/backend y se liberan esos pools
antes del loader. Si no es seguro, el proceso aislado conserva el respaldo
hasta salir. El JSON distingue limpieza Vulkan de limpieza completa del proceso.

La ultima ejecucion privada verifica carga de d3d9.dll, Direct3DCreate9,
identificacion real, consultas de modos y caps HAL limitadas (REF sigue indisponible).
HAL permite comprobar los formatos de color y profundidad. El juego termina
consultando REF y despues solicita ShellExecuteA para FalloutNVLauncher.exe.
La solicitud contiene el byte inicial 0x16. La ruta de fallback consulta
HKLM\Software\Bethesda Softworks\FalloutNV, valor Installed Path; como el
registro virtual esta vacio, RegOpenKeyExA informa que la clave no existe y el
buffer de salida de esa consulta queda sin inicializar. Esto explica el prefijo
de la ruta, pero no demuestra por que el juego eligio el fallback. ShellExecuteA
ahora reconoce unicamente esa solicitud del launcher, informa ERROR_FILE_NOT_FOUND
y devuelve fallo. El analisis estatico muestra que el EXE comprueba cero y
entra por su rama de inicializacion de ventana. El replay de esa continuacion
aun esta pendiente; no se ejecuta un proceso externo ni se finge un lanzamiento.
El siguiente bloqueo grafico que se debe resolver es por que el motor pasa de
las consultas HAL validas a REF sin llegar a CreateDevice; revisar el control de
enumeracion/caps y los formatos solicitados antes de anunciar shader versions
o ampliar el perfil.

En este camino se detecto RegCloseKey(NULL) despues de una apertura fallida.
Ahora devuelve ERROR_INVALID_HANDLE6; se comparo1000 veces con Windows,
conservando LastError. Los handles no nulos desconocidos siguen siendo un
pendiente explicito; no se accede al registro real desde el invitado.

El replay usa codigo capturado, EXE con identidad comprobada y una cache exacta
de resultados x87. No demuestra arranque desde EXE frio, un frame del motor,
audio real ni ejecucion en Switch. Los cuatro workers que terminaron con fallo
y el proceso ocupado al limpiar siguen siendo limitaciones del diagnostico.
EXE, capturas, codigo generado, caches y reportes privados no se publican.


Ver [capacidades limitadas](native-d3d9-caps.md) para el contrato actual,
pruebas y diferencia entre traduccion aceptada y shaders listos para dibujar.

## Continuacion privada tras el rechazo del launcher

El replay privado43 recompila el proveedor de ventanas y el runtime actual,
comprueba la identidad del EXE original y reutiliza los objetos privados
con sus hashes. Supera la solicitud ShellExecuteA y alcanza204624 llamadas
indirectas. El nuevo stop es MessageBoxA: "Failed to initialize renderer. Your
display doesn't support the selected resolution". No se implementa una respuesta
exitosa para ocultar ese error. CreateDevice no se alcanza; draws y submits
siguen en cero. La cache aritmetica registra1082 hits y0 misses; los cuatro
workers fallidos y cleanup BUSY siguen pendientes.

La traza enumera un modo A8R8G8B8(21), pero cero modos X8R8G8B8(22),
A1R5G5B5(25), X1R5G5B5(24) y R5G6B5(23). El perfil actual usa el mismo
color_format para el modo de escritorio y el backbuffer. Hay que revisar esta
separacion y los filtros de caps del motor antes de atribuir el error solamente
a las dimensiones o anunciar capacidades adicionales. El mensaje por si solo
no identifica la condicion fallida. No hay nueva prueba HW ni frame del motor.

## Resolucion configurada y siguiente requisito del motor

La observacion privada48 del selector confirma que el ancho solicitado es640
mientras el unico modo preparado es1280x720; la seleccion termina sin un modo
aceptado. No se deduce la causa solo del mensaje final. La observacion47 de un
logger no produjo textos utiles y no se usa como prueba del fallo.

La prueba49 usa un root privado sin assets y un FalloutPrefs.ini sintetico con
[Display], iSize W=1280, iSize H=720 y bFull Screen=1. Conserva el EXE original,
los objetos con hashes y las caps del proveedor. El error de resolucion desaparece:
el siguiente MessageBoxA dice "Hardware T&L required but not supported by Adapter".
Alcanza204418 llamadas;0draws/0submits,1082hits/0misses en la cache aritmetica.
Los cuatro workers fallidos y cleanup BUSY siguen presentes. No hay CreateDevice
ni frame del motor. Esto delimita otro bloqueo, no acredita soporte de T&L.

`pc/tools/prepare_startup_display_fixture.py --output local/NEW_ROOT --width 1280
--height 720` crea un root nuevo con esos ajustes, manifest y hash. Rechaza una
salida existente. No copia assets ni modifica preferencias de Steam. El helper
produce LF determinista; sus ajustes coinciden con los del experimento49, cuyo
INI privado original tenia CRLF. Las dimensiones deben coincidir con el backend
preparado. No cambia el NRO ni configura un juego jugable.

Siguiente trabajo: determinar que procesamiento de vertices requiere el motor
antes de ampliar DevCaps. Las caps actuales no anuncian Hardware T&L; no activar
el bit para esconder el rechazo sin implementar y verificar el contrato.


## Clasificacion de trabajadores al detener el arranque

La repeticion privada54 usa el runtime y COM actuales, sin cambiar las caps.
Conserva el mismo rechazo HWTL, 204.418 llamadas y cero draws/submits. El nuevo
contador distingue `failures=0` de `peer_stops=4`: los cuatro trabajadores se
pararon por el fallo del principal. La cifra anterior de cuatro failures era
un contador global repetido, no evidencia de cuatro errores independientes.
Las pruebas sinteticas tambien distinguen fallos propios de ThreadProc frente
a paradas de peer. La cache aritmetica conserva 1.082 hits y cero misses.

La limpieza del proceso aun devuelve BUSY. En este punto hay dos heaps vivos,
incluido uno privado, y destroy rechaza expresamente private_heaps no vacio.
Esto explica una condicion suficiente para BUSY en el diagnostico detenido;
no demuestra que la ruta normal del juego libere todo ni acredita una fuga.
No se fuerza la liberacion de recursos de un motor interrumpido.
