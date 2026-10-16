# Constantes D3D9 y bloques Vulkan

El nuevo modulo `native/d3d9_uniform_pack.c` convierte snapshots de bancos
globales D3D9 en bloques definidos por cada shader traducido. Es codigo propio
portable, sin dependencia Windows, Vulkan, SDK x86 ni biblioteca de traduccion.
Ya compila como biblioteca ARM64. Todavia no es el proveedor COM Vulkan del
motor ni esta enlazado al NRO de diagnostico existente.

## Reflection y contrato

El enlazador offline propio exporta los rangos publicos de uniformes que
devuelve MojoShader: kind, registro inicial, cuenta y caracter local.
`spirv_uniform_layout.py` contrasta esos datos con el SPIR-V enlazado:

* Un Block UBO con pointer Uniform, miembros array, tipos float4/int4/int32,
  longitud literal, ArrayStride 16 y MemberDecorate Offset explicito.
* Los indices de slot integer nombrados cN/iN/bN por el ABI de este traductor.
  No se asume que el orden de los uniformes determine el orden de memoria.
* Cada registro global tiene un rango completo y cada slot del bloque queda
  cubierto una sola vez. Arrays relativos deben tener su rango conocido.
* DEF/DEFI/DEFB locales no se copian desde los bancos de la API. Layouts
  desconocidos, regiones ausentes, ambiguas o solapadas se rechazan.

El resultado por etapa contiene bytes/set/binding y spans
`{kind, first, count, offset, stride}`. `translate_d3d9_pair.py` lo guarda en
translation.json con los hashes de bytecode y herramientas. Es necesario
reconstruir el enlazador: versiones anteriores sin metadatos fallan expresamente.
Esto soporta el ABI examinado de MojoShader, no cualquier modulo SPIR-V.

## Modulo nativo

`fnv_d3d9_uniform_pack` recibe un plan inmutable, punteros/counts a los tres
bancos y un destino con capacidad. Los valores float4/INT son DWORD opacos:
se copian bits, incluidos NaN, cero negativo y enteros con signo. BOOL conserva
su valor API en el banco, pero en el bloque se escribe 0 o 1. El padding se
inicializa a cero. No se convierte todo a float ni se toman valores locales
del shader de los registros globales.

Todos los rangos, strides, offsets, capacidades y alias se comprueban antes
de modificar la salida. Un fallo deja el destino intacto. Limites internos:
64 KiB, 512 spans, 256 float4 y 16 INT/BOOL; el futuro proveedor debe aplicar
tambien las caps anunciadas por etapa y maxUniformBufferRange del dispositivo.
No hay asignaciones, bloqueos, subidas, submits ni fences dentro del modulo.
El caller debe mantener el snapshot estable y proteger la vida de los buffers
GPU que consuman el resultado. La validacion actual es por llamada, incluida
la deteccion de solapamientos; no es una medicion de rendimiento del juego.

## Pruebas y alcance

`test_uniform_pack.py` compila la implementacion C host y la compara con una
referencia Python independiente en 600 casos reproducibles y 16 rechazos.
Comprueba preservacion de bancos, padding, bits, BOOL no canonico, limites,
alias y salida intacta en fallos. Tambien convierte un array HLSL relativo
`palette[4]` en c20: el plan correcto es cuatro registros/64 bytes.

El fixture GPU usa VS de ensamblador D3D9 propio y PS HLSL propio, compilados
con D3DCompiler47 de System32. HLSL no permite enlazar ese parametro a i2;
el ensamblador hace explicitos los bancos globales INT/BOOL:

| Etapa | Datos API | Ubicacion UBO observada |
| --- | --- | --- |
| VS | c7, c9, i2, b3 | offsets 0, 16, 32, 48; 64 bytes |
| PS | c3, c12 | offsets 0, 16; 32 bytes |

El VS solo queda en pantalla si REP i2 itera dos veces y b3 es verdadero.
La profundidad combina c7 y c9. El PS combina tinte c12 con alpha de c3.
Los registros float globales no usados contienen NaN; los DEF locales del VS
siguen siendo valores internos. Los bloques que consume Vulkan se producen
llamando al empaquetador C nativo, no a la referencia Python.

La prueba Vulkan acepta un archivo privado FNVUBO01 con esos seis bloques,
versionados para tres draws; verifica 4.096 pixeles exactos en siete draws.
Pasa tambien la ruta anterior sin archivo de constantes y con centroid.
No se ejecuta Fallout ni se dibujan shaders del juego. El test solo espera
el fence al leer resultados; no establece el diseño final de frames en vuelo.

Resultados locales: uniform-pack05 PASS; uniform-draw01 PASS; default-regression01
PASS; link-regression01 PASS; CMake target fnv_d3d9_uniform_pack ARM64 PASS.
Pack05 incluye el caso relativo y produce byte a byte los mismos modulos y
bloques que pack03, consumidos por uniform-draw01, por lo que esa prueba GPU
aceptada se conserva sin repetirla.

## Shader restante

El shader privado PARTICLE.vso aun se rechaza por el traductor. Su CTAB declara
InstanceData en c18 con cuenta 2 (c18/c19), pero hay referencias relativas cuyo
registro base es c20. El rango efectivo no puede ampliarse solo por esa tabla.
Es una limitacion de esta traduccion, no una prueba de que el shader del juego
sea invalido. Se conserva el diagnostico, sin alterar bytecode, CTAB ni rango.

`inspect_shader_ctab.py` queda como herramienta generica de lectura acotada
de SM2/3, strings y registros declarados. Los campos Reserved se conservan
opacos: los compiladores examinados los dejan distintos de cero. No ejecuta
ni repara shaders. Su salida privada no se publica.

## Reproducir

Usar salidas nuevas y reconstruir el enlazador actualizado segun el
[documento de shaders](native-vulkan-shaders.md).

```powershell
python pc/tests/test_uniform_pack.py --linker local/shader-build/link_d3d9_spirv.exe --output local/uniform-fixture --cc /tools/zig.exe cc
python pc/tests/test_vulkan_pipeline.py --headers /externo/vulkan-headers/include --fixture local/uniform-fixture/gpu-fixture --uniform-data local/uniform-fixture/uniform-data.bin --output local/uniform-gpu --cc /tools/zig.exe cc
python pc/tools/inspect_shader_ctab.py /privado/shader.bin --output local/ctab.json
cmake --build /build/arm64 --target fnv_d3d9_uniform_pack
```

La siguiente integracion es mantener estos bancos desde upload_constants,
aplicar planes por shader y enlazar recursos/versiones del proveedor COM a
Vulkan y al presenter NVK. CPU, arranque del motor y servicios Windows siguen
siendo requisitos separados; este lote no requiere otra prueba de consola.

La [gestion de bancos y versiones](native-vulkan-uniform-state.md) ya prueba
los callbacks COM con snapshots conservados hasta un fence Vulkan real.
Sigue pendiente el proveedor completo de recursos/estados y el presenter Switch.
