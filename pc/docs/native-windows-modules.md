# Catalogo de DLL nativas

`windows_modules.c/.h` incorpora seis exports opcionales de kernel32:
LoadLibraryA/W, GetModuleHandleA/W, GetProcAddress y FreeLibrary. Resuelve
implementaciones propias preparadas dentro del ejecutable ARM64. No carga ni
interpreta el codigo x86 de DLLs de Windows y no sustituye automaticamente
binkw32, libvorbis, libvorbisfile, GDFFalloutNV o atimgpud.

## Preparacion e integracion

1. Crear WindowsProcess y los proveedores nativos, antes del primer attach.
2. Reunir un catalogo con `fnv_windows_exports`,
   `fnv_windows_modules_exports` y los exports de los proveedores adicionales.
3. Llamar `fnv_windows_modules_create(process, image, "FalloutNV.exe", catalog,
   count, &modules)`. La imagen debe estar en el arena del CPU correspondiente.
4. Bind de IAT con `fnv_windows_bind_imports`: pasar solamente los exports
   adicionales y los seis de modules. El binder ya agrega los 57 Windows base;
   duplicarlos falla. Preparar modules no sustituye este bind ni ejecuta el EXE.
5. Adjuntar/ejecutar/separar los hilos. Al terminar, destruir primero los
   proveedores de modules y despues WindowsProcess.

Se copian nombres y simbolos a memoria host; el caller debe conservar el codigo
nativo hasta destruir el proveedor. El catalogo es inmutable durante ejecucion.
Las funciones distintas se registran una vez en el registro nativo existente;
los aliases del mismo puntero host, incluso nombre/ordinal, devuelven un mismo
token. No se exponen punteros ARM64 al guest32. El limite del catalogo es 1024
entradas y comparte el presupuesto de metodos con COM/otros proveedores.
Los tokens retirados no se reutilizan durante la vida del CPU. Un fallo de
capacidad del registro no publica un proveedor parcial ni cambia la IAT.

El setup/teardown lo serializa el host y exige el ciclo de vida del CPU. Solo
puede existir un proveedor activo, como para el CPU actual. Destroy devuelve
BUSY si quedan threads adjuntos; WindowsProcess tambien rechaza destroy con
metodos del proveedor aun registrados. El modulo se puede usar desde workers
adjuntos al proceso; no instala implicitamente TEB/TLS.

## Nombres, referencias y llamadas

El namespace soporta nombres de modulo ASCII simples, sin rutas, comparados
sin distinguir mayusculas. Agrega .dll cuando no hay extension y elimina un
punto final explicito. La variante W lee UTF-16LE, pero devuelve error para
nombres fuera del subconjunto ASCII. No hay busqueda en SD ni rutas de Windows.
GetModuleHandle(NULL) devuelve la base guest de la imagen; tambien se reconoce
su nombre EXE configurado. Los HMODULE de DLL son identificadores opacos propios,
no direcciones con cabeceras PE que se puedan recorrer.

Solo aparecen DLLs incluidas en el catalogo; no se inventan modulos ni exports
para satisfacer deteccion de capacidades. GetProcAddress distingue mayusculas y
admite ordinal no cero en los 16 bits bajos. Devuelve un token que puede consumir
el dispatch de llamadas indirectas del x86 recompilado. Las llamadas ya resueltas
no repiten la busqueda de simbolos en cada ejecucion.

Todas las implementaciones del catalogo estan fijadas en memoria durante el
proceso. LoadLibrary incrementa una referencia dinamica; GetModuleHandle no.
FreeLibrary reduce las referencias dinamicas si existen y conserva el modulo
residente. No es el ciclo completo de descarga de DLLs de Windows. El snapshot
`fnv_windows_modules_stats` distingue modulos fijados, exports, cargas exitosas y
referencias dinamicas. Un overflow de referencias falla con error 8.

Modulos desconocidos devuelven NULL/error 126; procedimientos no encontrados
devuelven NULL/error 127; FreeLibrary con identificador ajeno devuelve FALSE/6.
Los exitos conservan LastError. Punteros no legibles, cadenas que desbordan la
direccion guest o proveedor ausente detienen la sesion con diagnostico, fuera
de cualquier lock. No hay SEH. No se dereferencia memoria VM NOACCESS para leer
una cadena. La vida de esa memoria sigue siendo responsabilidad del caller.

## Verificacion y limites pendientes

`test_windows_modules.py` crea nueve funciones x86 propias, las traduce con el SDK
y compila el runtime real. Comprueba 40 casos de nombres/simbolos contra Windows,
ordinales y aliases, snapshot de nombres, errores, referencias y 2000 ciclos
concurrentes en cuatro hilos. El caller x86 usa CALL EAX para llamar al token
obtenido; no es solo una comparacion de nombres. Cinco procesos negativos cubren
puntero fuera del arena, wrap UTF-16, proveedor ausente, VM NOACCESS con permisos
reales del host y registro nativo agotado. CMake incorpora
`fnv_native_windows_modules`; `build_windows_thread.py --modules` agrega la unidad
al enlace parcial ARM64 y verifica los simbolos de libnx/newlib.

Faltan DLL binarias, DllMain/TLS callbacks, forwarded exports, exported data,
API sets, LoadLibraryEx/GetModuleHandleEx, Unicode general y busqueda por ruta.
La IAT estatica mantiene su dominio de tokens previo: comparar sus punteros con
GetProcAddress no garantiza aun identidad aunque ambas rutas llamen la misma
funcion. El EXE no tiene export table registrada por este proveedor.
No hay arranque del motor, NRO nuevo ni medicion Switch de este bloque.

Contratos oficiales: [vinculacion dinamica en ejecucion](https://learn.microsoft.com/en-us/windows/win32/dlls/run-time-dynamic-linking),
[GetProcAddress](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress),
[GetModuleHandle](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandlea).
