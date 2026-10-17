## Subidas de textura diferidas (PR #3)

La ruta activa conserva UnlockRect en CPU: convierte/parchea el shadow propio
y marca la textura pendiente. La primera utilizacion en un draw o readback
crea una version Vulkan y sube la cadena de mips una sola vez, aunque haya
varios Unlock entre usos. Un error conserva el shadow y la marca pendiente;
el reintento corresponde al siguiente uso, no a repetir un Unlock ya exitoso.
Los draws enviados conservan sus versiones anteriores hasta sus fences.

El readback valida primero nivel y capacidad. Drena trabajo anterior con el
timeout del llamador antes de subir la textura; una peticion invalida no crea
trabajo GPU. Una textura que nunca recibio datos sigue sin poder leerse.
El test del PR se amplio para comprobar cero submits por Unlock, todos los
mips, parches, A8 y liberacion completa. Las pruebas de fallos ahora inyectan
allocation/map/submit en el primer uso, donde realmente ocurren.

La subida aun usa un submit por textura sucia, no un batch de transferencias.
Varios primeros usos pueden llenar el anillo antes de un draw; BUSY requiere
poll/wait y reintento por el host, segun el contrato actual del proveedor.
Aun no es un dispositivo D3D9 general conectado al juego. No se atribuye una
mejora de FPS en Switch a este cambio sin una medicion de hardware.

Pruebas: test_vulkan_lazy_texture.py y test_d3d9_vk_backend.py. Los helpers
texture_ready de la suite fuerzan transferencias solo para preparar datos de
prueba de forma determinista; no pertenecen a la ruta de produccion. El caso
independiente de siete mips comprueba el ahorro real de submits en Unlock.
Los apartados historicos de subida inmediata que siguen describen la base
anterior y deben interpretarse junto a esta actualizacion.

# Proveedor D3D9 propio: dispositivo, superficies, buffers y texturas Vulkan

Estado posterior: [Reset del dispositivo sano y tamano fijo](native-d3d9-reset.md) ya implementado y probado en escritorio. La recuperacion de superficies perdidas y el cambio de resolucion siguen pendientes.

La ampliacion [DrawPrimitiveUP/DrawIndexedPrimitiveUP](native-d3d9-up.md)
comparte el mismo draw con buffers transitorios privados; 7168 pixels
adicionales verifican su copia y duracion. Pendiente de prueba Switch.

`pc/native/d3d9_vk_backend.c/.h` crea un dispositivo Vulkan 1.1 real con una
cola grafica, superficies color BGRA8 y profundidad/stencil D24S8 opcional.
Entrega callbacks al bridge COM existente. Los objetos pertenecen a este
proveedor: no crea un dispositivo D3D9 de Windows ni usa NULLREF para ejecutar
las operaciones. Es el inicio del backend de produccion, aun sin ventana.

## Contrato implementado

- Superficies standalone ARGB8 y D24S8, sample count 1. Caps de imagen/formato
  consultadas al dispositivo. Lockable color, otros formatos y MSAA rechazados.
- Render target cero y depth opcional, con dimensiones compatibles. Cambiar
  color reinicia viewport y scissor al tamano del destino, incluido mismo target.
- Viewport y scissor con getters, y SCISSORTESTENABLE. Clear conserva rectangulos
  propios y aplica interseccion con viewport/scissor habilitado. Copia valores
  antes de retornar: el guest puede reutilizar su memoria inmediatamente.
- Clear de color/depth/stencil mediante render passes y vkCmdClearAttachments.
  Mantiene layout GENERAL y dependencias explicitas entre usos/submissions.
- Referencias publicas separadas de bindings y comandos. Un recurso liberado
  por el guest sigue vivo si esta enlazado o pendiente de una confirmacion GPU.
- Presupuesto de VkDeviceMemory por tamanos reales de requirements. No incluye
  memoria interna del driver ni equivale al RSS del proceso. Allocations
  dedicadas por imagen/staging; todavia no hay arena/suballocacion general.
- Ring de 1..3 slots de comandos/fences, configurado a dos en la prueba.
  flush no espera ni retira slots implicitamente; devuelve BUSY si estan ocupados.
  poll observa fences, wait usa un solo timeout para todos los fences pendientes.
  EndScene no envia ni libera recursos. No hay bucles de polling con sleeps.
- Vertex/index buffers reales con bindings de 16 streams e indices 16/32 bits.
  Pools Default/Managed, WRITEONLY/DYNAMIC; VB con FVF cero y declaraciones propias.
  Lock/Unlock por COM instala versiones Vulkan inmutables. Actualizacion parcial
  preserva bytes, DISCARD invalida el contenido anterior y aqui lo inicializa a cero.
  NOOVERWRITE tambien crea una version completa; instancing no implementado.
  Cada version pendiente permanece viva hasta su fence, incluso tras un DISCARD.
  Fallos de allocation/map/flush conservan la version anterior y permiten reintentar.
- Texturas 2D con mipmaps y 16 bindings de pixel sampler. ARGB8/XRGB8,
  RGB565, A8 y L8 convertidos a RGBA8; imagenes Vulkan OPTIMAL con uso sampled
  y transfer. Pools Default/Managed, DYNAMIC solo Default. RT/depth/autogen,
  superficies de textura y vertex samplers pendientes. [DXT1/3/5](native-vulkan-bc.md)
  ya usan imagenes BC1/2/3 comprimidas si el dispositivo habilita BC.
- Cada UnlockRect convierte el parche sobre un shadow propio y sube todos los
  mips a una imagen nueva usando staging HOST_VISIBLE. Barreras explicitas y
  flush si no coherente. No retiene punteros guest ni modifica la imagen previa.
  DISCARD de nivel cero completo invalida todos los mips (aqui ceros elegidos).
  Un fallo anterior al envio conserva la imagen anterior y permite reintentar.
- Upload de textura ocupa un slot de transfer hasta su fence: sin esperas
  implicitas, devuelve BUSY si no hay slot. El caller debe poll/wait para retirar
  completados. En fallo de grabacion/envio se invalida el backend y conserva
  recursos hasta cierre seguro. No reusa comandos con completitud desconocida.
- Readback de mip mediante copia GPU: RGBA8 o bloques BC compactos, con salida
  intacta en fallo/timeout.
  Textura recien creada tiene contenido indefinido; readback se rechaza hasta
  primer upload. Esa subida inicializa tambien los otros mips con el shadow propio.
- Catalogo host propio: tokens DX9 exactos, SPIR-V prevalidado offline y plan
  reflejado de constantes se copian a memoria propia. Registro limitado a 4096
  entradas; duplicados, etapa/cabecera invalidas y planes fuera de rango fallan.
  La comprobacion basica de SPIR-V no sustituye spirv-val, reflection ni linkage
  offline de la pareja. El caller debe suministrar los artefactos compatibles.
- CreateVertexShader/CreatePixelShader crea VkShaderModule real desde ese catalogo;
  Set/Get y referencias por binding pertenecen al backend. Fallos no publican
  handles. El guest nunca suministra SPIR-V. Los modulos quedan hasta su ultima
  referencia o por el fence de un draw que los consume.
- Declaraciones de vertices propias, copiadas e inmutables; comprueba END,
  streams, semanticas unicas, limites y soporte de formato del dispositivo.
  FLOAT/half, D3DCOLOR y formatos normalizados o scaled admitidos si soportados.
  UBYTE4 usa USCALED y SHORT2/4 SSCALED (entradas float); UDEC3/DEC3N y metodos
  especiales se rechazan hasta disponer de conversion equivalente.
- Constantes float/int/bool por COM a bancos privados persistentes, con planes
  reflejados copiados al bind. Debug read_uniforms verifica el empaquetado std140
  sin submit; Draw copia cada snapshot a UBOs Vulkan propios y cancela el ticket
  CPU despues de copiarlos. Los UBOs GPU permanecen vivos hasta su fence.
- Contrato de pareja offline por link_program: copia semanticas DX9 -> locations
  y mascara de pixel samplers, sin suponer orden de declaracion. Layout fijo:
  set 1 binding 0 VS UBO; set 2 bindings sampler; set 3 binding 0 PS UBO.
  El host verifica linkage, tipos y descriptors de SPIR-V antes del registro.
- DrawPrimitive/DrawIndexedPrimitive ejecutan triangle lists, strips y fans con shaders propios,
  texturas, UBOs, vertex/index buffers, base vertex con signo y offsets. Valida
  rangos de indices/vertices, alineacion y caps antes de tocar la GPU. Requiere
  scene activa y pareja registrada. CULL_NONE/CW/CCW estan implementados, incluido
  el valor inicial CCW, con orientacion comprobada bajo viewport de altura negativa.
  Lineas, puntos y fixed-function vertex processing siguen pendientes.
- Profundidad enable/write/compare, sampler point/linear con wrap/mirror/clamp/
  border limitado, viewport y scissor se aplican al pipeline. Blend color/alfa con
  factores comunes, factor constante ARGB, ADD/SUB/REVSUB/MIN/MAX, alfa separado
  y mascara RGBA. Factores obsoletos BOTH* y dual-source se rechazan. Blend
  consulta soporte del formato. Stencil, anisotropia y sRGB siguen pendientes.
  Setters invalidos conservan el estado anterior.
- Clear pendiente se envia antes del draw en la misma cola. Cada draw crea sus
  propios pipeline/descriptors/samplers/UBOs y ocupa un frame slot. Ring lleno
  devuelve BUSY sin esperas implicitas. Al retornar exito, bytes y recursos ya
  son propios. Cambiar bindings, constantes o actualizar recursos no altera
  lo enviado. EndScene no libera ni confirma trabajo.
- Fallos de construccion anteriores a BeginCommandBuffer permiten reintento y
  liberan el candidato. Fallos de grabacion/envio invalidan backend y conservan
  recursos hasta cierre seguro. Pipelines parciales devueltos en error se liberan.
- Readback diagnostico BGRA8, staging propio y salida intacta en error. Si
  vence una espera, staging e imagen siguen retenidos. Comprueba coherencia de
  la memoria y usa invalidate cuando corresponde. Su timeout aplica a cada
  espera interna; puede hacer hasta tres, ademas del trabajo de CPU/driver.

El backend serializa su estado/cola con mutex nativo. El caller garantiza la
vida del loader y propiedad exclusiva al destruir. Hay limites explicitos:
2..256 recursos, 1..128 comandos por lote, hasta 32 rectangulos por Clear.
No devuelve exito para metodos pendientes. CreateShader solo admite tokens con
una traduccion previamente registrada por el host; no hay compilacion guest.
Draws tienen callback en el subconjunto indicado; no es un proveedor D3D9 completo.

La primera referencia de device se transfiere al consumidor COM. Recursos
con referencias publicas retienen el device; bindings y comandos no forman
ciclos. Al llegar a cero se sueltan bindings/targets implicitos. Frames
enviados solo se retiran tras un fence observado. Si falla la grabacion/envio,
el backend se considera inutilizable y conserva la propiedad para su cierre.
Una finalizacion desconocida en DeviceWaitIdle conserva el contexto completo;
el caller puede reintentar destroy. No se inventa un fence completado.

Render passes/framebuffers se conservan por lote hasta el fence. Se crean
por comando en esta primera version; faltan sus caches y medir coste en NVK.
Los buffers usan memoria HOST_VISIBLE, flush explicito si no es coherente y
una copia CPU completa mas una allocation dedicada en cada actualizacion.
Es una base de correccion: faltan arenas, uploads parciales y medir en Switch.
El contador buffer_versions incluye versiones VB/IB y UBOs propios de draws.
Readback de buffers es diagnostico de transferencias, no una prueba de draws.
Cada upload de textura hace una copia CPU completa, una nueva imagen/staging y
un submit dedicado; aun no hay batching ni reutilizacion de staging. Es un
contrato inicial de correccion, no una estrategia final de rendimiento.
Los draws inmediatos hacen un submit dedicado y recompilan el pipeline por draw.
Faltan batching, caches de pipeline/layout/sampler y arenas de UBOs/descriptor sets.
No debe usarse esta estrategia inicial para estimar el rendimiento del juego.
Las barreras son conservadoras. No se atribuyen mejoras de FPS a este modulo.

## Prueba de integracion

`test_d3d9_vk_backend.py` traduce 79 callers x86 propios usando el SDK del port
y ejecuta vtables de superficies, targets, Clear, viewport/scissor y escenas.
Reutiliza codigo auxiliar del harness previo, pero nunca llama a su baseline
NULLREF: comprueba que no se han creado sus objetos D3D9 Windows.

Resultado final `local/vk-factory-com03/validation.json`:

- 2.304 pixeles exactos de color: full clear, parche, copia de rectangulos y
  recorte viewport/scissor. 141 submissions y 141 completions reales en el tramo COM, incluidos transfers.
- Dos slots ocupados y tercer lote BUSY sin perderlo. Libera/unbind superficies
  antes de completar: se retienen hasta ultimo lote, y sus IDs obsoletos se rechazan.
- Presupuesto insuficiente y formato no soportado conservan estado/memoria.
  Reciclado de slots de recursos y cierre final sin VkDeviceMemory propia viva.
- 821 bytes de VB/IB recuperados por copias GPU, incluidos rangos no alineados,
  buffers de 1..8 bytes, indices de 16/32 bits y cambios parciales/DISCARD.
  Tres fallos adicionales durante upload (allocation/map/flush), con reintento.
  Timeout seguido de DISCARD retiene ambas versiones hasta completar el fence.
  Liberacion publica, bindings, Get con resurreccion y IDs obsoletos comprobados.
- 692 bytes de texturas copiados por GPU: cinco formatos, mipmaps, conversion
  de canales, pitch con padding, parche preservando otros pixels/mips y DISCARD.
  Cinco fallos de upload inyectados (memory/image/view/map/flush) con reintento.
  Ring lleno no pierde el lock guest. READONLY no envia; bindings/Get retienen.
  Readback con timeout conserva imagen vieja mientras DISCARD sube una nueva.
  Release antes del fence tanto de upload como de readback mantiene el recurso.
- Tres fallos adicionales de envio de textura (EndCommandBuffer, ResetFences,
  QueueSubmit) invalidan el backend y permiten cierre sin handles reales vivos.
- Dos modulos shader Vulkan reales desde tokens DX9 por COM; dos fallos
  CreateShaderModule inyectados y reintento. Catalogo, SPIR-V y planes conservan
  sus copias despues de sobrescribir el input del registro. Rechaza tokens no
  registrados, cabecera incorrecta, plan invalido y registro duplicado.
- 96 bytes de constantes empaquetadas desde setters x86, incluidos int con signo,
  BOOL no canonico y padding cero. Salida intacta si no hay shaders o capacidad.
  Bind/Get resucita refs; cierre de device suelta shader/declaracion enlazados.
- Copia nativa de declaracion y roundtrip COM, rechazo de semantica duplicada,
  tipos packed pendientes e IDs obsoletos. Tres selecciones scaled comprobadas
  consultando caps; no es una prueba GPU de decodificacion de esos tres tipos.
- Seis draws iniciales (cinco indexed y uno no indexed), 6.144 pixels BGRA8 exactos
  desde este backend: textura por cuadrantes, UBOs, vertex offset, StartIndex,
  BaseVertexIndex=-1, LEQUAL, depth deshabilitado y scissor. Mirror se comprueba
  con coordenadas que invierten U, no solo con un periodo equivalente a wrap.
- Cambia VB/IB, textura y constantes antes de observar el primer fence: la
  imagen anterior conserva sus datos. Refs publicas y bindings desaparecen
  antes del ultimo fence; los recursos permanecen hasta su confirmacion.
- Nueve fallos de construccion de draw: memory/map/flush, sampler, pipeline
  layout, descriptor pool, pipeline parcial real, descriptor sets y framebuffer.
  Se reintenta sin fugas ni incrementar draws/submissions.
- Tres fallos de EndCommandBuffer/ResetFences/QueueSubmit en draws invalidan el
  backend. Idle desconocido conserva contexto y recursos; reintento de destroy
  tras confirmacion real libera los handles controlados.
- Veinte draws adicionales, 20.480 pixels de estados: seis casos con ambos
  sentidos de triangulo y los tres modos de culling; strip/fan indexed y no
  indexed; cinco operaciones de blend, alfa comun/separado, factor constante,
  mascara R/B y SRCALPHASAT. Blend se compara con formulas independientes con
  tolerancia de un nivel UNORM8; culling/topologias/mascara son exactos.
- Tres controles negativos en procesos separados: fuerza clamp, invierte culling
  o desactiva blend. Cada uno debe detectar un pixel incorrecto; se registra en
  negative_controls. Esas salidas anticipadas no prueban balance de handles.
- Fabrica guest32: Direct3DCreate9 por IAT y CreateDevice con 32 comprobaciones
  de parametros, identidad/parent y formato. Caps completas se rechazan con
  salida intacta. La misma instancia despues ejecuta los 26 draws.
- Nueve escenarios de fallo mediante dispatch proxy de test: allocation,
  ImageView, Fence, readback timeout, MapMemory, rama invalidate, Framebuffer,
  QueueSubmit y cuarentena ante fallo de DeviceWaitIdle. Los handles reales
  creados por el backend se cuentan y balancean en el proxy.

Los fallos/timeouts anteriores son inyectados; no se presentan como fallos
observados del driver. El proxy puede ocultar HOST_COHERENT para ejercitar
flush/invalidate: no demuestra comportamiento fisico de toda memoria no coherente.
La profundidad se comprueba mediante sus resultados de color; stencil no tiene
readback directo ni pruebas de stencil en draws. Hay draws GPU propios; no hay
frames de Fallout. No se ha ejecutado el modulo en Switch/NVK. El target
CMake `fnv_d3d9_vk_backend` compila como AArch64 con `-Werror`; eso no sustituye HW.

## Uso y siguientes conexiones

```powershell
python pc/tests/test_d3d9_vk_backend.py --sdk /externo/xboxrecomp --python-deps /externo/python-deps --headers /externo/mesa/include --shaders /privado/shaders-propios --linked-shaders /privado/fixture-linked --output local/vk-backend --cc /tools/zig.exe cc
cmake --build /build/arm64 --target fnv_d3d9_vk_backend
```

Usar un build ARM64 configurado con FNV_VULKAN_HEADERS y un SDP de shaders
propios del fixture anterior y sus normal.vs.spv/normal.ps.spv enlazados, con
planes conocidos de 64/32 bytes. No es un runner generico para otra pareja. Resultados
privados de desarrollo no son carpetas requeridas en la SD.

La [fabrica preparada](native-d3d9-factory.md) ya enlaza Direct3DCreate9 por IAT
y publica una vez el device real tras validar CreateDevice. Es un adaptador
virtual de tamano fijo; faltan caps completas y recreacion de superficie perdida.
La [ruta WSI host](native-vulkan-presentation.md) ya presenta por Vulkan Windows
y compila el enlace NWindow; su subconjunto sintetico hw01 ya paso en Switch.
Permanecen pendientes catalogo/interfaz de shaders del juego, mas topologias,
lineas/puntos, stencil y otros estados, DXT2/4, superficies de textura,
cache de pipelines y Reset con NVK. Present COM ya usa la ruta WSI; falta su
validacion ampliada de Reset/DXT en consola. Las pruebas graficas
anteriores se conservan como pruebas separadas; este runner usa el proveedor
propio para las operaciones descritas.
El arranque del motor desde EXE y sus servicios Windows permanecen independientes.

Semantica de Clear y scissor contrastada con documentacion primaria:
[Clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-clear),
[Scissor Test](https://learn.microsoft.com/en-us/windows/win32/direct3d9/scissor-test).

La seleccion de formatos scaled se contrasto con [D3DDECLTYPE de Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3ddecltype)
y [vertex input de Vulkan](https://docs.vulkan.org/spec/latest/chapters/fxvertex.html).
No se interpreta USCALED/SSCALED como normalizacion a 0..1.

Culling y blending se contrastan con [D3DCULL](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcull),
[D3DBLEND](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblend),
[D3DBLENDOP](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dblendop)
y [VkFrontFace](https://docs.vulkan.org/refpages/latest/refpages/source/VkFrontFace.html).
