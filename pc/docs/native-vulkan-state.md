# Profundidad y sampler COM hasta Vulkan

`pc/native/d3d9_vk_state.c/.h` traduce valores D3D9 a estructuras reales
`VkPipelineDepthStencilStateCreateInfo` y `VkSamplerCreateInfo`. No llama Vulkan,
asigna memoria ni mantiene estado global. Recibe snapshots inmutables y escribe
la salida solo al terminar la validacion. El target opcional CMake
`fnv_d3d9_vk_state` usa encabezados externos mediante `FNV_VULKAN_HEADERS`.
No incorpora una copia de Mesa al repo ni modifica el driver.

## Subset inicial

Profundidad: ZENABLE 0/1 (W-buffer rechazado), ZWRITEENABLE y las ocho funciones
de comparacion. Write se normaliza como BOOL. No cubre stencil, bounds, depth
bias ni el resto de estados raster/blend. La semantica Vulkan ignora escrituras
de profundidad cuando el test esta desactivado.

Sampler: point/linear para min/mag, mip none/point/linear, direcciones wrap,
mirror, clamp y border. Border acepta transparent black, opaque black o white;
un color arbitrario con direccion border falla. Si border no se usa, ese valor
se conserva en el input pero no afecta al sampler. Rechaza anisotropia,
mirror-once y MAXMIPLEVEL distinto de cero. sRGB depende de image views y no es
una propiedad implementada por este modulo. El bias debe ser finito y estar
dentro del limite real del dispositivo. Mip none fija maxLod cero; point/linear
permiten hasta levels-1, con 1..32 niveles aportados por el caller.

Estos limites devuelven INVALIDCALL antes de escribir; no son capacidades
completas del proveedor ni un motivo para descartar su implementacion futura.
Las funciones no seleccionan pipelines, cachean objetos ni sincronizan hilos;
el proveedor debe mantener snapshots y ownership de sus recursos.

## Conexion y pruebas

El fixture COM/Vulkan ahora ejecuta 41 callers x86. Su callback de estados
mantiene tres valores de profundidad y diez del sampler cero; empieza con
defaults leidos de NULLREF. Valida un candidato antes de modificar el oracle
y publicar el estado. Getters comprueban que dos setters rechazados conservan
el valor anterior. Otras clases de estado se rechazan en este fixture reducido.

Los tres draws COM conservan samplers con U clamp, wrap y mirror. La geometria
lleva respectivamente U, U+1 y U+2. Vulkan crea un sampler por snapshot,
revalidado con maxSamplerLodBias real del dispositivo. RGB y depth producen
4.096 pixeles exactos en siete draws Vulkan. La segunda variante de profundidad
se configura con setters COM tras los tres draws: el fixture la elige para su
cuarta pasada Vulkan sin test de profundidad. No representa un cuarto draw COM
ni seleccion automatica de pipelines. Las variantes/pasadas siguen explicitas.

El control negativo `FNV_TEST_FORCE_CLAMP` existe solo en el helper de test:
fuerza U clamp y debe fallar por pixeles de la segunda pasada. El runner
comprueba ese fallo esperado y guarda `wrong_clamp_detected=true`; no es una
opcion del port ni del INI del usuario. El snapshot permanece estable aunque
se modifique el estado actual tras registrar los draws.

Resultados privados aceptados:

- `vk-state-unit02`: 48 combinaciones de profundidad, 771 de sampler y 25
  rechazos sin modificar salida. Aserciones explicitamente activadas.
- `state-com-gpu02`: 41 callers, tres snapshots por recurso, 4.096 pixeles,
  fence real y control negativo detectado. Driver GPU Windows, no NVK.
- `state-vulkan-regression01`: fixture Vulkan independiente PASS.
- `state-com-regression01`: 461 checks escalares y 128 espaciales frente a
  NULLREF. No usa este mapper para dibujar ni acredita soporte Vulkan para
  todos esos estados.
- CMake ARM64 `fnv_d3d9_vk_state`: PASS con `-Werror`.

Los filtros linear/mip/border y comparadores distintos de LEQUAL se comprueban
como traduccion de estructuras; no se atribuye a este fixture cobertura GPU
de todos ellos. Viewport, scissor, raster/blend, targets, cache general de
recursos/pipelines y presenter NVK siguen pendientes. No ejecuta Fallout ni
hardware Switch y no produce un nuevo NRO grafico ni medidas de FPS.

```powershell
python pc/tests/test_vk_state.py --headers /externo/mesa/include --output local/state-unit --cc /tools/zig.exe cc
python pc/tests/test_d3d9_uniform_state.py --sdk /externo/xboxrecomp --python-deps /externo/python-deps --headers /externo/mesa/include --uniform-fixture local/uniform-fixture --output local/state-com --cc /tools/zig.exe cc
cmake -S pc/native -B /build/arm64 -DFNV_VULKAN_HEADERS=/externo/mesa/include
cmake --build /build/arm64 --target fnv_d3d9_vk_state
```

Usar el build ARM64 ya configurado y el [fixture de constantes](native-vulkan-uniforms.md).
Resultados y herramientas de desarrollo no se copian a la SD.
