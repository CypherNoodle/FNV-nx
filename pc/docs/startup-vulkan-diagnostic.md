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
