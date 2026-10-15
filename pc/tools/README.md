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
| `aot_mul_flags.py` + `../native/fnv_mul_flags.h` | Productos de 8/16/32 bits, con signo y sin signo, y captura de CF/OF; integracion conservadora con el seguimiento de flags del SDK. |
| `aot_code_data.py` | Audita switches MSVC de dos niveles con limite explicito y crea una seleccion derivada separada; solo aparta seeds `gap_prologue` sin referencias contradictorias confinados a tablas/padding. |
| `../native/pe_image.c/.h` | Carga de datos PE32/i386 y enlace IAT mediante un resolver del llamador. No ejecuta x86 ni implementa Windows. |
| `../native/cpu_runtime.c/.h` + `fnv_sdk_runtime.h` | Estado CPU y traza por hilo, resolucion de imports PC por tokens y parada con diagnostico; requiere el SDK x86 externo y funciones guest ya traducidas. |
| `../native/windows_thread.c/.h` | Ocho imports Kernel32, TEB32, TLS dinamico y LastError en memoria guest; no implementa el directorio TLS PE ni CreateThread. |
| `../native/guest_memory.c/.h` + `host_mutex.h` | Pool guest32 de datos con reserve/commit/free/query/protect, presupuesto explicito y mutex bloqueante; conserva la representacion plana del SDK. |
| `../native/memory_horizon.c/.h` | Permisos reales por rangos y rollback; adaptador libnx, backing fisico eager, sin devolver RAM al decommit. |
| `../native/windows_memory.c/.h` | Cuatro bridges Virtual* con MBI32, LastError y parada estricta si el backend queda incoherente; no implementa heap ni SEH. |
| `prepare_aot_image.py` | Valida identidad y empaqueta secciones PE en el contenedor x86 que espera el SDK; permite una captura de texto declarada. |
| `run_aot_analysis.py` | Orquesta scan/ABI/codegen con hashes y variantes separadas; no aplica firmas XDK al programa Windows. |
| `audit_aot_gaps.py` | Clasifica destinos pendientes, todos sus propietarios solapados y procedencia en C generado. Solo lectura. |
| `verify_aot_simd.py` | Compara baseline/variante SIMD o multiplicacion, comprueba dispatch y compila a ARM64 todos los chunks cambiados. No enlaza el juego. |
| `verify_aot_code_data.py` | Reproduce el filtro desde el analisis padre, comprueba los cuerpos retenidos y compila todos los C. Puede enlazar los objetos con `--ld` e inventariar dependencias con `--nm`. |
| `build_cpu_runtime.py` | Recompila todo el C con TLS de Switch y el adaptador de traza, enlaza runtime/PE e inventaria externos permitidos. Genera objetos privados, no NRO. |
| `build_windows_thread.py` | Verifica y reutiliza el lote CPU completo sin cambios; agrega TLS/VM y, con `--memory-exports`, Virtual* y backend Horizon. Comprueba definiciones externas en bibliotecas identificadas. |
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
3. Llevar juntos cada overlay de instrucciones y su header; incluir `pc/native` y el runtime
   del SDK al compilar los C generados. La API de memoria/registros debe ser
   la misma o necesita un adaptador verificado.
4. Adaptar las convenciones del pipeline: hoy los nombres de contenedor e
   informes, el banner del generador y la comprobacion de Steam son de FNV.
   No se presenta como un paquete universal listo para cualquier juego.
5. Compilar y comparar semantica antes de integrar. Las pruebas ARM64 actuales
   usan devkitA64 GCC16.1.0 con `-mearly-ra=none`; otro compilador/version exige
   validacion propia. Una generacion exitosa no demuestra arranque ni FPS.

Los helpers SIMD no implementan FP/MXCSR. El overlay separado de multiplicacion
aporta OF/CF de MUL/IMUL y permite SETO/SETNO solo con un productor conocido;
no es un registro EFLAGS completo compartido entre funciones. Los
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
python pc/tests/test_aot_mul_flags.py --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --cc /ruta/aarch64-none-elf-gcc.exe --output /ruta/privada/prueba-mul
python pc/tests/test_aot_mul_cfg.py --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --cc /ruta/aarch64-none-elf-gcc.exe --output /ruta/privada/prueba-mul-cfg
python pc/tests/test_aot_code_data.py --python-deps /ruta/dependencias --temp-dir /ruta/privada/tmp
```

Las salidas SIMD y de multiplicacion exigen un directorio nuevo y guardan hashes, flags del
compilador y limites de lo probado. Para el pipeline y sus comandos completos,
ver `../docs/native-aot.md`. La configuracion SD del futuro juego sigue junto
al NRO y los assets; estas carpetas de desarrollo no se copian a la consola.

Para estado por hilo, imports, fallos y las opciones TLS obligatorias de
Switch, ver [el contrato del runtime CPU](../docs/native-cpu-runtime.md).
`test_cpu_runtime.py` prueba concurrencia e imports con codigo sintetico
traducido por el SDK; las instrucciones anteriores conservan su cobertura.
`test_windows_thread.py` agrega TLS/TEB, conflictos y comparaciones con las
API del host Windows. Ver [contrato y comandos](../docs/native-windows-thread.md).
`test_guest_memory.py` compara reservas/paginas con Windows e inyecta fallos
del backend; `test_windows_memory.py` ejecuta los bridges y accesos MEM
traducidos. Ver [memoria y limites](../docs/native-windows-memory.md).
