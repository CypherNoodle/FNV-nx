# Localizacion de archivos y metadatos

WindowsThread incorpora GetFileAttributesA/W, FindFirstFileA/W, FindNextFileA/W,
FindClose y GetFileType: 73 exports base en total. Usa la raiz y capacidad de
archivos existentes, sin crear carpetas ni modificar assets. Los imports reales
de Fallout incluyen las variantes A, FindClose y GetFileType; W queda disponible
para otros callers.

## Busquedas y resultados guest32

FindFirstFile abre un cursor nativo del directorio padre y filtra los nombres.
Admite nombres ASCII, matching sin mayusculas y patrones comunes con '*',
incluidos *.bsa, *.esm, *.*, prefix*, plain.* y *., comparados contra Windows.
No implementa toda la semantica DOS de wildcards: '?' y wildcards en directorios
padre fallan con error 50. No se ordenan resultados. El caller no puede depender
del orden del filesystem. Se omiten '.' y '..' deliberadamente; los tests tambien
los excluyen al comparar conjuntos con Windows. No es una replica completa de
la enumeracion Win32. Los nombres no ASCII se omiten y se cuentan en skipped_non_ascii, sin
cortar la enumeracion. El adaptador POSIX tambien omite archivos especiales
y los cuenta en skipped_special. No se anuncia soporte Unicode para abrirlos. Las rutas comparten la normalizacion y limites de la lectura.

WIN32_FIND_DATA se escribe campo por campo en little endian. A ocupa 320 bytes,
W 592; los nombres W se convierten a UTF-16LE a partir del subconjunto ASCII.
Incluye atributos, tres FILETIMEs, size high/low, reservados, nombre y nombre
alternativo. Las escrituras no exceden el tamano del resultado. Un fallo deja
el buffer intacto. FindFirstFile sin coincidencias devuelve INVALID_HANDLE_VALUE
y error 2; FindNextFile agotado devuelve FALSE y 18, tambien en consultas repetidas.

Los cursores tienen identificadores C separados de los handles de lectura D.
FindClose retira un cursor; CloseHandle lo rechaza con error 6. GetFileType solo
reconoce archivos regulares abiertos por la capa de lectura y devuelve DISK.
Los prefijos se validan contra el tipo del slot, no basta con cambiarlos para
usar un cursor como archivo. Un token obsoleto no vuelve a ser valido.

Las busquedas comparten capacidad y generacion con los slots de archivos. Si
la tabla esta llena fallan con 4 y cierran el cursor host que acababan de abrir.
Los cursores tienen el mismo mutex y pinning de IO de archivos: avanzar desde
varios threads se serializa; FindClose difiere la liberacion nativa hasta el
ultimo pin. Las operaciones de directorio no mantienen el lock de eventos,
threads o tabla de archivos mientras ejecutan IO. El proceso libera cursores
restantes cuando todos los threads se han separado.

Los buffers guest se validan antes de adquirir un cursor. Un puntero no legible/
escribible produce diagnostico estricto fuera de los locks. No se implementa SEH
ni se fijan paginas guest frente a VirtualFree concurrente. Los exitos preservan
el LastError guest; no se promete reproducir efectos no documentados de LastError
de Windows tras un exito. El snapshot de archivos agrega live_searches, searches,
find_next_calls y attribute_queries. live_handles sigue contando solo archivos
de lectura; el uso de slots es live_handles + live_searches, mas cierres en curso.

## Adaptadores del host

En Windows, el adaptador enumera con las APIs publicas del sistema; nuestro
filtro y marshal guest se prueban contra un segundo enumerador Windows usando
el patron solicitado. Esta comparacion valida matching, ABI y ciclo de vida;
no es un oraculo independiente para la implementacion de metadatos del sistema.

En Horizon/POSIX se usan opendir/readdir/stat. Atributos disponibles: directorio,
archivo regular/archive y readonly segun permisos; faltan atributos FAT especificos
como hidden/system y nombres cortos. Los tiempos de acceso/escritura convierten
segundos Unix a FILETIME UTC, con comprobacion de rango y resolucion de segundos.
CreationTime queda en cero: st_ctime es cambio de metadatos y no se usa como una
fecha de creacion ficticia. La semantica de links/reparse points y equivalencia
completa con atributos Windows siguen pendientes. No hay prueba runtime de este
adaptador POSIX en Switch; compilarlo y resolver sus simbolos no la sustituye.

GetFileAttributes admite archivos, directorios y '.' para la raiz. CreateFile
rechaza ahora un separador final en vez de abrir por error un archivo como si
esa barra no estuviera. Los roots y cursores no cambian el cwd global del host.
GetCurrentDirectory, GetFullPathName y funciones civiles de tiempo aun faltan.

## Verificacion

test_windows_directory.py traduce doce wrappers x86 propios. Compara 24 conjuntos
de busqueda A/W, metadatos y nombres alternativos; doce consultas de atributos;
tamano ANSI/W y guardas de buffer; EOF, tipos de handle, capacidad y cursores
obsoletos. Un archivo sparse de 8589934591 bytes cubre sizeHigh/sizeLow. Cuatro
threads consumen un mismo cursor y cada entrada aparece exactamente una vez.
Dos procesos negativos cubren buffer fuera del arena y proveedor deshabilitado;
un tercero comprueba que un nombre no ASCII se omita sin cortar el listado. No se usa el orden de resultados como expectativa.

--game-root compara enumeraciones reales de Data/*.bsa y Data/*.esm de Steam,
en A y W, sin ejecutar el EXE ni publicar sus datos. Las regresiones de lectura,
modulos, workers, TLS, sincronizacion, heaps y VM se mantienen. El build ARM64 y
el enlace parcial verifican opendir/readdir/closedir en libsysbase y strcpy en
newlib. Todavia no hay NRO nuevo, frame del motor PC ni medida de FPS del juego.

Contratos: [FindFirstFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilea),
[FindNextFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilea),
[WIN32_FIND_DATA](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-win32_find_dataa),
[GetFileAttributes](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileattributesa).

## Revision de continuidad y diagnostico

Los nombres no ASCII ya no devuelven 1113 a mitad del recorrido: se omiten y
se cuentan. El EOF final conserva error 18 en FindNext. Los tipos POSIX especiales
(FIFO/dispositivo/socket) se omiten con contador, evitando terminar el escaneo
por error 50. Los errores reales de IO y los nombres excesivos siguen siendo
fallos explicitos. El proveedor no aplica case-insensitivity al lookup de fopen
en ext4; en hosts POSIX sensibles a mayusculas se requiere el nombre real.

La prueba Windows inserta un nombre no ASCII y conserva todas las entradas
ASCII en A/W hasta EOF, con stats de omisiones. La prueba POSIX crea un FIFO y
dos archivos normales, verifica continuidad, contador y EOF; no abre el FIFO.
GetFileAttributes sobre un tipo especial aun devuelve error 50. El CLOCK_REALTIME
de Linux se ejecuta en esa prueba; esto no valida la inicializacion UTC de libnx.
