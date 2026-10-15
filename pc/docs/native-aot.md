# Generacion nativa PC: herramientas y limites

El generador de SH3-nx usa el pipeline x86 de xboxrecomp: contenedor XBE,
desensamblado, deteccion de funciones, ABI y generacion de C por lotes.
Su nombre no significa usar la version Xbox 360: aqui se analiza el x86
de la version Windows, y el XBE es solamente un archivo intermedio del build.

Referencias inspeccionadas:
[SH3 regen.sh](https://github.com/cutarev/sh3-nx/blob/fcb753878475018e62906da5792aaadfe46c5ab3/tools/regen.sh),
[SH2 xboxrecomp](https://github.com/cutarev/sh2-nx/tree/main/third_party/xboxrecomp).
El SDK se suministra como fuente externa y se registra el hash de sus archivos
Python. No se incluyen copias de ese SDK ni del juego en estos commits.

## Entrada de codigo

El EXE Steam examinado tiene una entrada empaquetada en `.bind`. Su `.text`
en disco difiere en 12.392.610 de 12.440.971 bytes de la captura cargada que
usa el laboratorio anterior. Tratar ambos como el mismo codigo es incorrecto.
La captura permite avanzar en la traduccion, pero no demuestra arranque
desde el archivo original ni compatibilidad con otra edicion.

`prepare_aot_image.py` exige el SHA256 del EXE. Si se usa texto capturado,
exige tambien su SHA256, su longitud exacta y una entrada de analisis explicita
dentro de `.text`. Esa entrada no se anuncia como WinMain o entrada real.
El archivo original se lee sin modificarlo. La salida siempre indica
`analysis_only=true` y `cold_boot_supported=false`.

El convertidor conserva direcciones y datos de las secciones y repaqueta sus
offsets en disco. Calcula tablas de nombres separadas: las ocho secciones
de FNV no caben correctamente en un layout fijo disenado para menos secciones.
Registra TLS y delay imports; su presencia no equivale a implementarlos.

## Ejecucion del analisis

Primer analisis completo conservado (captura anterior, 14 de octubre):
4.134.733 instrucciones en el barrido, 85.850 entradas detectadas y 85.927
cuerpos C generados tras las reparaciones de limites del recompiler. Hay
664 destinos sin resolver y 1.988 apariciones de instrucciones pendientes.
Estos numeros son resultados de analisis heuristico, no un porcentaje de
motor correctamente portado.

El primer y el ultimo bloque (427 funciones en total) compilaron a objetos
ARM64 con devkitA64. No se enlazo ni ejecuto el motor; no se compilo todavia
todo el codigo generado. Los objetos, C, disassembly e informes con bytes
del juego se conservan privados. El estado de esta generacion es
`GENERATED_WITH_GAPS`, con entrada real y arranque desde disco sin verificar.

Los archivos generados contienen codigo/datos del juego y deben estar fuera
de los commits. Usar un directorio privado, por ejemplo `local/aot-analysis`.

```sh
python pc/tools/prepare_aot_image.py --exe /ruta/FalloutNV.exe --exe-sha256 HASH --output /ruta/privada/aot
# Para la captura conservada se requieren ademas:
# --text-image /ruta/privada/text.bin --text-sha256 HASH --analysis-entry 0xDIRECCION
python pc/tools/run_aot_analysis.py --analysis /ruta/privada/aot --sdk /ruta/xboxrecomp
```

Se requiere Python con Capstone. `--python-deps` permite indicar su carpeta.
`--stage scan` ejecuta solo parser/desensamblado; `--stage generate` usa ese
scan previamente verificado. Se fijan hashes del contenedor, el SDK y todos
los JSON de desensamblado antes de generar. No se aceptan bases de datos de
otro archivo aunque su nombre coincida.

El wrapper propio `aot_index.py` mantiene un indice incremental de direcciones
para las correcciones de tablas de salto. El SDK invalidaba su lista ordenada
en cada insercion o borrado y la reconstruia completa en la siguiente consulta.
Aqui se conserva el indice inicial, se agregan las direcciones nuevas y se
filtran las eliminadas al consultar. No se modifican los archivos del SDK ni
la logica de deteccion; el hash del wrapper tambien queda fijado en el scan.
Las pruebas comparan inserciones, cambios, borrados, reinserciones y decodes
solapados contra una ordenacion completa independiente. Este cambio acelera
la herramienta de build; no constituye una mejora de FPS en la consola.

El wrapper tambien limita el decode de `decode_at` a las instrucciones que
el SDK puede consumir: Capstone prepara todo su resultado antes de empezar
a entregarlo, por lo que un `break` temprano en Python no limita ese trabajo.
El limite respeta la instruccion existente inicial y los 15 bytes maximos
de una instruccion x86. Esta segunda mejora paso comparaciones contra el SDK
original con entradas sinteticas; el scan completo conservado se hizo con
la primera version del indice y guarda su fuente exacta por hash.

El ABI de funciones internas se infiere sin aplicar clasificaciones XDK
de Xbox a las API Windows. Las API importadas necesitan bridges independientes
con sus firmas y comportamiento correctos. No se usan overrides de retorno
forzado para hacer avanzar el programa.

`generation-report.json` cuenta funciones generadas, fallos, destinos sin
resolver, instrucciones no soportadas y marcadores de codigo incompleto.
Un cuerpo `translation failed`, una llamada a `recomp_unimpl` o un comentario
de instruccion no manejada se consideran pendientes. Incluso sin esos
marcadores, el resultado es `GENERATED_UNVALIDATED`: todavia falta compilar,
comparar semantica y validar callbacks, excepciones, TLS y servicios nativos.
`nro_build_ready` permanece falso; generar C no acredita un motor funcional.

Pruebas del adaptador, sin ejecutar codigo guest:

```sh
python pc/tests/test_aot_image.py
python pc/tests/test_aot_index.py
python pc/tests/test_aot_sdk_overlay.py --sdk /ruta/xboxrecomp
```

En un entorno con carpeta temporal restringida, usar `--temp-dir /ruta/permitida`.

## Auditoria de destinos pendientes

`audit_aot_gaps.py` compara la identidad del contenedor, perfil, scan y stubs
generados antes de leer las referencias. Consulta todos los rangos de funciones
que contienen un destino, incluidos solapamientos; elegir solo la entrada
anterior mas cercana puede dar un propietario incorrecto. No modifica el SDK,
el scan, el codigo generado ni el ejecutable y rechaza sobrescribir su informe.

```sh
python pc/tests/test_aot_gaps.py --python-deps /ruta/dependencias
python pc/tools/audit_aot_gaps.py --analysis /ruta/privada/aot --output /ruta/privada/audit.json --python-deps /ruta/dependencias
```

Resultado del analisis conservado, 15 de octubre: de los 664 destinos, 609
caen dentro de cuerpos detectados, 17 en huecos ejecutables y 38 fuera de la
imagen. Hay 530 con un limite de instruccion observado desde algun propietario,
68 dentro de instrucciones segun ese decode y 11 cuyo decode acotado no
resuelve el limite; los otros 55 no tienen propietario. 139 no tienen xrefs
en el scan original y requieren rastrear su origen en la generacion.

No interpretar estos grupos como funciones verificadas o referencias descartadas.
El decode lineal puede atravesar datos y los rangos son heuristicos; para
aceptar una entrada hacen falta flujo real y ABI coherente. Las 664 referencias
siguen pendientes, sin agregar aliases ni retornos de exito. El informe privado
contiene bytes, instrucciones, propietarios y llamadores; no subirlo al repo.

La prueba incluye 2004 consultas comparadas con un oraculo de intervalos,
propietarios solapados, direccion interior a una instruccion x86, perfiles
anteriores y actuales, rechazo de entradas alteradas y ausencia de mutaciones.

`--trace-generated` agrega procedencia de simbolos del C, verificando primero
el hash de cada chunk. Ignora comentarios y literales y conserva archivo,
linea, funcion y etiqueta de bloque; esta ultima no se anuncia como direccion
exacta de la instruccion guest. En el baseline se verificaron los 344 chunks:
los 664 destinos tienen usos reales en C (1109 usos). Los 139 sin xrefs del
scan se dividen en 102 destinos de transferencias condicionales, 10 de colas
y 27 de llamadas ABI; algunos tienen mas de un uso. Eso no prueba por si solo
que las instrucciones que originaron esos usos sean codigo ejecutable real.

## Extension SIMD de bits y copias

`aot_simd_bits.py` agrega explicitamente trece instrucciones SSE2 legacy:
MOVLPD, MOVDQU, ANDPD, ANDNPD, ORPD, PSHUFD, SHUFPD, UNPCKLPD, UNPCKHPD,
PSLLD, PSRLD, PSLLQ y PSRLQ. El overlay actua solo sobre formas x86-32 validas
que reconoce; otras instrucciones/formas conservan la ruta del SDK. No se
modifican fuentes del SDK. Los helpers estan en `native/fnv_simd_bits.h`.

Son operaciones sobre bits: conservan payloads NaN y no cambian EFLAGS ni
MXCSR. MOVLPD conserva la mitad alta al cargar y escribe solo ocho bytes al
guardar. MOVLPD/MOVDQU usan accesos de byte para evitar el comportamiento
indefinido de punteros C desalineados del SDK. Los desplazamientos usan el
contador completo de 64 bits de la forma variable; no lo reducen modulo32/64.
Contadores mayores o iguales al ancho producen cero sin un shift C indefinido.

Las formas legacy m128 que exigen alineamiento fallan con diagnostico y abort
si no lo cumplen. Todavia no entregan #GP/SEH al guest. MOVDQA, operaciones FP,
SET O/productores de OF y control MXCSR siguen fuera de este cambio. Tampoco
se afirma fidelidad completa de page faults, atomicidad o memoria no mapeada.
Referencia de semantica: [Intel SDM, volumen 2](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

La prueba compila el C emitido a ARM64 con devkitA64 GCC16.1.0 y compara su
ejecucion en Unicorn ARM64 contra las instrucciones originales en Unicorn
x86. Pasaron 2384 formas y 11920 casos con GPR/XMM/memoria/EFLAGS/MXCSR,
todos los inmediatos de shuffle/shift, alias de registros, cuentas grandes,
payloads de bits y las copias desalineadas permitidas. Otros 138 casos
verificaron el fail-stop de alineamiento sin escrituras de resultado.
Esto es validacion diferencial emulada, no una prueba del juego en consola.

```sh
python pc/tests/test_aot_simd_bits.py --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --cc /ruta/aarch64-none-elf-gcc.exe --output /ruta/privada/prueba-simd
python pc/tools/run_aot_analysis.py --analysis /ruta/privada/aot --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --stage generate --generation-name simd-bits01 --simd-bits
python pc/tools/verify_aot_simd.py --analysis /ruta/privada/aot --generation-name simd-bits01 --sdk /ruta/xboxrecomp --cc /ruta/aarch64-none-elf-gcc.exe --output /ruta/privada/objetos-simd --jobs 2
```

Una generacion con nombre separa C, ABI, clasificaciones, logs e informe; no
reescribe el baseline. Los scans nuevos conservan su wrapper exacto. Al
reutilizar un scan antiguo cuyo wrapper difiere del actual, el runner exige
que `aot-index-used.py` coincida con el hash original; solo lo lee como prueba
de identidad, nunca lo ejecuta. No se vuelve a etiquetar un scan v1 como v2.

El verificador exige la misma entrada/SDK/seleccion completa, conserva el
dispatch publico y compara firmas de las funciones de los chunks cambiados.
Comprueba cada hash generado y compila todos esos chunks a objetos ARM64;
no enlaza el motor. Para compilarlos se necesita incluir `pc/native` junto
al runtime del SDK. El informe diferencia la cobertura de instrucciones,
los destinos aun pendientes y la ausencia de arranque desde el archivo.

Resultado del lote completo `simd-bits01` (15 de octubre): 85.927 cuerpos C,
cero fallos de generacion; las 419 apariciones de las trece instrucciones
quedaron traducidas y el resto de pendientes paso de 1.988 a 1.569. Los 664
destinos siguen sin resolver. Compilaron los ocho chunks modificados a ARM64
con los flags indicados; 339 archivos generados conservaron sus hashes y el
dispatch permanecio identico. La diferencia adicional es el include del
header SIMD en las declaraciones compartidas. No se enlazo ni ejecuto el motor.
