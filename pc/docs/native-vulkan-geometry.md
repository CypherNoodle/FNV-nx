# Geometria COM hasta Vulkan

El fixture `test_d3d9_uniform_state.py` ahora conecta tambien uploads y bindings
VB/IB desde callers x86 recompilados. Crea la declaracion FLOAT2 POSITION y
FLOAT2 TEXCOORD, escribe buffers con Lock/Unlock y los enlaza por sus vtables
COM guest. No usa vertices fijos dentro del renderer Vulkan en este modo.

Los callbacks de prueba conservan copias propias de bytes tras Unlock exitoso.
En cada draw, el nuevo modulo C `d3d9_geometry.c` comprueba los accesos. Luego
el fixture copia las versiones utilizadas. Tres ordenaciones distintas de los
vertices, con sus indices correspondientes, dibujan la misma figura. Despues
se sobrescriben los buffers origen con ceros: los draws registrados conservan
sus datos y siguen produciendo 4.096 pixeles exactos en siete draws Vulkan.
Los VkBuffer y su memoria solo se destruyen despues de un fence real.

El caso comprueba offset de stream 16, stride 16, startIndex 2 e indices
16-bit con BaseVertexIndex -1. Una ventana de indices invalida rechaza el draw
y cancela su reserva de constantes; no consume permanentemente un slot.
La declaracion y el pipeline corresponden explicitamente a estos shaders propios.
El caso no acredita enlace automatico de cualquier declaracion con cualquier VS.

## Validador reusable

`fnv_d3d9_geometry_validate` recibe versiones inmutables y devuelve intervalos
de bytes por stream y buffer de indices, sin asignar memoria ni llamar Vulkan.
Cuenta vertices segun las seis topologias D3D9, lee indices little-endian de
16/32 bits sin asumir alineamiento, y aplica el base con signo en 64 bits.
Valida la ventana MinVertexIndex/NumVertices, limites de buffers, declaracion
acotada, tamanos de sus 17 tipos y streams utilizados. Soporta stride cero.
No modifica la salida al fallar. Rechaza draws vacios, instancing y metodos de
declaracion distintos de DEFAULT; son limites explicitos de este modulo inicial.
No convierte formatos ni comprueba caps del dispositivo.

El caller garantiza que los punteros cubren las longitudes declaradas y que
nadie modifica esos bytes durante la validacion. Los intervalos delimitan los
accesos reales, incluidos huecos, y no deben usarse para reinterpretar offsets
o indices sin el ajuste correspondiente. Un backend puede conservar el buffer
entero o copiar un subrango y ajustar su binding; el fixture hace lo segundo.

Actualmente se examinan todos los indices consumidos: coste O(numero de indices).
Para el renderer definitivo conviene cachear los limites por version y rango,
invalidandolos al actualizar el buffer. No es una optimizacion de FPS demostrada.
No omitir la validacion ni cachear solo por handle mutable.

## Evidencia y limites

Resultados privados aceptados: `geometry-access-unit02` (4.000 casos
diferenciales, 17 formatos y 39 rechazos), `geometry-com-gpu01` (32 callers x86,
tres versiones de geometria, 4.096 pixeles y fence real), y
`geometry-vulkan-regression01` (fixture Vulkan independiente). El target CMake
`fnv_d3d9_geometry` compila para AArch64 con warnings tratados como errores.

NULLREF Windows sigue siendo el oracle de objetos y referencias en esta prueba.
Los callbacks de recursos de produccion Vulkan, su gestor de memoria/versiones,
texturas, estados, superficies y presenter NVK aun no estan completos. Las
copias CPU de geometria se conservan hasta terminar el test; no constituyen un
anillo de recursos general ni una prueba de reciclado de recursos GPU.
No se ejecuta Fallout, ni el driver NVK, ni hardware Switch en estos tests.
No hay NRO grafico nuevo ni medidas de rendimiento del port PC.

```powershell
python pc/tests/test_geometry_access.py --output local/geometry-unit --cc /tools/zig.exe cc
python pc/tests/test_d3d9_uniform_state.py --sdk /externo/xboxrecomp --python-deps /externo/python-deps --headers /externo/vulkan-headers/include --uniform-fixture local/uniform-fixture --output local/geometry-com --cc /tools/zig.exe cc
cmake --build /build/arm64 --target fnv_d3d9_geometry
```

Preparar primero el [fixture de constantes](native-vulkan-uniforms.md). Usar
directorios nuevos para resultados. Las herramientas y datos privados de
desarrollo no crean carpetas de runtime en la SD.
