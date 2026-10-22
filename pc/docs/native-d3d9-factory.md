# Fabrica DX9 preparada: Direct3DCreate9 y CreateDevice

Estado posterior: [Reset del dispositivo sano y tamano fijo](native-d3d9-reset.md) ya implementado y probado en escritorio. La recuperacion de superficies perdidas y el cambio de resolucion siguen pendientes.

`native/d3d9_factory.inc` forma parte del modulo COM propio. Exporta
`d3d9.dll!Direct3DCreate9` mediante `fnv_d3d9_com_exports`, para enlazar la IAT
antes del primer attach. Son funciones propias dentro del runtime, sin DLL de
Windows ni launcher. SDK version 32 devuelve IDirect3D9 guest32 con QueryInterface,
AddRef/Release y metodos de adaptador. Version incorrecta devuelve NULL.

## Dispositivo preparado y limites

El registro de metodos del traductor es inmutable despues de comenzar la ejecucion.
Por eso el host prepara Vulkan, COM, vtables y export antes del EXE. El perfil
opcional de fabrica se copia y verifica contra las superficies implicitas reales:
ARGB8, profundidad D24S8 opcional, dimensiones compatibles y sin MSAA. El caller
conserva la vida del contexto del callback, no la memoria del perfil original.

CreateDevice no crea un segundo VkDevice. Valida los parametros x86 y transfiere
la referencia inicial del dispositivo preparado una sola vez. No acepta otro
CreateDevice ni recreacion tras liberar el dispositivo. No cambia las garantias
del registro CPU ni permite registrar metodos desde una llamada guest.

Subconjunto actual: adapter 0, HAL, vertex processing hardware con FPU_PRESERVE y
MULTITHREADED opcionales; rechaza software/mixed/pure. Backbuffer unico, tamano y
formato del perfil, swap effect DISCARD, sin MSAA ni backbuffer lockable. Admite
windowed/fullscreen del adaptador virtual y presentation interval DEFAULT/ONE.
Width/height/format cero se normalizan solo en windowed; count cero significa uno.
El dispositivo debe tener la misma configuracion auto depth que el perfil. Fallos
conservan los parametros y no publican un handle. Se rechaza alias entre parametros
y salida. HWND son identificadores guest opacos: falta integrar ventanas/foco y
las restricciones del hilo de creacion de Windows.

La fabrica publica un modo de adaptador virtual definido por el host; no afirma
que esa enumeracion sea la del monitor del PC ni la de una swapchain de Switch.
GetAdapterMonitor devuelve NULL. Identifier y conversiones no modeladas devuelven
NOTAVAILABLE. CheckDeviceType valida el modo preparado.
CheckDeviceMultiSampleType acepta solamente NONE para ARGB8 o D24S8, tras consultar
el formato real al proveedor. Devuelve un nivel de calidad (indice 0); admite
salida NULL y conserva la salida en fallos. Rechaza MSAA aunque la GPU lo soporte:
el renderer aun no lo implementa. Windowed/fullscreen comparten estos formatos
del adaptador virtual. CheckDepthStencilMatch acepta solo ARGB8+D24S8 y consulta
ambos formatos antes de responder. No asigna imagenes ni promete otros pares.
CheckDeviceFormat delega consultas a Vulkan para los formatos/usos implementados,
con las mismas flags de imagen que usa su creacion; no asigna memoria para consultar.

GetDirect3D del device obtiene una referencia publica al padre. Una referencia
interna conserva la fabrica mientras viva el device; al terminar la ultima
referencia al device se suelta. GetCreationParameters devuelve cuatro DWORD x86,
incluido HWND de 32 bits, independientemente de la ABI del host.

## Capacidades pendientes

GetDeviceCaps de IDirect3D9 y del device usan un callback opcional con un bloque
x86 D3DCAPS9 de 304 bytes. El callback debe suministrar una tabla completa y
validada contra el proveedor; el bridge copia solo si devuelve exito. La ruta
Vulkan actual aun NO publica esa tabla: retorna NOTAVAILABLE y deja la salida
intacta, incluso si el callback de prueba escribio datos parciales antes de fallar.

Esto no completa D3D9 ni el arranque del motor. Faltan D3DCAPS9, identificador,
mas consultas de compatibilidad, Reset y validacion de swapchain NVK; tambien el catalogo
e interfaces de shaders del juego, operaciones/formatos restantes y servicios
Windows. No se debe anunciar este CreateDevice como dispositivo listo para jugar.

## Verificacion

`local/vk-factory-com03/validation.json`: un PE propio con una importacion por
nombre Direct3DCreate9. Se enlaza por fnv_cpu_bind_imports y ejecuta el CALL [IAT]
x86 traducido. 79 callers, 32 comprobaciones de fabrica: versiones, identidad,
refs, modo, formatos reales consultados, parametros invalidos/normalizados,
publicacion unica, parent y creation parameters. Prueba perfil incompatible antes
del setup, copia del perfil y salida caps intacta. Luego usa el mismo device
Vulkan para los 26 draws y las pruebas de recursos/fences anteriores. Los tres
controles negativos siguen detectando pixels incorrectos. No usa D3D9 Windows.

`local/factory-com-regression02/validation.json`: regresion COM previa con NULLREF
como oraculo separado, sin fabrica: shader/constantes concurrentes, parent lifetime,
capacidad/alias y cuatro fallos estrictos. Ahora prepara 85 metodos (67 previos mas
18 wrappers factory/device). Esa regresion no prueba pixels ni NVK.

CMake ARM64 `fnv_native_d3d9_com` y `fnv_d3d9_vk_backend` pasan con -Werror.
Ninguna de estas pruebas arranca Fallout ni ejecuta NVK en la consola.

Present ahora agrega el metodo preparado 86 (slot device17) y valida el
subconjunto DISCARD de backbuffer completo. CreateDevice recuerda la identidad
guest de hDeviceWindow/focus para rechazar overrides de otra ventana. El host
configura el watchdog por espera; cero deshabilita Present. La prueba actual
WSI pasa cuatro llamadas Present traducidas y la regresion previa 86 metodos.
Ver [presentacion](native-vulkan-presentation.md) y [probe HW](switch-renderer-probe.md).

`local/vk-factory-queries01/validation.json` amplía la prueba a 81 callers y 57
comprobaciones de fabrica. Cubre NONE color/depth, calidad/salida NULL, rechazo
de los 16 modos MSAA, enum invalido, adapter y pares de profundidad incompatibles.
Conserva los 26 draws, controles negativos, fallos y 141 submits/completions.
ARM64 con -Werror vuelve a pasar. GetDeviceCaps sigue pendiente.

Referencias primarias: [Direct3DCreate9](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-direct3dcreate9),
[CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-createdevice),
[D3DPRESENT_PARAMETERS](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent-parameters),
[CheckDeviceMultiSampleType](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdevicemultisampletype),
[CheckDepthStencilMatch](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdepthstencilmatch).


Actualizacion: identificacion real del adaptador Vulkan
-----------------------------------------------------

GetAdapterIdentifier deja de ser un metodo indisponible. Consulta kind=2 de
factory_profile recibe adapter/flags y una imagen x86 D3DADAPTER_IDENTIFIER9
de1104 bytes; los offsets se verifican con el header de Windows. Flags 0/2,
adapter0; adapter fuera de rango limpia la estructura y devuelve INVALIDCALL.
Flags invalidos o salida invalida devuelven INVALIDCALL. Un proveedor ausente
o fallido no publica su salida parcial ni inventa una descripcion.

fnv_vk_backend_adapter_identifier usa deviceName/vendorID/deviceID reales del
VkPhysicalDevice seleccionado. Driver identifica nuestra capa FNV-nx Vulkan;
DriverVersion0 representa esa version inicial, no convierte driverVersion de
Vulkan a una version de Windows. SubSysId, Revision y WHQLLevel quedan0 porque
no se conocen/no hay certificacion. DeviceName es el monitor virtual DISPLAY1;
DeviceIdentifier conserva el UUID opaco de cache Vulkan del driver/dispositivo,
no un numero de serie ni un identificador PCI de la consola.

local/vulkan-adapter-identity03/validation.json:94 callers,65 checks de fabrica,
46 draws y251 submissions/completions, controles negativos de pixeles y fallos
inyectados. La identidad se coteja contra una enumeracion Vulkan independiente;
se verifica rollback ante proveedor fallido, flags, adapter, NULL, tamano y
canarios. No usa D3D9 de Windows como renderer. GetDeviceCaps sigue devolviendo
NOTAVAILABLE y su salida se conserva: aun no anunciamos capacidades completas.
local/renderer-adapter-shared01 conserva9 grupos/10240 pixeles y4 Present COM
sobre WSI de escritorio. El builder del probe exige ahora94 callers; esto no
constituye una nueva ejecucion en Switch. Tres unidades propias vuelven a
compilar ARM64/libnx con Werror y ABI x87 extendido.

FNV_D3D9_FACTORY_TRACE es opcional, apagado por defecto: registra modos, tipo,
formatos, caps e identificacion en stderr despues de soltar el lock COM. Sirve
para localizar el rechazo de arranque, no para medir rendimiento.

Referencia: [GetAdapterIdentifier](https://learn.microsoft.com/en-us/windows/win32/api/d3d9helper/nf-d3d9helper-idirect3d9-getadapteridentifier).


Estado posterior: [GetDeviceCaps limitado](native-d3d9-caps.md) ya devuelve una
imagen HAL valida de304 bytes para el proveedor Vulkan propio. No completa la
cobertura D3D9 ni anuncia Shader Model sin soporte general verificado.

## Display alpha compatibility

The prepared A8R8G8B8 mode is also enumerated when queried as X8R8G8B8:
D3D9 mode comparison ignores alpha. Enumeration preserves the requested
format in D3DDISPLAYMODE. Adapter checks accept either display spelling while
CheckDeviceType, depth matching and CreateDevice retain the implemented ARGB
render-target requirement. This does not add an XRGB render target or shaders.

Desktop validation `local/vulkan-display-alpha01/validation.json` passes80
factory checks, including both enumeration forms, unchanged output for unsupported
formats, ARGB render checks with XRGB display, and rejection of XRGB targets.
The existing46 synthetic draws, pixel controls and251/256 completed submissions
(offscreen/WSI) still pass. These are synthetic desktop results, not game frames.

Private startup replay44 now enumerates XRGB successfully but still reaches
MessageBoxA renderer rejection at204725 indirect calls. Its XRGB backbuffer
requests remain unsupported; there is no CreateDevice call. This correction
alone does not resolve the game's renderer selection. Shader caps are still
limited. Further investigation must distinguish these filters from dimensions.

Reference: [Microsoft EnumAdapterModes](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-enumadaptermodes).
