# Acceso a archivos desde Win32 guest

WindowsThread incorpora CreateFileA/W, ReadFile, WriteFile, GetFileSize/Ex y SetFilePointer/Ex.
El catalogo base contiene 90 exports, incluyendo
[atributos y enumeracion](native-windows-directory.md) y servicios del CRT. CloseHandle usa el mismo import existente y
dirige los handles de archivo al proveedor; no reemplaza ni duplica ese export.
Eventos, mutex, semaforos y threads conservan su tabla y comportamiento.

## Configuracion y alcance

El host configura `file_root` con la carpeta del juego, por ejemplo
`sdmc:/switch/FNV-nx`, y `file_handle_capacity` entre 1 y 4096. El runtime copia
la raiz durante create. Con ambos en cero se deshabilita el proveedor. Raiz
inexistente, combinaciones incompletas o limites invalidos se rechazan y
deshacen la inicializacion del proceso. No se crean carpetas ni archivos.

Los archivos del juego se abren solo con GENERIC_READ y OPEN_EXISTING. Para el
prefijo virtual `C:\Users\FNV\` se admiten GENERIC_WRITE o READ|WRITE y los cinco
dispositions CreateFile; CREATE_NEW es exclusivo y CREATE_ALWAYS trunca. Esta
ruta se mapea dentro de `UserData/FNV` bajo `file_root`, nunca al perfil del host.
Los flags secuenciales/aleatorios se aceptan como hints; no se promete un efecto
de rendimiento. Se aceptan separadores de Windows y '/',
normalizan '.' y '..' dentro de la raiz y rechazan un '..' que salga por encima.
El namespace guest admite ASCII; la variante W lee UTF-16LE pero no implementa
conversion Unicode general. La distincion de mayusculas al abrir depende del
filesystem host. Las claves para compartir aperturas se comparan en ASCII sin
mayusculas; no detectan aliases de symlinks, hardlinks o nombres cortos.

No hay busqueda de archivos, rutas absolutas guest, unidades Windows, dispositivos,
streams alternativos, SECURITY_ATTRIBUTES, templates, OVERLAPPED, IOCP ni cancelacion.
WriteFile es sincrono, exige un handle con permiso de escritura y rechaza OVERLAPPED.
Las escrituras relativas a `Data` u otras rutas del juego fallan cerradas; no hay
todavia DeleteFile, renombrado, creacion Win32 de directorios, archivos mapeados,
WritePrivateProfileString ni FlushFileBuffers. GetFileAttributes y FindFirst/FindNext/FindClose ahora tienen un
[proveedor de inventarios](native-windows-directory.md), con sus limites explicitos. Tampoco hay
guardados ni archivos mapeados a memoria en este bloque.

## Cursores, handles y locks

Cada apertura tiene FILE* propio y un mutex de IO. ReadFile, consultas y seeks
del mismo handle se serializan; abrir el mismo archivo dos veces crea cursores
independientes. La comprobacion de sharing de lectura/escritura se hace bajo el
lock de tabla entre aperturas de este proveedor. No implementa arbitraje con otros
procesos/otros proveedores del host. El acceso efectivo queda limitado por los
derechos registrados en cada handle.

La tabla de archivos tiene lock separado de eventos/threads. fopen, fclose,
fread, fstat y seeks no toman el lock de eventos ni mantienen el lock de tabla
durante IO. Cada operacion fija un slot antes de soltar la tabla; CloseHandle
retira el handle de inmediato y difiere fclose y reciclaje hasta el ultimo pin.
Cerrar durante una operacion es una politica segura propia, sin prometer el
resultado del caso indefinido en Windows. No se cierran streams usados por peers.
Destruir el proceso exige detach de todos los threads y cierra archivos restantes.

Los handles usan prefijo D, indice adaptativo, generacion y dos bits bajos de
etiqueta ignorados. No son FILE* ni direcciones guest. Lookup y reciclaje FIFO
son O(1); la comprobacion de sharing recorre la tabla al abrir. Las generaciones
no hacen wrap: se retira el slot agotado. `file_generation_limit=0` selecciona el
maximo de los 26 bits restantes despues del indice; un limite menor permite
pruebas de agotamiento. Cada apertura puede asignar buffers internos de stdio.

Los tamanos, cursores y offsets se manejan en 64 bits. Horizon requiere off_t
de al menos 64 bits; Windows usa _fstat64/_fseeki64/_ftelli64. Se comprueban seeks
negativos y overflow antes de cambiar el cursor. GetFileSize y SetFilePointer
limpian LastError cuando el resultado bajo es FFFFFFFF pero la operacion fue
exitosa. Se conserva LastError en los demas exitos del subconjunto probado.
EOF sincronico devuelve TRUE y cero bytes; no es un fallo. ReadFile pone cero
en el contador antes de validar el resto de los argumentos.

Los buffers de IO y resultados se validan en el arena y contra permisos VM
antes de fijar un slot o tomar su lock. Los punteros invalidos producen
diagnostico estricto, no SEH ni errores de Windows simulados. El caller mantiene
vivos sus buffers durante la llamada; la verificacion no fija paginas VM frente
a una liberacion concurrente del propio guest.

## Medicion y verificacion

`fnv_windows_file_stats` expone handles vivos, slots retirados, aperturas,
lecturas, bytes, errores backend, tiempo dentro de fread y tiempo para adquirir
tabla+lock de IO. Los tiempos usan el reloj host; no el QPC guest inyectable.
El tiempo de fread puede incluir cache de stdio/OS: no mide por si solo latencia
fisica de SD ni demuestra impacto en FPS. Los fallos anteriores a fread no se
cuentan como errores backend de lectura.

`test_windows_files.py` ejecuta once wrappers x86 traducidos y compara 2000
operaciones con archivos equivalentes abiertos por Windows real. Comprueba EOF,
errores de seek, LastError, contenido, ANSI/UTF-16, handles etiquetados y obsoletos,
sharing, normalizacion, capacidad, configuraciones invalidas y retiro de slots.
Un archivo sintetico sparse de 8589934591 bytes verifica high DWORD y offsets
mayores de 4 GB sin reservar ese espacio fisico. Cuatro threads consumen 128
bloques del mismo cursor y comprueban que cada bloque se lea exactamente una vez.
Cuatro procesos negativos prueban cadenas/buffers fuera de limites, proveedor
deshabilitado y buffer VM NOACCESS con permisos reales del host.

`--game-root` agrega una comparacion de lectura de FalloutNV.exe, FalloutNV.esm,
Fallout - Textures.bsa y Fallout - Sound.bsa. Lee inicio y final de cada archivo
mediante los wrappers x86 y el proveedor real, y los compara con Windows: 32768
bytes. Verifica tamano y fecha de escritura antes/despues, sin publicar bytes ni
ejecutar el codigo del juego. Es una prueba de lectura; no valida el entry point,
DRM, parsing completo de assets ni arranque del motor.

El build CMake ARM64 pasa con -Werror. La integracion parcial verifica los
simbolos stdio/newlib y libnx; todavia no hay NRO nuevo ni ejecucion de esta
capa de archivos en Switch. Las fixtures, fuentes y herramientas son propias;
no se incorpora codigo filtrado de Windows.

Contratos: [ReadFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile),
[SetFilePointerEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfilepointerex),
[GetFileSizeEx](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfilesizeex),
[CreateFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

La prueba sintetica del perfil cubre CREATE_ALWAYS+WriteFile sobre
`C:\Users\FNV\AppData\Local`, vuelve a leer los bytes desde el host y comprueba
que una escritura a `Data\...` se rechaza. No acredita todavia un guardado real
de FNV ni la escritura de INI.

La revision del proveedor confirma que CloseHandle enruta D a archivos, C a
busquedas (rechazadas por ese API), y E al kernel; los tests cubren tipos,
handles forjados y cierres concurrentes. fread limpia errno antes de leer y
captura su valor inmediatamente. Si el backend falla sin informar errno,
se usa ERROR_IO_DEVICE (1117), no un error obsoleto de una operacion anterior.
Las lecturas usan `rb` para assets; el subconjunto de escritura queda confinado
al perfil virtual y no habilita rutas de assets.

