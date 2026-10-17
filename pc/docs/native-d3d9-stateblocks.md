# State blocks grabados: estados, constantes, texturas y shaders

BeginStateBlock/EndStateBlock y los metodos Capture/Apply de StateBlock9 ya
pasan por callers x86 recompilados. Esta primera ruta admite SetRenderState,
SetSamplerState, SetViewport, SetScissorRect, SetTexture (slots 0..15),
SetVertexShader/SetPixelShader y constantes FLOAT/INT/BOOL de
vertex y pixel shaders para los estados que implementa
el proveedor. El modulo prepara 97 metodos y una vtable StateBlock9 de seis
entradas, incluidos IUnknown y GetDevice.

Grabar no cambia el dispositivo actual. Cada setter se valida sin aplicar
sus cambios; se copia el valor a memoria propia. Si se graba varias veces
la misma clave, queda su ultimo valor. Apply restaura solo las claves del
bloque y Capture relee solo esas claves del dispositivo actual. Un bloque
vacio no cambia nada. Begin anidado y Capture/Apply durante otra grabacion
se rechazan.

La implementacion no captura streams,
indices, declaraciones ni FVF. Sus setters se rechazan durante la grabacion
y marcan el bloque como no admitido: EndStateBlock no devuelve un bloque
parcial exitoso aunque la aplicacion ignore esos errores. Cambiar targets o
hacer Reset mientras se graba tambien se rechaza. CreateStateBlock para
ALL/PIXELSTATE/VERTEXSTATE devuelve NOTAVAILABLE; no se anuncia soporte
completo de state blocks.

El bloque admite hasta 256 entradas numericas, 16 bindings de texturas y dos de shaders. El
proveedor procesa las 274 entradas posibles en una transaccion. Mantiene
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

Cada slot de textura grabado conserva una referencia interna al recurso,
sin incrementar su contador COM publico. Una aplicacion puede recibir cero
en Release y la textura seguir viva para Apply; GetTexture la vuelve a
exponer mediante una referencia publica nueva. La prueba Windows confirma
este comportamiento. Capture adquiere primero todas las retenciones nuevas
y despues libera las anteriores, incluidos slots que comparten un recurso.
El bloque conserva la identidad de la textura, no una copia de sus pixels.
Los shaders VS/PS tambien conservan referencias internas: Capture/Apply admite
null y bloques que seleccionan solo una etapa. Sus planes de constantes se
validan juntos antes de cambiar cualquiera de las dos etapas, sin asignacion
ni espera. Un plan invalido conserva ambos bindings y todo el estado numerico.

La transaccion del proveedor valida estados y texturas bajo un unico mutex.
Solo al tener exito adquiere todos los pins nuevos y suelta los anteriores.
Un error conserva los bindings originales; un Capture fallido no modifica
la salida. Los IDs de textura viajan en dos DWORDs solo dentro del proveedor,
nunca como punteros guest ni como handles Vulkan publicos. Apply no dibuja,
no espera a la GPU ni agrega submits.

Reset descarta las referencias de los bindings actuales, conservando las
retenciones de los bloques. Las texturas MANAGED retenidas con contador
publico cero sobreviven; una DEFAULT retenida impide Reset hasta liberar el
bloque. El teardown suelta las retenciones de la grabacion activa y los
bloques antes del barrido general, evitando recursos ya retirados.

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
Ademas verifica ocho bloques de shaders con 48 consultas y ocho bloques de
texturas con 384 consultas: grabacion sin
cambio actual, ultimo setter, null, Release publico cero, Capture y Apply.

```sh
python pc/tests/test_d3d9_stateblocks.py --output <carpeta-nueva> --cc <compilador-C>
```

El fixture Vulkan pasa 27 comprobaciones: estados, referencias del padre,
rechazo de cinco tipos de setters de recursos/FVF, capacidad y reintento,
rollback y salida de Capture intacta ante error. Ocho draws y 8192 pixels
comprueban restauracion de mascara, constantes, texturas y shaders antes/despues
de Capture. Los seis bancos pasan por setters/getters COM; los shaders GPU
consumen FLOAT/INT/BOOL de VS y FLOAT de PS (INT/BOOL de PS se comprueban
mediante getters, no mediante esos shaders).
Otras 51 comprobaciones Vulkan cubren los 16 slots, referencias publicas
cero, validacion sin aplicar y rollback de transacciones mixtas. Reset pasa
35 comprobaciones con seis reinicios exitosos, restaurando tambien planes de
shaders retenidos con referencias publicas cero; el cierre se prueba dejando
un bloque de texturas y una grabacion activos.
Doce consultas de bindings de shaders comprueban referencias publicas cero,
Capture null, VS parcial y persistencia tras Reset; dos draws adicionales
consumen los shaders restaurados. La prueba de planes VS/PS fuerza un fallo
en la segunda etapa y comprueba que la primera permanece ligada.
La captura de un proveedor sin referencias de dispositivo devuelve LOST
con salida intacta.

Evidencia privada: local/stateblocks-shaders-hal03,
local/vk-stateblock-shaders03, local/com-block-shaders01. La suite completa pasa 93 callers,
46 draws, 244 submits/completions headless y 249 WSI con cuatro Present.
Conserva los controles negativos y las pruebas previas de Reset/DXT/UP/FVF.
La regresion COM registra 97 metodos y 400 rondas concurrentes.
COM/backend/Horizon compilan ARM64 con -Werror. Pendiente de prueba Switch;
no hay un frame del motor en esta entrega.

Referencias: [BeginStateBlock](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-beginstateblock)
y [Capture](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dstateblock9-capture).
