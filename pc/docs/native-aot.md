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

`audit_startup.py` comprueba de nuevo EXE, perfil y contenedor por SHA256,
las entradas y el texto mapeado. Ejemplo de auditoria privada, sin ejecutar
ni modificar el juego:

```sh
python pc/tools/audit_startup.py --exe /ruta/FalloutNV.exe --analysis /ruta/privada/aot --output /ruta/privada/startup.json
python pc/tests/test_startup_audit.py
```

El EXE conservado entra en `0x014092EE` (`.bind`); el analisis usa
`0x0044F770`, un tramo de constructores conservado del laboratorio, no WinMain
ni una entrada de arranque verificada. La auditoria devuelve BLOCKED incluso
para un PE sintetico con entrada en `.text`: reconocer secciones o poner flags
de verificacion en JSON no prueba que el runtime pueda arrancar el motor.
Tampoco determina si una edicion carece de DRM. Las diferencias de identidad
fallan explicitamente; el informe no contiene bytes del codigo.

El primer requisito para ejecutar el juego desde sus archivos es un perfil
de entrada real compatible con la edicion concreta y verificado contra ella.
Despues se deben seguir las dependencias efectivas de ese arranque (CRT, SEH,
imports y proveedores). Sumar APIs o compilar fixtures no sustituye ese paso.

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

## Multiplicacion y flags definidos

El overlay `aot_mul_flags.py` amplia MUL e IMUL de 8/16/32 bits, con una,
dos o tres entradas segun el opcode. Captura ambos operandos antes de escribir
los registros, incluido el caso en que la direccion de memoria usa el registro
destino. Los accesos de memoria usan bytes para respetar operandos desalineados.

MUL calcula el producto unsigned de doble ancho y CF/OF indican si la mitad
alta es distinta de cero. IMUL calcula un producto signed de 64 bits sin
overflow C: CF/OF indican si cabe en el rango signed del destino. El resultado
se recorta por conversion unsigned definida; los registros parciales conservan
sus bits superiores. El modo de ocho bits escribe AX y deja EDX intacto.
No se usa una multiplicacion signed C de 32 bits para un producto de 32 bits.

Una variable local captura esos flags antes de que MOV o SETcc cambien los
operandos originales. El generador conserva la procedencia entre bloques
cuando coincide; INC/DEC pueden cambiar OF sin alterar CF. MUL no define
SF/ZF/AF/PF, por lo que no se deducen a partir de su resultado. SETO/SETNO y
JO/JNO consultan la captura solo si el productor conocido lo permite.

Una llamada puede modificar los flags en su callee. Al no existir todavia un
EFLAGS arquitectonico compartido entre funciones, el overlay invalida la
procedencia anterior a CALL. Un SETO que depende de flags de entrada,
de una llamada o de un join no resuelto permanece pendiente. Las otras rutas
de fallback del SDK tambien necesitan auditoria antes de ejecutar el motor;
este cambio no anuncia soporte completo de flags ni de callbacks/API.

La revision SIMD tambien registra las trece instrucciones como conservadoras
de EFLAGS. Varias faltaban en la lista del SDK: su resultado vectorial era
correcto pero el analizador perdia el productor entero anterior. Se verifico
el seguimiento de todas las formas y una secuencia MUL→SIMD→SETO completa.

Pruebas: 157 formas y 176.332 comparaciones x86/ARM64, incluidos 131.072 pares
exhaustivos de ocho bits (MUL/IMUL), limites signed/unsigned, productos de
32 bits que desbordan, alias, inmediatos negativos, memoria desalineada,
sobrescritura de operandos, ADC/SBB e INC/DEC. En 173.412 casos se comparan CF
y OF; los 2.920 casos con ADC/SBB comparan solo CF porque el SDK no suministra
una expresion de OF para esas secuencias. Ademas pasaron 2.400 casos de
seis funciones completas generadas por el SDK, con JO/JNO/JB/JAE, saltos a
otro bloque y un bucle. El test comprueba que un join de productores diferentes
siga marcado como pendiente. La regresion SIMD mantiene sus 11.920 casos.

```sh
python pc/tools/run_aot_analysis.py --analysis /ruta/privada/aot --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --stage generate --generation-name mul-flags02 --simd-bits --mul-flags
python pc/tools/verify_aot_simd.py --analysis /ruta/privada/aot --baseline-name simd-bits01 --generation-name mul-flags02 --mul-flags --simd-flags-preserve --sdk /ruta/xboxrecomp --cc /ruta/aarch64-none-elf-gcc.exe --output /ruta/privada/objetos-mul --jobs 2
```

El verificador diferencia apariciones resueltas de nuevas apariciones de
SETO/SETNO que ahora resultan desconocidas al invalidar CALL; no oculta estas
ultimas en el saldo. Ambos perfiles deben usar el mismo runtime SIMD. El
cambio de procedencia SIMD exige el switch explicito mostrado; la identidad
de las fuentes y headers usados queda registrada y se verifica antes/despues
de generar. Se mantienen privados los C, objetos y reportes con bytes del juego.

Resultado de `mul-flags02`: 85.927 cuerpos C y cero fallos de generacion. Se
resolvieron 716 apariciones de SETO, sin apariciones pendientes nuevas; el
contador del SDK paso de 1.569 a 853. Los 664 destinos siguen pendientes y el
dispatch publico se conserva. Cambiaron 202 chunks C y el include compartido;
145 archivos generados permanecen identicos al lote SIMD anterior.

Se intentaron compilar los 202 chunks modificados a ARM64 con GCC16.1.0:
201 compilaron (263.949.432 bytes de objetos) y uno fallo. El verificador
termino con codigo distinto de cero y `COMPILE_FAILURE`; no se considera un
build completo ni se reetiqueta como PASS por tratarse de un fallo previo.

La compilacion amplia encontro ademas un fallo C preexistente que no figura
en ese contador de instrucciones: el SDK emite una escritura a `cr2`, ausente
en su runtime. Se reprodujo al compilar el mismo chunk anterior. La funcion
detectada por `gap_prologue` comienza con dos bytes de padding y atraviesa una
tabla de saltos: un dispatch con limite comprobado consulta 143 selectores y
35 destinos, todos dentro del cuerpo anterior y en limites de instrucciones.
Esto es evidencia de datos confundidos con codigo, no permiso para eliminar
la funcion sin revisar el flujo. El informe privado conserva las identidades,
el dispatch y las tablas; no se altero el scan ni se invento un registro CR2.

Los contadores de instrucciones no sustituyen la compilacion, el analisis de
alcanzabilidad, la auditoria ABI ni la implementacion de servicios Windows.
Un cuerpo detectado o traducido no demuestra que sea codigo ejecutable real.

La [referencia historica aportada](https://web.itu.edu.tr/kesgin/mul06/intel/index.html)
organiza el x86 hasta 486. Sus tablas de ciclos no son estimaciones para Switch;
la referencia normativa de estas operaciones sigue siendo el
[Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

## Separacion de tablas y codigo

`aot_code_data.py` reconoce una forma acotada del switch MSVC de dos niveles:
CMP con limite unsigned, JA al caso por defecto, carga del indice si esta en
una variable de pila, MOVZX desde una tabla de bytes y JMP desde una tabla de
punteros. Exige instrucciones consecutivas, una ruta directa desde el
prologo al CMP y ninguna referencia conocida que entre despues del CMP para
saltarse la comprobacion. Los destinos deben ser limites de instrucciones
dentro del cuerpo anterior; las tablas deben ser adyacentes y estar despues
del RET, con un maximo de quince bytes de padding.

Solo aparta entradas `gap_prologue` sin llamadas ni referencias entrantes que
queden confinadas a esas tablas y al padding permitido. Una entrada fuerte,
una rama exterior hacia los datos, un destino mal alineado con el decode o
un patron ambiguo impiden aplicar el filtro. No es un detector universal de
datos ni una prueba completa de alcanzabilidad por llamadas indirectas.

La herramienta conserva el analisis original y crea otro directorio con la
misma imagen y perfil. Cambian solo la seleccion de funciones y sus
estadisticas; xrefs, labels y bytes del ejecutable se conservan. Registra el
hash del padre, la fuente de la herramienta y las tablas reconocidas. Antes
y despues de generar, el runner vuelve al padre y reproduce la decision:
no confia simplemente en una lista de direcciones excluidas.

`verify_aot_code_data.py` exige que el unico cambio de cuerpos C sea la
ausencia de esas entradas: los cuerpos retenidos deben conservar sus hashes
y no referenciarlas. Comprueba tambien el dispatch y que las instrucciones
pendientes retiradas pertenezcan a los rangos apartados. Su compilacion
abarca todos los C generados, incluidos dispatch y destinos pendientes, sin
enlazar ni ejecutar el motor. Las apariciones retiradas por ser datos se
contabilizan aparte de instrucciones realmente implementadas. Opcionalmente,
`--ld` enlaza los objetos en uno relocatable para detectar definiciones
duplicadas; `--nm` inventaria los simbolos aun pendientes del runtime. Este
enlace parcial permite simbolos externos y no sustituye el enlace del motor.
Un simbolo generado `sub_...` que siga indefinido se trata como un fallo.

La integracion del runtime debe detener la ejecucion en `recomp_unimpl`,
`recomp_icall_fail_log` y `recomp_icall_not_code_log`, conservando diagnostico
del destino. El header actual del SDK puede continuar despues de esos hooks
si retornan, restaurando ESP o dejando EAX en cero. Ese fallback no acredita
que la llamada Windows se haya implementado. Compilar los destinos pendientes
sirve para identificar el fallo; no los convierte en servicios funcionales.

El clasificador actual de llamadas indirectas del SDK permite el rango del
codigo guest y reserva direcciones desde `0xFE000000` para tokens de imports.
El resolver PC debe usar un contrato compatible o adaptar y validar
ese clasificador. El nuevo [runtime CPU](native-cpu-runtime.md) integra
tokens en ese rango y una tabla host de imports. Sus pruebas incluyen
llamadas x86 traducidas por nombre y ordinal, conservando el SDK externo.
Esto no implementa las API Windows; un puntero ARM64 host no cabe en la IAT.

```sh
python pc/tools/aot_code_data.py --analysis /ruta/privada/aot --report /ruta/privada/tablas.json --python-deps /ruta/dependencias --derive /ruta/privada/aot-filtrado
python pc/tools/run_aot_analysis.py --analysis /ruta/privada/aot-filtrado --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias --stage generate --generation-name code-data01 --simd-bits --mul-flags
python pc/tools/verify_aot_code_data.py --analysis /ruta/privada/aot-filtrado --generation-name code-data01 --baseline-analysis /ruta/privada/aot --baseline-name mul-flags02 --sdk /ruta/xboxrecomp --cc /ruta/aarch64-none-elf-gcc.exe --ld /ruta/aarch64-none-elf-ld.exe --nm /ruta/aarch64-none-elf-nm.exe --python-deps /ruta/dependencias --output /ruta/privada/objetos-filtrados --jobs 4
```

La prueba sintetica cubre indices en pila y registro, limites, destinos
interiores a instrucciones, registros equivocados, saltos que eluden el
guard, referencias de callbacks y una funcion real adyacente. Comprueba
ademas que el padre quede intacto y rechaza una seleccion manipulada aunque
se recalculen sus hashes. No usa ni ejecuta codigo del juego.

Resultado de `code-data01` (15 de octubre): se reconocieron 132 patrones y
se apartaron 42 seeds, incluidos los que causaban el error de CR2. Otros tres
patrones se rechazaron por evidencia contradictoria o guard no demostrado.
La generacion termino con 85.885 cuerpos, cero fallos y sus cuerpos retenidos
identicos a los anteriores. Se retiraron como datos 56 apariciones pendientes
y 16 destinos; quedan 797 apariciones y 648 destinos. Esto no se cuenta como
soporte nuevo de instrucciones ni una medida de cobertura en ejecucion.

Compilaron las 346 unidades C (307.214.088 bytes de objetos) y paso el enlace
parcial con `ld -r` (301.285.408 bytes). SHA256 del objeto relocatable:
`37df17252148ac016a55c9ab1cde7013e951c5fd0cb38c5e539e849ee5b53c80`.
No quedo ningun simbolo generado `sub_...` indefinido ni aparecieron fallos de
compilacion. Se registraron los ocho headers del SDK usados como entrada,
el compilador y los flags, y se comprobaron sus identidades al terminar.

El inventario de `nm` conserva 60 simbolos externos: 38 variables de estado,
15 funciones de libc/math y siete funciones del runtime:
`recomp_cpuid`, `recomp_icall_fail_log`, `recomp_icall_not_code_log`,
`recomp_lookup_kernel`, `recomp_lookup_manual`, `recomp_unimpl` y
`xbox_ReadTimeStampCounter`. El nombre kernel del SDK no convierte las
llamadas Windows en llamadas Xbox: necesita un resolver PC apropiado.
Estos 60 simbolos no equivalen a todas las dependencias del juego; las API
Windows de los 280 slots IAT siguen requiriendo implementaciones con ABI
correcta. Tampoco se enlazo un ejecutable final, verifico el arranque desde
disco ni genero un NRO del motor.

El lote anterior valida objetos ARM64 genericos. El ABI TLS de libnx exige
`-mtp=soft -ftls-model=local-exec`; no se deben usar esos objetos anteriores
en la aplicacion Switch. `build_cpu_runtime.py` reconstruye el conjunto
completo con esas opciones, el wrapper de traza por hilo y las bibliotecas
CPU/PE propias. El enlace relocatable sigue sin sustituir un ejecutable,
perfil de entrada original ni renderer del motor.
