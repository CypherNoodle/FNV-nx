# Traductor offline para el renderer nativo

Esta herramienta usa fuentes del SDK Horizon fijado por el proyecto, junto al
runtime Windows ReXGlue 0.10.0 y las dependencias del mismo SDK. Sus DLLs no
exportan las clases privadas del traductor, por lo que esas fuentes se compilan
en el ejecutable; permanecen como dependencia externa con su licencia.

Construir en el entorno C++ de Microsoft con Clang:

```powershell
cmake -S experimental/shader-tools -B local/shader-tools-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=RUTA/REXGLUE-0.10.0 -DFNV_HORIZON_SDK=RUTA/SDK
cmake --build local/shader-tools-build
# Mantener las DLLs del runtime ReXGlue en PATH durante la traducción.
python tools/retranslate_deko_shaders.py --capture RUTA/CAPTURA --translator local/shader-tools-build/fnv-shader-translate.exe --output local/native-spv
python tools/build_deko_shader_prototype.py --shaders local/native-spv --output local/native-dksh --spirv-cross RUTA/spirv-cross --spirv-val RUTA/spirv-val --uam RUTA/uam
```

El input es microcódigo privado de Xbox 360 en el byte order del dump del SDK.
Se conservan la identidad y la modificación de cada variante. Se seleccionan
render targets host y la ruta propia del SDK sin demote: el rechazo de píxeles
se aplaza para conservar el flujo guest y sus derivadas. No se reemplazan
instrucciones demote de un SPIR-V terminado por discard.

Las 68 variantes de la captura alpha05 se retradujeron y compilaron a DKSH,
incluidos los pixel shaders antes bloqueados por demote/interlock. Los módulos
adaptados pasaron validación SPIR-V y comprobaciones de ABI/formato DKSH.
Esto prueba cobertura de compilación de esa captura; no de todo el juego ni
equivalencia visual. Bindings, targets, constantes, formatos y salida GPU deben
comprobarse al conectar los recursos reales al backend.

Los microcódigos, SPIR-V, DKSH y manifiestos derivados se guardan en `local/`.
No distribuirlos en el repositorio ni compilar UAM durante el juego en Switch.
