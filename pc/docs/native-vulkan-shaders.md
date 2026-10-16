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

La auditoria verifica por SHA-256 los 1.809 bytecodes privados ya inventariados.
Con la biblioteca sin cambios, 843 se traducen y 966 se rechazan. Hay 3.899
apariciones de centroid no soportado y cinco de direccionamiento relativo
de un array indeterminado. Una aceptacion de traduccion no implica enlace,
validacion semantica ni imagen correcta. Ninguno de estos 1.809 shaders se ha
dibujado en esta prueba. No borrar centroid para hacer pasar el compilador.

La siguiente integracion debe preservar esa semantica o elegir otro traductor,
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
```

Solo nuestro adaptador, tests y documentos van al repositorio. EXE, datos,
shaders del juego, DLLs, bibliotecas externas, binarios y salidas permanecen
privados; no se crean directorios adicionales de runtime en la SD.
