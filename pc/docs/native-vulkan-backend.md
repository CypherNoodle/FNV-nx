# Proveedor D3D9 propio: dispositivo y superficies Vulkan

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
- Readback diagnostico BGRA8, staging propio y salida intacta en error. Si
  vence una espera, staging e imagen siguen retenidos. Comprueba coherencia de
  la memoria y usa invalidate cuando corresponde. Su timeout aplica a cada
  espera interna; puede hacer hasta tres, ademas del trabajo de CPU/driver.

El backend serializa su estado/cola con mutex nativo. El caller garantiza la
vida del loader y propiedad exclusiva al destruir. Hay limites explicitos:
2..256 recursos, 1..128 comandos por lote, hasta 32 rectangulos por Clear.
No devuelve exito para metodos pendientes. Shader creation devuelve error;
draws, buffers y texturas todavia no tienen callbacks en este proveedor.

La primera referencia de device se transfiere al consumidor COM. Recursos
con referencias publicas retienen el device; bindings y comandos no forman
ciclos. Al llegar a cero se sueltan bindings/targets implicitos. Frames
enviados solo se retiran tras un fence observado. Si falla la grabacion/envio,
el backend se considera inutilizable y conserva la propiedad para su cierre.
Una finalizacion desconocida en DeviceWaitIdle conserva el contexto completo;
el caller puede reintentar destroy. No se inventa un fence completado.

Render passes/framebuffers se conservan por lote hasta el fence. Se crean
por comando en esta primera version; faltan sus caches y medir coste en NVK.
Las barreras son conservadoras. No se atribuyen mejoras de FPS a este modulo.

## Prueba de integracion

`test_d3d9_vk_backend.py` traduce 37 callers x86 propios usando el SDK del port
y ejecuta vtables de superficies, targets, Clear, viewport/scissor y escenas.
Reutiliza codigo auxiliar del harness previo, pero nunca llama a su baseline
NULLREF: comprueba que no se han creado sus objetos D3D9 Windows.

Resultado final `local/vk-backend-com03/validation.json`:

- 2.304 pixeles exactos de color: full clear, parche, copia de rectangulos y
  recorte viewport/scissor. 9 submissions y 9 completions reales en el tramo COM.
- Dos slots ocupados y tercer lote BUSY sin perderlo. Libera/unbind superficies
  antes de completar: se retienen hasta ultimo lote, y sus IDs obsoletos se rechazan.
- Presupuesto insuficiente y formato no soportado conservan estado/memoria.
  Reciclado de slots de recursos y cierre final sin VkDeviceMemory propia viva.
- Nueve escenarios de fallo mediante dispatch proxy de test: allocation,
  ImageView, Fence, readback timeout, MapMemory, rama invalidate, Framebuffer,
  QueueSubmit y cuarentena ante fallo de DeviceWaitIdle. Los handles reales
  creados por el backend se cuentan y balancean en el proxy.

Los fallos/timeouts anteriores son inyectados; no se presentan como fallos
observados del driver. El proxy puede ocultar HOST_COHERENT para ejercitar
invalidate: no demuestra comportamiento fisico de toda memoria no coherente.
Depth/stencil se graban, pero el readback verifica color. No hay draws GPU ni
frames de Fallout. No se ha ejecutado el modulo en Switch/NVK. El target
CMake `fnv_d3d9_vk_backend` compila como AArch64 con `-Werror`; eso no sustituye HW.

## Uso y siguientes conexiones

```powershell
python pc/tests/test_d3d9_vk_backend.py --sdk /externo/xboxrecomp --python-deps /externo/python-deps --headers /externo/mesa/include --shaders /privado/shaders-propios --output local/vk-backend --cc /tools/zig.exe cc
cmake --build /build/arm64 --target fnv_d3d9_vk_backend
```

Usar un build ARM64 configurado con FNV_VULKAN_HEADERS y un SDP de shaders
propios del fixture anterior (aunque esta prueba no crea shaders). Resultados
privados de desarrollo no son carpetas requeridas en la SD.

Crear este dispositivo Vulkan **no implementa todavia** Direct3DCreate9 ni
IDirect3D9::CreateDevice guest. Faltan esa fabrica/caps, buffers/texturas/shaders
y draws en este backend, superficies de textura, otras operaciones y estados,
cache de pipelines y swapchain/Present/Reset de NVK. Las pruebas graficas
anteriores siguen separadas hasta conectar sus piezas a este proveedor.
El arranque del motor desde EXE y sus servicios Windows permanecen independientes.

Semantica de Clear y scissor contrastada con documentacion primaria:
[Clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-clear),
[Scissor Test](https://learn.microsoft.com/en-us/windows/win32/direct3d9/scissor-test).
