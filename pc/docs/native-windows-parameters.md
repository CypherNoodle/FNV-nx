# Parametros del proceso y argumentos guest32

GetCommandLineA/W devuelven cadenas persistentes en memoria guest, preparadas al
crear el proceso. fnv_windows_config recibe parameters_address/parameters_bytes
(arena RW comprometida, alineada a pagina), command_line e image_path. Los campos
se configuran juntos o se dejan todos a cero. Se rechazan solapes con imagen,
PEB, VM, heap y workers; attach tambien rechaza TEB/TLS/stack sobre esa arena.
Las cadenas host se copian antes de escribir destinos, no se retienen. El
snapshot de configuracion admite ASCII: hasta 32767 caracteres de comando y
1023 de ruta virtual. La version W se almacena en UTF-16LE. No se devuelve un
puntero host truncado. El consumidor no debe modificar ni liberar esas cadenas.
No se implementa todavia RTL_USER_PROCESS_PARAMETERS ni su enlace PEB+0x10.

CommandLineToArgvW (shell32.dll) toma un snapshot de hasta 32767 unidades UTF-16
guest. El parser conserva unidades Unicode, incluidas parejas surrogate; no
convierte Unicode a ANSI. La primera palabra y las reglas de comillas/barras
se comparan con Shell32 real. La cadena vacia devuelve un argumento con la ruta
virtual del EXE, incluidos sus espacios; esa ruta no se vuelve a tokenizar.

El resultado contiene punteros guest32 y cadenas UTF-16LE en una sola reserva
del heap del proceso. LocalFree retira ese bloque y devuelve NULL. Solo se
admiten estos bloques fijos, no handles movibles ni GlobalAlloc/LocalAlloc.
LocalFree(NULL) es inocuo; interiores, dobles liberaciones, cadenas permanentes
y bloques ordinarios de HeapAlloc fallan con error 6. Un tag de ownership en los
metadatos evita confundirlos. El metadata de bloque crece de 12 a 16 bytes;
sigue creciendo de forma perezosa. HeapReAlloc conserva el tag si mueve el bloque.

El parser requiere el proveedor de parametros y un heap para devolver memoria.
Los punteros se validan y el scratch host se reserva antes del lock de heap.
Ningun diagnostico sale con ese lock tomado. Las reservas fallidas dejan argc
intacto y devuelven NULL/error 8; las entradas que exceden el presupuesto dan 87.
El scratch de una llamada esta acotado a aproximadamente 256 KiB y se libera
antes de devolver, tambien si el puntero de entrada es invalido. Las operaciones
VM concurrentes sobre buffers guest mantienen la limitacion de falta de pinning
ya documentada en el resto de las APIs. No hay SEH de excepciones Windows.

## Verificacion

El runner traduce once wrappers propios, incluido el import de Shell32. Compara
6302 entradas con CommandLineToArgvW de Windows, incluidas series de comillas y
barras, controles, Unicode, entrada vacia y longitud maxima. Comprueba stack,
LastError, snapshot de configuracion, propiedad y reciclado, OOM sin publicar
argc, HeapReAlloc que mueve un bloque y 1000 ciclos desde cuatro hilos. Cuatro
procesos negativos cubren entrada/salida invalidas, wrap y proveedor ausente.
No se ejecuto el motor ni se genero un NRO por estas pruebas.

Contratos: [GetCommandLineW](https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getcommandlinew),
[CommandLineToArgvW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw),
[LocalFree](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-localfree).
