# FNV-nx: recompilacion estatica PC

Esta rama trabaja en un port nativo de la version PC. Xbox permanece pausado.
El usuario aporta su EXE y datos; el NRO contendra el codigo ARM64 generado
previamente y los servicios nativos de Windows que usa el juego. Es el modelo
de [SH2-nx](https://github.com/cutarev/sh2-nx) y
[SH3-nx](https://github.com/cutarev/sh3-nx), cuyos generadores procesan una
version concreta del EXE antes de construir el NRO.

Leer el EXE en la consola aporta secciones, datos y direcciones del programa.
No traduce todo el motor en ese momento. Cada version soportada necesitara
un perfil de identidad que coincida con el codigo ARM64 compilado. Los bridges
D3D8 de Silent Hill no sustituyen directamente el D3D9 de New Vegas.
La ruta grafica elegida es Vulkan sobre Mesa/NVK; deko3d queda pausado.
La [prueba Vulkan propia](docs/native-vulkan-shaders.md) verifica conversion
SPIR-V, textura y profundidad en la GPU del PC. Su conexion completa al proveedor
COM y al backend Switch sigue pendiente.
El adaptador centroid propio conserva la declaracion en SPIR-V, comprobada
con shaders propios y dibujo desktop a 1x. Los 1.808/1.809 shaders aceptados
por el traductor con copias temporales todavia no son cobertura grafica del juego.
Los [planes UBO y el empaquetador nativo](docs/native-vulkan-uniforms.md) ya
conservan float/INT/BOOL y pasan dibujos propios con registros dispersos;
su gestion desde callbacks COM se prueba en el fixture siguiente.

La [prueba de bancos/versiones COM](docs/native-vulkan-uniform-state.md) ya
ejecuta esos callbacks desde callers x86 traducidos, con pixeles y fence Vulkan
reales en desktop. La [conexion de geometria](docs/native-vulkan-geometry.md)
tambien lleva uploads y bindings VB/IB desde COM a Vulkan y valida sus accesos.
La [conexion de texturas](docs/native-vulkan-texture-upload.md) agrega uploads
ARGB8 y snapshots por draw, con parches/reintentos y conversion C para cinco
formatos sin compresion. La [conexion de estados](docs/native-vulkan-state.md)
lleva profundidad y sampler desde COM a estructuras Vulkan, verificando UV
fuera de 0..1. Seleccion de variantes, otros estados y targets siguen explicitos;
no es todavia el proveedor completo de Switch. Los forks del driver se
comparan con refs fijados en [revision Mesa](docs/mesa-switch-review.md).
El [proveedor Vulkan nativo](docs/native-vulkan-backend.md) ya atiende superficies,
targets, viewport/scissor y Clear con objetos propios y fences, sin NULLREF.
Su fabrica guest, recursos de draws y presentacion siguen pendientes.

## Estado

Existe una biblioteca C independiente en `native/pe_image.c` que:

* Carga encabezados y secciones PE32 en memoria ordinaria suministrada por el
  caller, con inicializacion a cero de las zonas BSS.
* Representa las direcciones x86 como offsets, sin exigir que el proceso
  Switch pueda mapear memoria host en la direccion Windows `0x00400000`.
* Recoge imports por nombre y ordinal y sus slots IAT.
* Enlaza todos los slots mediante un resolver nativo; si falta un servicio,
  deja la IAT completa intacta y devuelve un error.
* Comprueba rangos, huecos, capacidad de memoria, tablas truncadas, secciones
  solapadas, nombres invalidos y slots duplicados.

La prueba host compara el mapeo completo y los imports con un parser
independiente. Paso con fixtures y con el EXE Steam examinado: 280 slots,
17 DLL. La biblioteca tambien compilo como objeto ARM64 con devkitA64.
Los tokens del test son sinteticos; no implementan funciones Windows.
El primer NRO de diagnostico ya verifico este cargador en Switch: dos
ejecuciones con cero fallos, carga PE de 203/207 ms y salida normal.
No se ejecuto codigo del juego.

El laboratorio nativo previo conserva 412 constructores CRT observados y
una frontera en `0x44F770`, antes del motor. Usa codigo capturado de una
sesion PC y no es una recompilacion completa ni un arranque desde el EXE
original. No hay un perfil AOT listo para arrancar el motor, un launcher nativo del juego
ni un NRO que ejecute el motor. Existe un primer
[NRO de diagnostico](docs/switch-diagnostic.md) que inspecciona el EXE y
comprueba el runtime, TEB y permisos de memoria en la consola.

El analisis AOT amplio conserva 85.885 cuerpos C generados a partir de esa
captura. La extension SIMD resolvio 419 apariciones de instrucciones y el
lote MUL/IMUL y CF/OF otras 716. Una auditoria posterior reconocio 132 tablas
de switches acotados y aparto 42 entradas creadas por la heuristica sobre
datos. Sus 56 instrucciones y 16 destinos falsos se cuentan como datos
retirados, no instrucciones implementadas: quedan 797 y 648 destinos por
revisar. Los cuerpos C retenidos son identicos a los del lote anterior.

Pasaron 176.332 comparaciones diferenciales x86/ARM64 de multiplicacion y
secuencias de flags, 2.400 casos entre bloques y la regresion SIMD de 11.920
casos. Las pruebas de tablas protegen entradas reales y rechazan selecciones
manipuladas. Tras corregir la seleccion compilaron las 346 unidades C del
conjunto completo, incluido el bloque que emitia `cr2`, y paso el enlace
relocatable sin duplicados. Ese primer enlace ARM64 inventario 60 simbolos
externos de estado, libc/math y runtime; no incluye las API Windows de la IAT.
Estos contadores describen el analisis de la captura; no acreditan un motor
enlazado ni arranque desde disco. Ver
[analisis y validacion](docs/native-aot.md) e
[inventario de herramientas reutilizables](tools/README.md).

La siguiente integracion agrega un runtime con registros y traza por hilo,
resolucion IAT por tokens de 32 bits y parada con diagnostico ante codigo
pendiente. Pasaron 1.000 llamadas concurrentes entre dos hilos y doce casos
de fallo sobre rutinas sinteticas traducidas por el SDK. El runtime CPU
base requiere las API Windows en modulos adicionales. El
[contrato del runtime CPU](docs/native-cpu-runtime.md) documenta sus limites,
el wrapper obligatorio y el ABI TLS especifico de Switch, que requiere
recompilar todos los objetos en vez de reutilizar el lote ARM64 generico.
Con esa configuracion compilaron 348 unidades y paso el enlace parcial con
el runtime: ya no quedan simbolos propios de CPU sin resolver; los 30
externos restantes pertenecen a las bibliotecas de plataforma. Esto sigue
sin enlazar un ejecutable del motor.

El primer [modulo Windows por hilo](docs/native-windows-thread.md) implementa
ocho imports Kernel32: TLS, LastError e IDs guest, con campos TEB coherentes
con los accesos FS traducidos. Pasaron los 1.088 slots, 2.000 rondas entre
dos hilos, once comparaciones con Windows y los casos de memoria/conflicto.
Su integracion inicial sumaba 349 unidades. No implementa CreateThread,
heap guest, excepciones o arranque original.

El [gestor de memoria guest](docs/native-windows-memory.md) agrega reservas,
commit/decommit, query y proteccion real NOACCESS/R/RW, con cuatro bridges
Virtual*. Pasaron 1.600 operaciones aleatorias y 25.609 consultas contra
Windows, 500 ciclos concurrentes, fallos inyectados y accesos MEM traducidos.
El backend Horizon usa libnx y conserva backing fisico preasignado: decommit
no devuelve RAM al kernel. El enlace ARM64 suma 352 unidades; sus 34 externos
de plataforma tienen definiciones comprobadas en bibliotecas identificadas.
No es un enlace final ni prueba de consola. Se cubren once slots normales
de la IAT examinada; los otros 269, el heap y la carga dinamica siguen pendientes.

La version Steam examinada coincide con SteamStub 2.1 y depende tambien de
Steamworks. Su entrada original esta en `.bind`. El archivo permanece
intacto y no se sustituye su autenticacion. Consultar
[el estudio de la dependencia](docs/steam-dependency.md). No se presupone
compatibilidad con otra distribucion hasta auditar su archivo real.

El primer [componente D3D9](docs/native-d3d9-constants.md) ya conserva los
bancos VS/PS de constantes y agrupa cambios para el backend. Paso el oraculo
Windows, reintentos y concurrencia; los 1.809 shaders distintos tambien
pasaron creacion/GetFunction en NULLREF. El enlace parcial suma ahora 353
unidades en esa etapa. Ahora el [modulo COM](docs/native-d3d9-com.md) conecta
desde x86 traducido los metodos de constantes y creacion/consulta de shaders,
con referencias y proveedor real requerido. Pasaron 1.809 recorridos de
shaders, 240 de constantes y 400 entre hilos mediante vtables guest32.
La extension de [bindings y buffers](docs/native-d3d9-buffers.md) conecta
Set/GetShader, vertex/index buffers, streams, declaraciones y Lock/Unlock con
un arena reciclable. Pasaron 40 callers traducidos, 263 creaciones de buffers y la
comparacion de bytes de geometria capturada con buffers D3D9 nativos.
La extension verifica 17 tipos de atributo y 529 creaciones de declaraciones
contra Windows. La capa de [texturas 2D](docs/native-d3d9-textures.md) agrega
LockRect/UnlockRect, mips, formatos DXT y Set/GetTexture. Diez formatos y
cuarenta mips pasan comparacion de bytes contra Windows, con 139 creaciones
y 51 subidas. El enlace parcial actualizado suma 354 unidades. La presentacion y el
proveedor grafico Switch siguen pendientes. Los [bridges de estados](docs/native-d3d9-states.md)
ya transportan render/sampler y viewport/scissor con defaults del proveedor;
461 comprobaciones escalares y 128 espaciales pasan contra Windows.
La extension de [superficies y targets](docs/native-d3d9-targets.md) ya conecta
color/depth y Clear. Dos Clear desde callers traducidos pasan comparacion de
2.048 pixeles en un dispositivo HAL de Windows. La siguiente extension
de [escenas/draws](docs/native-d3d9-draw.md) ya verifica cuatro dibujos con
shaders propios, texturas y profundidad: 4.096 pixeles exactos en Windows HAL.
No acredita un frame del juego ni renderizado PC en Switch.

## Carpeta de instalacion objetivo

Este es el contrato del futuro port, no un paquete ya disponible:

```text
sd:/switch/FNV-nx/fnv-nx.nro
sd:/switch/FNV-nx/FalloutNV.exe
sd:/switch/FNV-nx/Data/...
sd:/switch/FNV-nx/fnv-pc.ini     # configuracion nativa futura, opcional
```

EXE y datos junto al NRO, sin `pc/` ni `switch/wine/`. El cargador actual
no crea ni copia carpetas. El INI `pc/fnv-pc.ini` conservado en este repo
pertenece al experimento Wine y no configura la biblioteca nativa.
Las rutas de guardados y cache nativos se definiran al integrar esos servicios.

## Proxima integracion

La generacion amplia para analisis usa el pipeline x86 de SH3:
[herramientas AOT, identidad de entradas y control de fallos](docs/native-aot.md).
La captura y el EXE Steam empaquetado son entradas diferentes; los resultados
del analisis no se presentan como un arranque desde disco.

Primero se necesita un perfil verificable del ejecutable y una generacion
AOT que cubra el arranque original, callbacks y entradas indirectas. Cada
import requiere su ABI x86, retorno, efectos sobre memoria y comportamiento
de error correctos, no una funcion vacia que diga que tuvo exito. La carga
dinamica de bibliotecas, TLS, excepciones y las tablas virtuales necesitan
auditorias adicionales.

Los servicios abarcan Windows/CRT, D3D9/D3DX, input, sonido y video. Se
pueden reutilizar los conocimientos y componentes host ya probados para
Switch; los shaders, las llamadas D3D9 y el motor PC aun requieren adaptacion.
El renderer de Xbox no es un reemplazo directo del renderer PC.

Las decisiones de GPU, sincronizacion, memoria, SD y perfilado se recogen en
[la estrategia para Switch](docs/switch-renderer-strategy.md), contrastada con
los documentos de Peggle 2, SA2 y Futari. Son objetivos de implementacion y
medida; todavia no son optimizaciones integradas en un renderer PC funcional.

La siguiente prueba de consola del motor requiere una entrada ARM64 real
que alcance inicializacion del juego con esos servicios. Una lectura PE
correcta, un menu capturado o un triangulo no cumplen ese criterio.

## Pruebas y herramientas

```sh
python pc/tools/audit_native_imports.py --exe /ruta/FalloutNV.exe --output /ruta/privada/native-imports.json
python pc/tests/test_native_pe.py --cc cc --game /ruta/Fallout-New-Vegas
cmake -S pc/native -B /ruta/privada/native-build
cmake --build /ruta/privada/native-build
```

`--game` es opcional para ejecutar solo fixtures. `--cc` admite un comando
con argumentos, por ejemplo `--cc /ruta/zig cc`. CMake construye una
bibliotecas de carga de datos y gestion de memoria; no genera un NRO ni recompila el motor.
Con `-DFNV_X86_SDK=/ruta/xboxrecomp` construye tambien la biblioteca CPU
opcional; las funciones del juego y sus servicios siguen siendo externos.
`audit_native_imports.py` produce una lista de requisitos, sin afirmar que
los bridges esten implementados ni que el ejecutable carezca de DRM.

Se conservan las herramientas de auditoria PE/BSA y el
[experimento Wine PC01–PC03](docs/wine-experiment.md) por trazabilidad.
`tools/build_runtime.sh` requiere ahora `FNV_BUILD_HISTORICAL_WINE=1` para
reproducir explicitamente ese experimento pausado. No construye el port
nativo. Los informes de ejecutables propios permanecen fuera de los commits.
