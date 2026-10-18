# Runtime de CPU para la ruta PC

`pc/native/cpu_runtime.c` conecta el cargador PE propio con el C generado
por el SDK x86 de SH2/SH3. Implementa estado de CPU por hilo, enlace de
imports estaticos y una frontera de diagnostico. No es todavia un launcher
del motor: no implementa las API Windows, un renderer D3D9 ni el arranque
original de `FalloutNV.exe`.

## Estado y concurrencia

Cada hilo host que ejecuta codigo guest debe llamar a `fnv_cpu_attach` con
sus registros, pila y direccion FS; al terminar debe hacer `fnv_cpu_detach`.
El llamador prepara el frame x86, incluida su direccion de retorno. El
runtime no inventa un punto de entrada ni construye un TIB Windows.
Al usar el modulo Windows adicional, utilizar su ciclo thread_attach/detach
para preparar tambien el TEB, en lugar de mezclar ambos ciclos directamente.

GPR, FS, estado x87 del SDK, XMM, MMX y traza de llamadas son TLS host.
La imagen y su traduccion de direcciones son compartidas e inmutables
mientras existan hilos adjuntos. Un bloqueo protege solo el ciclo de vida
y el enlace inicial; no serializa la ejecucion guest. Esto no implementa
`CreateThread`, eventos, afinidad ni el planificador Windows.

El SDK original declara la traza indirecta como variables globales. Nuestro
`fnv_sdk_runtime.h` redirige sus usos a TLS sin editar el SDK. Se debe
incluir forzosamente en **todas** las unidades C generadas y en el runtime;
no mezclar objetos anteriores que usen la traza global.

La estructura publica describe el modelo actual del SDK: no es EFLAGS
completo, x87 de 80 bits, MXCSR ni excepciones Windows. Las macros MEM del
codigo generado tampoco comprueban paginas. Los helpers de imports si
comprueban los limites del arena plana; no sustituyen VirtualAlloc o SEH.

## Llamadas e imports

El registro de exports requiere funciones nativas con ABI guest, por nombre
o por ordinal. Los nombres de DLL ignoran mayusculas; los simbolos conservan
su capitalizacion. Cada funcion nativa distinta recibe un token de 32 bits desde `0xFE000000`,
resuelto al puntero ARM64 en una tabla host. Nunca se trunca un puntero host
para guardarlo en la IAT x86.

La operacion es atomica: si falta un import, conserva la IAT completa y el
registro activo. Se permite un enlace estatico exitoso antes de adjuntar
hilos. El catalogo nativo opcional resuelve GetProcAddress con identidad IAT;
no carga ni descarga DLL binarias. El nombre
`recomp_lookup_kernel` viene del SDK externo; aqui resuelve imports PC y
no suministra servicios Xbox.

`fnv_cpu_argument_u32` y `fnv_cpu_return_u32` ayudan a implementar argumentos
y limpieza de pila cdecl/stdcall. No deducen firmas ni efectos de las API.
La prueba del runtime CPU utiliza servicios sinteticos. El modulo adicional
[Windows por hilo](native-windows-thread.md) aporta ahora ocho bridges reales
de TLS/LastError/IDs; los otros 272 slots normales del PE examinado siguen
pendientes. No hay todavia una IAT completa para arrancar el juego.

## Fallos y proveedores

Una instruccion pendiente, destino no registrado o argumento fuera del arena
captura registros y traza y sale mediante la frontera setjmp/longjmp nativa.
No continua con EAX=0 ni simula que una API tuvo exito. La sesion queda
invalidada y no puede reanudarse; otros hilos lo detectan al entrar o volver
de una ejecucion. No se interrumpe de forma asincrona un bucle guest que ya
esta ejecutandose. Un fallo fuera de la frontera termina el proceso.

CPUID y RDTSC requieren proveedores explicitos. CPUID recibe leaf/subleaf;
el reloj declara frecuencia guest y no puede retroceder por hilo. No se
exponen funciones ARM como capacidades x86 ni se intercambian las frecuencias
Xbox, contador ARM y RDTSC. Los callbacks y su contexto deben ser seguros
ante llamadas concurrentes y el diagnostico no debe reentrar en el guest.
Esta frontera no captura fallos del SO host ni implementa SEH guest.

## Compilar para Switch

El [registro de metodos nativos para COM](native-d3d9-com.md) agrega tokens
desde 0xFE010000, independientes de los imports. Se prepara antes del primer
attach, conserva indices sin reutilizarlos y solo permite retirar handlers
cuando no quedan hilos adjuntos. Destroy exige retirarlos primero. No cambia
el ABI de registros o los macros del SDK. Hay helpers de preparacion de
rangos y comprobacion no faulting, sin acreditar permisos ni SEH.

ARM64 generico no basta para TLS de Horizon. Seguir las opciones de la
plataforma NintendoSwitch de devkitPro:

```text
-march=armv8-a+crc+crypto -mtune=cortex-a57
-mtp=soft -ftls-model=local-exec -fPIE
```

Con `-mtp=soft`, el compilador obtiene el thread pointer mediante
`__aarch64_read_tp`, suministrado por libnx. No usar el valor directo de
TPIDR_EL0 para estas variables. Ver la
[explicacion del ABI por el mantenedor de libnx](https://github.com/switchbrew/libnx/issues/661)
y la [inicializacion TLS de libnx](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/newlib.c).
La referencia de compilacion local es `devkitPro/cmake/Platform/NintendoSwitch.cmake`.

`CMakeLists.txt` conserva la biblioteca PE independiente. Activar
`-DFNV_X86_SDK=/ruta/xboxrecomp` agrega `fnv_native_cpu` y el target interface
`fnv_generated_cpu_abi`; aplicar este ultimo a cada target de C generado.
Usar el toolchain Switch de devkitPro para construir la aplicacion final.
No se genera un NRO desde estas bibliotecas.

Para reconstruir el conjunto privado completo:

```sh
python pc/tools/build_cpu_runtime.py \
  --analysis /privado/analisis-derivado --generation-name code-data01 \
  --audit-validation /privado/code-data-object-check/validation.json \
  --sdk /ruta/xboxrecomp --python-deps /ruta/dependencias \
  --cc /ruta/aarch64-none-elf-gcc --ld /ruta/aarch64-none-elf-ld \
  --nm /ruta/aarch64-none-elf-nm --objdump /ruta/aarch64-none-elf-objdump \
  --output /privado/cpu-runtime-switch --jobs 4
```

La herramienta exige una auditoria previa exitosa de codigo/datos, verifica
hashes antes y despues, recompila cada unidad con el wrapper y el ABI Switch,
incluye el runtime y cargador PE y realiza un enlace relocatable. Rechaza
externos fuera de una lista explicita libc/math/newlib/libnx. Inspecciona
el acceso TLS en el runtime y una unidad generada; las restantes reciben
las mismas opciones, sin reutilizar objetos ARM64 genericos. La salida
derivada del juego debe mantenerse privada.

Si cambia solo el auditor, `--recheck --previous-validator /ruta/script-anterior.py`
permite revisar el mismo lote. Exige que el script archivado coincida con
el hash original y que fuentes, SDK, herramientas, objetos y enlace no hayan
cambiado. Conserva `validation.json` y escribe `revalidation.json`; no sirve
para aceptar cambios de codigo sin recompilarlos.

## Evidencia y limite

La prueba host traduce ocho rutinas x86 sinteticas con el generador real,
ejecuta 1.000 llamadas sobre dos hilos y comprueba registros, FS, vectores,
traza, imports por nombre/ordinal y limpieza de pila. Los doce resultados
de fallo incluyen el diagnostico, la interrupcion antes de la instruccion
posterior al fallo originario y la invalidacion detectada por otro hilo al regresar.
Tambien rechaza atomicamente los imports del PE Steam, cargado solo
como datos, y comprueba la terminacion ante un fallo fuera de la frontera.

```sh
python pc/tests/test_cpu_runtime.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --output /privado/prueba-runtime \
  --cc /ruta/zig cc --game /ruta/Fallout-New-Vegas
```

`--game` es opcional. Ni estas pruebas host ni el enlace ARM64 prueban
arranque del motor, rendimiento o comportamiento en consola. La captura de
texto usada en el analisis no equivale a una entrada original validada.

El 15 de octubre compilaron las 348 unidades: 346 generadas, runtime CPU y
cargador PE, todas con opciones Switch. El enlace relocatable ocupa
310.411.744 bytes y no conserva simbolos propios de CPU o `sub_...` sin
resolver. Sus 30 externos corresponden a libc/math/newlib y al acceso TLS
de libnx. La inspeccion del runtime y una unidad generada confirma el uso
de `__aarch64_read_tp`. Todavia no se enlazo un ejecutable del motor.

`fnv_cpu_import_token` consulta el token canonico desde un thread adjunto, sin
registrar metodos ni modificar la tabla. Devuelve cero para funciones ausentes,
runtime sin bind o thread no adjunto. Los aliases de imports no duplican tokens.
