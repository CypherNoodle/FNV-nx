# Herramientas del port PC

Las fuentes reutilizables viven en esta carpeta, sus pruebas en `../tests`
y los componentes C del runtime en `../native`. Todo se versiona en `pc-port`.
Los resultados derivados del juego pertenecen a una carpeta privada `local/`;
no forman parte del codigo fuente de las herramientas.

## Inventario

| Componente | Funcion y reutilizacion |
| --- | --- |
| `aot_index.py` | Indice incremental y decode acotado para el analizador x86 del SDK externo. No depende de direcciones de FNV. |
| `aot_simd_bits.py` + `../native/fnv_simd_bits.h` | Extension explicita de 13 instrucciones SSE2 de bits/copias/barajado/shifts. Usa la API `RecompXmm` del SDK; no contiene codigo del juego. |
| `../native/pe_image.c/.h` | Carga de datos PE32/i386 y enlace IAT mediante un resolver del llamador. No ejecuta x86 ni implementa Windows. |
| `prepare_aot_image.py` | Valida identidad y empaqueta secciones PE en el contenedor x86 que espera el SDK; permite una captura de texto declarada. |
| `run_aot_analysis.py` | Orquesta scan/ABI/codegen con hashes y variantes separadas; no aplica firmas XDK al programa Windows. |
| `audit_aot_gaps.py` | Clasifica destinos pendientes, todos sus propietarios solapados y procedencia en C generado. Solo lectura. |
| `verify_aot_simd.py` | Compara baseline/variante, comprueba dispatch y compila a ARM64 todos los chunks cambiados. No enlaza el juego. |
| `audit_native_imports.py` | Inventario de imports que necesita el futuro runtime nativo. Un inventario no significa que existan sus bridges. |
| `audit_install.py` | Inspeccion PE y comprobaciones de la instalacion FNV; contiene reglas especificas de esta edicion. |

`audit_runtime.py`, `build_runtime.sh` e `integrate_autorun.py` pertenecen al
experimento anterior Wine/Autorun. No son dependencias de la ruta nativa activa.
Consultar `../docs/wine-experiment.md` antes de reutilizarlos.

## Uso en otro proyecto

1. Suministrar el SDK x86 de SH2/SH3 xboxrecomp como dependencia externa.
   No confundirlo con ReXGlue para PowerPC/Xbox 360. Conservar su licencia y
   verificar sus interfaces mediante las pruebas antes de cambiar de version.
2. Empezar con fixtures sinteticos y la identidad del ejecutable propio.
   No reutilizar la captura cargada, entradas, ABI, imports o datos de FNV.
3. Llevar juntos el overlay SIMD y su header; incluir `pc/native` y el runtime
   del SDK al compilar los C generados. La API de memoria/registros debe ser
   la misma o necesita un adaptador verificado.
4. Adaptar las convenciones del pipeline: hoy los nombres de contenedor e
   informes, el banner del generador y la comprobacion de Steam son de FNV.
   No se presenta como un paquete universal listo para cualquier juego.
5. Compilar y comparar semantica antes de integrar. Las pruebas ARM64 actuales
   usan devkitA64 GCC16.1.0 con `-mearly-ra=none`; otro compilador/version exige
   validacion propia. Una generacion exitosa no demuestra arranque ni FPS.

Los helpers SIMD no implementan FP/MXCSR ni SET O/productores de OF. Los
operandos legacy m128 desalineados paran con diagnostico; la entrega de #GP/
SEH al guest sigue pendiente. Las comprobaciones diferenciales usan Unicorn
x86/ARM64, no sustituyen la validacion de un runtime en consola.

## Verificacion

Python3, Capstone, Unicorn y devkitA64 son dependencias externas. Se puede
indicar una carpeta Python existente con `--python-deps`; estos scripts no
instalan paquetes automaticamente. `--cc` selecciona el compilador ARM64.
Los comandos se ejecutan desde la raiz del proyecto:

```sh
python pc/tests/test_aot_image.py --temp-dir /ruta/privada/tmp
python pc/tests/test_aot_index.py
python pc/tests/test_aot_gaps.py --python-deps /ruta/dependencias --temp-dir /ruta/privada/tmp
python pc/tests/test_aot_sdk_overlay.py --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias
python pc/tests/test_aot_simd_bits.py --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --cc /ruta/aarch64-none-elf-gcc.exe --output /ruta/privada/prueba-simd
```

Las salidas SIMD exigen un directorio nuevo y guardan hashes, flags del
compilador y limites de lo probado. Para el pipeline y sus comandos completos,
ver `../docs/native-aot.md`. La configuracion SD del futuro juego sigue junto
al NRO y los assets; estas carpetas de desarrollo no se copian a la consola.
