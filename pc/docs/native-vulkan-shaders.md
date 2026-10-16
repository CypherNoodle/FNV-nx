# Shaders D3D9 y primer pipeline Vulkan propio

El backend activo es Vulkan sobre Mesa/NVK para Nintendo Switch. Deko3d
queda pausado. Xbox sigue pausado en main. Este lote implementa herramientas
offline propias y una prueba grafica desktop; no cambia el NRO de diagnostico
ya probado y no ejecuta la entrada de Fallout.

## Conversion y enlace

`build_shader_linker.py` compila `link_d3d9_spirv.c` con una copia externa sin
cambios de MojoShader. Se examino el checkout ad5dff84830c2863c841f4b1f4e3df78c705b383;
el informe conserva SHA-256 de los 17 archivos fuente/header consumidos y
comprueba que no cambien durante el build. La biblioteca no se modifica ni se
incluye aqui; no se envia ninguna contribucion a upstream.

El adaptador usa el perfil `spirv` para Vulkan, no `glspirv`. Recibe dos
bytecodes D3D9 y una declaracion de atributos: usage, indice y element format
de los enums publicos de MojoShader. Rechaza etapas incorrectas, bytecodes
invalidos, atributos duplicados/fuera de rango, entradas ausentes y semantics
del pixel shader sin salida correspondiente del vertex shader. Las entradas
pixel builtin/legacy que no cumplan ese contrato siguen sin implementarse.

El enlace publico de MojoShader ajusta tipos de entrada y varyings. Devuelve
el tamano de una tabla auxiliar que se resta de AMBOS modulos antes de escribir
`.spv`: 7.440 bytes por etapa en este build. No pasar el output crudo con esa
tabla a `vkCreateShaderModule`. Las salidas usan creacion exclusiva; un fallo
al abrir la segunda no deja la primera como resultado parcial y nunca
sobrescribe artefactos aceptados.

`inspect_spirv.py` limita tamano, comprueba framing/header, recorrido de
instrucciones, entrypoint y descriptor set/binding explicitos. Refleja
inputs/outputs y recursos. No es un validador semantico SPIR-V: no demuestra
tipos, dominancia, reglas Vulkan completas ni equivalencia del shader.

Para el par propio se observa VS UBO set 1/binding 0, PS sampler set 2/binding 0
y PS UBO set 3/binding 0. Set 0 queda vacio porque este VS no muestrea texturas.
La futura subida de constantes requiere su layout/reflection por variante;
estos 16 bytes por etapa no justifican copiar el banco completo sin adaptacion.

## Prueba grafica

`test_d3d9_spirv.py` compila nuestro HLSL de posicion/UV/depth y textura*tint
mediante D3DCompiler47 de System32, identificada por hash. No se entrega esa
DLL a la consola. Convierte el par y comprueba interfaces, descriptores,
ocho rechazos y cinco modulos malformados.

`test_vulkan_pipeline.py` compila un ejecutable propio con headers Vulkan
externos identificados por hash. Carga Vulkan desde System32 y crea un
dispositivo desktop, sin ventana ni swapchain. No usa DXVK, Wine o el juego.

Usa targets host RGBA8/D32_FLOAT de 32x32, textura 2x2 de cuatro tonos, point
sampling, vertices float2/float2 e indices uint16 con base -1. Viewport de altura
negativa Vulkan 1.1 conserva Y arriba y UV de D3D en este caso. Cada lectura
comprueba orientacion y los cuatro tonos, no solo un color. Las constantes se
versionan en buffers separados para que cada draw vea sus propios valores.

Cuatro resultados: rojo a profundidad .75; verde .25 delante del rojo; azul
.9 rechazado detras del verde; azul con profundidad desactivada. Se hacen
siete draws en cuatro pasadas con clear independiente y se comparan 4.096
pixeles RGBA exactos. Las transiciones de textura/target, dependencia antes de
reutilizar attachments, lectura host y fence de readback estan explicitas.
La espera del fence es propia de una prueba de lectura, no el diseno final
de frames en vuelo.

Resultado local aceptado: shader-test03 y vulkan-draw-host03 PASS, dos pipelines,
siete draws, 4.096 pixeles. Dispositivo desktop vendor 4318/device 8644.
Esto acredita este pipeline propio en ese driver; no es un frame de Fallout,
un proveedor COM Vulkan completo ni una medida de rendimiento de Switch.

## Catalogo privado y limite real

La auditoria inicial verifica por SHA-256 los 1.809 bytecodes privados ya inventariados.
Con la biblioteca sin cambios y sin adaptador, 843 se traducen y 966 se rechazan. Hay 3.899
apariciones de centroid no soportado y cinco de direccionamiento relativo
de un array indeterminado. Una aceptacion de traduccion no implica enlace,
validacion semantica ni imagen correcta. Ninguno de estos 1.809 shaders se ha
dibujado en esta prueba. No borrar centroid para hacer pasar el compilador.

## Adaptador centroid, 16 de octubre

`translate_d3d9_pair.py` conserva centroid en una capa propia fuera de la
biblioteca externa. El [DCL de D3D9](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dcl-usage---ps)
marca el input; la [decoracion Centroid de Vulkan](https://docs.vulkan.org/spec/latest/chapters/shaders.html)
restringe la posicion de interpolacion al interior de la primitiva. Ese es el
contrato que se representa, sin sustituir centroid por interpolacion central.

El recorrido acotado SM2/SM3 salta comentarios, exige END, captura register y
semantic y comprueba la mascara de cada DCL centroid. Solo limpia ese bit en una copia
temporal. SM1 pasa sin modificaciones al traductor. No cambia instrucciones,
literales, CTAB ni shaders originales. Los casos centroid desconocidos o
invalidos se rechazan. La copia de MojoShader y el enlazador C quedan intactos.

Tras el enlace y retirada de la tabla auxiliar, se encuentra el input SPIR-V
correspondiente por el ABI de nombres del traductor actual. Se exige un unico
Input del entrypoint con Location real y sin BuiltIn. TEXCOORD0 usa el input
especial ps_PointCoordOrTexCoord0; no se decora su copia Private. Se inserta
OpDecorate Centroid antes de la seccion de tipos, sin crear ids ni alterar
offsets de la tabla de enlace ya consumida. Conflictos Flat/Sample, duplicados
y mappings ausentes fallan. Este ABI esta limitado a la dependencia examinada;
no es un parser universal de cualquier SPIR-V.

Las pruebas propias centroid03 pasan: un unico bit DCL cambiado y restaurado
como decoracion, uno y dos varyings (TEXCOORD0/TEXCOORD1), comentario con falsos
tokens, SM2.1 t1 y diez rechazos. La prueba GPU centroid-draw03 pasa con el par
de dos varyings: siete draws y 4.096 pixeles exactos a 1x. No valida cobertura
de bordes MSAA ni equivalencia general de centroid entre D3D9 y Vulkan.

Con `audit_spirv_catalog.py --centroid`, 965 originales necesitan copia temporal.
El traductor acepta 1.808 de 1.809 inputs normalizados. Queda un shader con cinco
errores de array indeterminado; no se inventa el rango de ese array. La auditoria
conserva originales, hashes normalizados y declaraciones. Su informe marca
centroid_reapplied=false y linked_pairs_checked=0: NO ha enlazado/restaurado los
1.808 ni ha dibujado shaders del juego. El restablecimiento se demuestra en los
pares propios; falta auditar los pares/declaraciones que consume el motor.

La siguiente integracion debe ampliar la validacion de pares y MSAA,
implementar reflection/packing, conectar el proveedor COM D3D9 y comprobar
recursos/estados y presentacion con NVK en Switch. Se localizaron las bibliotecas
estaticas libvulkan/libnvk/libnak/libnir/libvtn/libnouveau_horizon conservadas
en el workspace, pero este test usa el driver desktop, no esas bibliotecas.

## Reproducir

Usar directorios de salida nuevos; los siguientes son ejemplos. Zig cc o un
compilador compatible con los flags GCC/Clang sirve para los ejecutables host.

```powershell
python pc/tools/build_shader_linker.py --mojoshader /externo/mojoshader --output local/shader-build --cc /tools/zig.exe cc
python pc/tests/test_d3d9_spirv.py --linker local/shader-build/link_d3d9_spirv.exe --output local/shader-fixture
python pc/tests/test_vulkan_pipeline.py --headers /externo/vulkan-headers/include --fixture local/shader-fixture --output local/vulkan-test --cc /tools/zig.exe cc
python pc/tools/audit_spirv_catalog.py --manifest /privado/manifest.txt --raw /privado/raw --linker local/shader-build/link_d3d9_spirv.exe --output local/catalog-audit
python pc/tests/test_spirv_centroid.py --linker local/shader-build/link_d3d9_spirv.exe --output local/centroid-fixture
python pc/tests/test_vulkan_pipeline.py --headers /externo/vulkan-headers/include --fixture local/centroid-fixture/gpu-fixture --output local/centroid-gpu --cc /tools/zig.exe cc
python pc/tools/translate_d3d9_pair.py --vs /privado/vs.bin --ps /privado/ps.bin --attributes /privado/attributes.txt --linker local/shader-build/link_d3d9_spirv.exe --output local/translated-pair
python pc/tools/audit_spirv_catalog.py --manifest /privado/manifest.txt --raw /privado/raw --linker local/shader-build/link_d3d9_spirv.exe --output local/centroid-catalog --centroid
```

Solo nuestro adaptador, tests y documentos van al repositorio. EXE, datos,
shaders del juego, DLLs, bibliotecas externas, binarios y salidas permanecen
privados; no se crean directorios adicionales de runtime en la SD.
