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
| `../native/d3d9_constants.c/.h` | Bancos VS/PS float4, int4 y BOOL, con agrupacion de cambios, mutex y reintento de subidas; independiente del backend y consumido por el modulo COM. |
| `shader_packages.py` | Inventario acotado de SDP y exportacion privada opcional por hash; no traduce ni valida semantica de shaders. |
| `build_d3d9_constants.py` | Agrega el banco de constantes al conjunto CPU/Windows/VM verificado, con flags Switch y auditoria de externos. No construye renderer ni NRO. |
| `../native/d3d9_com.c/.h` | Bridges COM stdcall con vtables guest32, shaders de catalogo y proveedor real obligatorio; referencias, shaders/constantes, vertex/index buffers, declaraciones, texturas 2D, bindings y estados de render/sampler/viewport/scissor. Targets/Clear y escenas/draws conectados; proveedor Switch pendiente. |
| `build_switch_diagnostic.py` + `../switch/diagnostic.c` | Enlaza el conjunto ARM64 privado aceptado como NRO de diagnostico: lectura PE, inventario IAT y pruebas de runtime/TEB/permisos Horizon. No ejecuta el motor. |
| `build_d3d9_com.py` | Sustituye CPU conservando su version anterior verificada, comprueba dependencias de includes y reutiliza los C generados inmutables; compila los modulos propios y audita TLS/externos. |
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
`test_shader_packages.py` comprueba entradas malformadas; `test_d3d9_constants.py`
compara los bancos con D3D9 NULLREF de Windows, procesa setters conservados y
comprueba CreateShader/GetFunction del catalogo. Ver [contrato D3D9](../docs/native-d3d9-constants.md).
`test_d3d9_com.py` traduce callers de vtable y verifica bridges/referencias,
shaders y constantes con D3D9 NULLREF. Ver [ABI COM y comandos](../docs/native-d3d9-com.md).
`test_d3d9_geometry.py` verifica declaraciones, bindings, buffers y bytes capturados privados,
con reciclado de memoria y fallos inyectados. Ver [buffers y limites](../docs/native-d3d9-buffers.md).
`test_d3d9_textures.py` compara mips, formatos, rectangulos y uploads contra
D3D9 NULLREF. Ver [texturas y limites](../docs/native-d3d9-textures.md).
`test_d3d9_states.py` comprueba defaults, bits de estado, viewport/scissor y
fallos sin modificar salidas. Ver [contrato de estados](../docs/native-d3d9-states.md).
`test_d3d9_targets.py` usa un D3D9 HAL propio para comprobar superficies,
backbuffer implicito, viewport y pixeles de Clear. Ver [targets y limites](../docs/native-d3d9-targets.md).
`test_d3d9_draw.py` verifica un pipeline propio texturizado con indices,
constantes, depth y pixels en D3D9 HAL. Ver [draws y limites](../docs/native-d3d9-draw.md).

`build_shader_linker.py` construye nuestro adaptador offline contra una copia
externa sin cambios de MojoShader; `link_d3d9_spirv.c` enlaza VS/PS y retira
su tabla auxiliar antes de entregar SPIR-V. `inspect_spirv.py` comprueba framing
y refleja bindings; no sustituye spirv-val. `audit_spirv_catalog.py` verifica
hashes y separa aceptacion de traduccion de cobertura dibujada.
`test_d3d9_spirv.py` y `test_vulkan_pipeline.py` comprueban shaders propios,
rechazos y 4.096 pixeles Vulkan en desktop. Ver [dependencias, comandos y
limites](../docs/native-vulkan-shaders.md). No se copian herramientas, DLLs
del PC ni carpetas de desarrollo a la SD.

`translate_d3d9_pair.py` conserva las declaraciones centroid en un adaptador
propio y las restaura como decoraciones del input SPIR-V despues del enlace.
`test_spirv_centroid.py` cubre uno/dos varyings, rechazo y un fixture GPU propio.
La auditoria `--centroid` acepta 1.808/1.809 copias temporales; ese recuento
no representa pares enlazados ni shaders del juego dibujados. Ver los limites
MSAA y comandos en el documento Vulkan anterior. No modifica MojoShader.

`spirv_uniform_layout.py` obtiene spans de bancos globales desde los metadatos
publicos y offsets/tipos/indices reales del SPIR-V. `test_uniform_pack.py`
comprueba el modulo C portable y genera datos propios consumidos por Vulkan,
incluidos INT/BOOL y registros dispersos. `inspect_shader_ctab.py` permite
inspeccionar rangos declarados sin repararlos. Ver [contrato, pruebas y
comandos](../docs/native-vulkan-uniforms.md). Las salidas son privadas y no se
copian a la SD como carpetas de runtime.

`test_uniform_state.py` valida bancos persistentes, snapshots y lotes con hilos.
`test_d3d9_uniform_state.py` traduce callers COM propios y consume sus bloques
en Vulkan, liberando el lote tras un fence real. Genera solo fixtures privados;
ver [contrato y alcance](../docs/native-vulkan-uniform-state.md). Los dos forks
de Mesa indicados por el usuario se revisan en [refs y procedencia](../docs/mesa-switch-review.md).

`test_geometry_access.py` compara el validador C de rangos con un modelo Python:
indices 16/32, base con signo, seis topologias, streams y tipos de declaracion.
`test_d3d9_uniform_state.py` tambien conecta geometria COM con draws Vulkan,
conservando copias tras modificar buffers. Ver [alcance y limites](../docs/native-vulkan-geometry.md).

`test_texture_upload.py` compara parches de cinco formatos sin compresion con
un modelo Python; verifica RGBA, alpha, pitch, padding y errores sin escrituras.
`test_d3d9_uniform_state.py` conecta tambien uploads y bindings de texturas con
Vulkan. Ver [contrato y limites](../docs/native-vulkan-texture-upload.md).

`test_vk_state.py` verifica el mapper C de profundidad y samplers a estructuras
Vulkan. `test_d3d9_uniform_state.py` comprueba su conexion a estados COM con UV
fuera de rango y un control negativo de clamp. Ver [subset y cobertura GPU](../docs/native-vulkan-state.md).

`test_d3d9_vk_backend.py` ejecuta callers x86 de targets/Clear contra el proveedor
Vulkan propio. Comprueba pixeles, dos frames en vuelo, vida de recursos, limites
y fallos con `vk_backend_fault_proxy.h`. No usa el dispositivo NULLREF anterior.
Ver [propiedad, sincronizacion y limites](../docs/native-vulkan-backend.md).

El [backend Vulkan propio](../docs/native-vulkan-backend.md) integra ahora uploads
versionados de texturas 2D por COM. `../tests/test_d3d9_vk_backend.py` verifica
mipmaps, cinco conversiones sin compresion, bytes por transfer GPU, fallos y
retencion por fences. Ademas verifica 26 draws texturizados propios con UBOs,
profundidad/scissor/culling/blend y sus pixels; no ejecuta una partida.

El mismo runner requiere ahora `--linked-shaders` con los SPIR-V propios enlazados
normal.vs.spv/normal.ps.spv del fixture de constantes. Verifica modulos shader
Vulkan, declaraciones, UBOs y draws desde llamadas COM traducidas. Comprueba
fallos, vida util y controles negativos de sampler, culling y blend. El catalogo offline del
juego y su arranque siguen pendientes.

El runner Vulkan ahora genera un PE con Direct3DCreate9 por nombre y un caller
CALL [IAT]. Valida el [bridge de fabrica](../docs/native-d3d9-factory.md), entrega
el device preparado a traves de CreateDevice y conserva los tests GPU. El runner
COM previo mantiene sus regresiones sin habilitar la fabrica. Caps completas y
presentacion no se simulan como terminadas.
# Diagnostico grafico Switch

`build_switch_renderer_probe.py` compila el NRO propio DX9/COM/Vulkan para probar
NVK y NWindow en hardware. Incluye solamente callers sinteticos y shaders propios;
no usa el AOT ni los archivos del juego. Ver
[dependencias, comprobaciones e instrucciones](../docs/switch-renderer-probe.md).


### Verificar transferencias de textura diferidas

`python pc/tests/test_vulkan_lazy_texture.py --headers <vulkan-headers> --output <nueva-carpeta> --cc <compilador>`

Compila dos tests del proveedor real, sin SDK de recompilacion ni datos del
juego. Windows carga Vulkan del sistema; Linux enlaza Vulkan/pthread. Verifica
siete mips BC1, parches, A8, cero submits por Unlock, cola llena al leer,
peticiones invalidas y teardown. Guarda hashes de fuentes, ejecutables y logs.
No es una prueba de FPS ni de hardware Switch.

### Probe Switch conjunto hw02

`build_switch_renderer_probe.py` usa la prueba aceptada de
`pc/tests/test_renderer_probe_checks.py`, con 93 callers y checks compartidos
en `pc/switch/renderer_probe_checks.h`. El builder rechaza fuentes, shaders o
callers distintos de los verificados. [Uso y cobertura](../docs/switch-renderer-probe.md).
## Heap y dependencias PC

`../tests/test_windows_heap.py` verifica imports x86 del heap de proceso contra
Windows, agotamiento, realloc y concurrencia. Ver [contrato y limites](../docs/native-windows-heap.md).
`../tests/test_windows_private_heaps.py` agrega HeapCreate/Destroy, crecimiento
con permisos reales, aislamiento y rollback de fallos sobre el pool Virtual*.
`../tests/test_windows_heap_scale.py` prueba 100k/200k allocs y equivalencia first-fit
con fragmentacion; los tiempos son de escritorio, no FPS de Switch.
`../tests/test_windows_sync.py` verifica secciones criticas e Interlocked con
contencion, medicion del hilo bloqueado y cancelacion por fallo de otro hilo.
`../tests/test_windows_events.py` compara eventos sin nombre con Windows y prueba
Set/Reset bajo contencion, deadlines, handles de generacion y peer CPU faults.
`audit_heap_flags.py --exe /ruta/FalloutNV.exe --analysis /privado/analisis
--python-deps /ruta/deps --output /privado/heap-flags.json` conserva evidencia
estatica de flags inmediatos/acotados en llamadas IAT; no prueba valores runtime.
`audit_pc_dependencies.py --game /ruta/juego --output /privado/dependencies.json`
audita los imports normales del EXE y DLL locales, sin ejecutarlos ni copiarlos.
El reporte local contiene metadatos; la ausencia en este grafo no descarta carga
dinamica. No se publica el reporte ni los binarios del usuario.

# Auditoria del arranque

`audit_startup.py --exe EXE [--analysis DIR] --output NUEVO.json` lee el PE y,
si se indica, el perfil/contenedor privado. Verifica identidad, compara entradas
y texto, y explica los bloqueos. Nunca ejecuta ni parchea el EXE, no infiere
ausencia de DRM ni arranque por flags del perfil, y no sobrescribe informes.
`test_startup_audit.py` cubre PE sintetico, captura, `.bind` y corrupcion de inputs.


## Llamadas de una raiz de analisis

`audit_aot_calls.py --exe EXE --analysis DIR --python-deps DIR --entry 0xVA
--output NUEVO.json` valida identidades, recorre ramas directas y enumera calls
por destino o nombre IAT. Admite varias --entry y un limite por raiz. No sigue
calls, tablas de salto ni excepciones. Una raiz explicita es una hipotesis,
no una entrada original o frontera de funcion verificada. Nunca ejecuta codigo.
`test_aot_calls.py --python-deps DIR` usa un PE sintetico para direct calls,
imports por ordinal, thunks, limites e inputs invalidos.

## Control de bytes de fuentes

check_line_endings.py usa los paths versionados por Git (o --manifest JSON para
un overlay) y comprueba LF en pc/**, README.md y .gitattributes. --fix normaliza
CRLF de forma explicita; valida primero todos los inputs y rechaza rutas fuera
del repo, binarios y CR sueltos. Excluye third_party/local/dist. Ejecutarlo antes
de congelar hashes, no despues. test_line_endings.py comprueba esos contratos.

fetch_minimp3.py descarga/verifica headers CC0 externos fijados para el decoder
MP3 nativo; no toca Steam ni genera carpetas SD. test_mp3_stream.py verifica
conversion de frecuencia y streaming/rewind/cola sobre pistas privadas;
ver [contrato MP3](../docs/native-mp3.md). No es audio del motor funcionando.

build_horizon_media.py verifica la compilacion y enlace parcial de controles
HID/XInput y PCM/audout. Requiere validation.json del test_windows_xinput.py;
ver [contrato y limites](../docs/native-input-audio.md). No ejecuta el motor.

El auditor audit_seh_helpers.py y --seh-helpers conectan variantes MSVC con
cookie verificadas al traductor. Requiere --frame-restore y generacion separada;
ver [contrato y pruebas](../docs/native-seh-helpers.md). No implementa excepciones.

La capa aot_frame_restore.py agrega publicacion de EBP tras POP EBP/LEAVE y LEA
EBP. Se habilita con --frame-restore en una generacion nueva; no modifica el SDK.
Ver limites SEH en native-aot.md. test_windows_seh_frames.py permite --leave,
--lea y el control negativo --without-overlay (debe fallar con el SDK actual).

El test POSIX test_host_directory_posix.py se puede compilar y ejecutar en Linux,
o compilar en Windows con --compile-only --target x86_64-linux-musl --cc zig cc
y ejecutar en WSL con --run-only sobre el mismo output. Se verifican las mismas
fuentes y binario en ambas fases, sin instalar otro compilador en el sistema.
