# State blocks grabados: estados y constantes

BeginStateBlock/EndStateBlock y los metodos Capture/Apply de StateBlock9 ya
pasan por callers x86 recompilados. Esta primera ruta admite SetRenderState,
SetSamplerState, SetViewport, SetScissorRect y constantes FLOAT/INT/BOOL de
vertex y pixel shaders para los estados que implementa
el proveedor. El modulo prepara 97 metodos y una vtable StateBlock9 de seis
entradas, incluidos IUnknown y GetDevice.

Grabar no cambia el dispositivo actual. Cada setter se valida sin aplicar
sus cambios; se copia el valor a memoria propia. Si se graba varias veces
la misma clave, queda su ultimo valor. Apply restaura solo las claves del
bloque y Capture relee solo esas claves del dispositivo actual. Un bloque
vacio no cambia nada. Begin anidado y Capture/Apply durante otra grabacion
se rechazan.

La implementacion no captura bindings de shaders, texturas, streams,
indices, declaraciones ni FVF. Sus setters se rechazan durante la grabacion
y marcan el bloque como no admitido: EndStateBlock no devuelve un bloque
parcial exitoso aunque la aplicacion ignore esos errores. Cambiar targets o
hacer Reset mientras se graba tambien se rechaza. CreateStateBlock para
ALL/PIXELSTATE/VERTEXSTATE devuelve NOTAVAILABLE; no se anuncia soporte
completo de state blocks.

El proveedor expone una transaccion numerica de hasta 256 entradas. Mantiene
su mutex durante validacion/aplicacion y conserva una copia del estado
anterior. Si una entrada falla, restaura todos los cambios; Capture escribe
su salida solo si todas las lecturas pasan. No envia comandos, espera fences
ni modifica draws ya enviados. Viewport y scissor siguen validandose contra
el target actual, por lo que un bloque puede fallar tras cambiar de target.

Las constantes se copian a bancos propios con mascaras por registro. Las
escrituras solapadas conservan el ultimo valor; Capture modifica solo los
registros seleccionados. Apply marca como pendientes de upload solo los
registros cuyo valor cambia, sin enviar comandos Vulkan por si mismo. El
flush anterior al draw conserva los rangos pendientes si falla una subida.
La validacion de todas las mascaras precede la transaccion numerica: si esta
falla, tampoco se aplican constantes. Se conserva el ABI FLOAT/INT/BOOL sin
convertir sus bits. El llamador del API interno de snapshots debe serializar
el acceso a cada snapshot; COM lo hace con su mutex de modulo.

Los bloques son objetos del bridge COM, sin un recurso Vulkan ficticio. Su
vida mantiene una referencia al dispositivo hasta el ultimo Release. Ocupan
un slot del limite de recursos del bridge y memoria CPU propia (aprox. 18 KiB
por bloque); no representan memoria VkDeviceMemory. Si EndStateBlock falla
por capacidad, conserva la grabacion para reintentar tras liberar un slot.
Un rechazo de puntero de salida tampoco consume la grabacion. Estas reglas
de error del port no se presentan como equivalencia exhaustiva con Windows.

## Verificacion

test_d3d9_stateblocks.py usa Windows HAL independiente. Confirma que grabar
no cambia el estado, que gana el ultimo setter, Apply/Capture, Begin anidado,
Capture mientras se graba, bloque vacio, QueryInterface y GetDevice.
Ademas contrasta 64 bloques de constantes (1152 comparaciones de bancos),
con escrituras aleatorias/solapadas, limites y captura parcial. Comprueba
reintento de uploads fallidos y cuatro rechazos que conservan datos/salida.

```sh
python pc/tests/test_d3d9_stateblocks.py --output <carpeta-nueva> --cc <compilador-C>
```

El fixture Vulkan pasa 29 comprobaciones: estados, referencias del padre,
rechazo de ocho setters de recursos/FVF, capacidad y reintento,
rollback y salida de Capture intacta ante error. Cuatro draws y 4096 pixels
comprueban restauracion de la mascara de escritura y constantes antes/despues
de Capture. Los seis bancos pasan por setters/getters COM; los shaders GPU
consumen FLOAT/INT/BOOL de VS y FLOAT de PS (INT/BOOL de PS se comprueban
mediante getters, no mediante esos shaders).
La captura de un proveedor sin referencias de dispositivo devuelve LOST
con salida intacta.

Evidencia privada: local/stateblocks-constants-hal01,
local/vk-stateblock-constants02, local/com-block-constants01. La suite completa pasa 93 callers,
42 draws, 230 submits/completions headless y 235 WSI con cuatro Present.
Conserva los controles negativos y las pruebas previas de Reset/DXT/UP/FVF.
La regresion COM registra 97 metodos y 400 rondas concurrentes.
COM/backend/Horizon compilan ARM64 con -Werror. Pendiente de prueba Switch;
no hay un frame del motor en esta entrega.

Referencias: [BeginStateBlock](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-beginstateblock)
y [Capture](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dstateblock9-capture).
