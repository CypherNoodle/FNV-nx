# TLS estatico de un EXE PE32

El runtime prepara una copia inmutable de la plantilla TLS de un EXE a su base
preferida. Valida directorio, rangos, alineacion, limite de 1 MiB e indice en una
seccion escribible. La preparacion no modifica la imagen. Un enlace de imports
exitoso asigna indice cero; los rechazos conservan tanto la IAT como el indice.
Tambien se exige que la imagen pertenezca a la arena/base del proceso CPU.

Al adjuntar un hilo, se copia la plantilla, se inicializa su cola de ceros y se
escribe el puntero del bloque en vector[0]. FS:0x2C apunta al vector guest, separado
del TLS dinamico. Cada hilo tiene su propio bloque y las reservas no pueden
solaparse con otros hilos, imagen, PEB, heaps o memoria Virtual*.

El host proporciona static_tls_vector/static_tls_vector_bytes y
static_tls_data/static_tls_data_bytes al hilo principal o externo. Para hilos
administrados, worker_tls_bytes reserva un presupuesto adicional por slot,
multiplo de pagina, que contiene vector y datos alineados. Se suma a las dos
paginas de TEB/expansion y la pila guest. Sin presupuesto suficiente, CreateThread
falla de forma estricta. La pila ARM64 sigue siendo otra asignacion.

La primera implementacion soporta un unico modulo EXE y callbacks vacios. Una
lista no vacia devuelve UNRESOLVED antes de cambiar la imagen. Faltan callbacks
de proceso/hilo, TLS de DLLs, carga dinamica, relocaciones y SEH. No se hace pasar
esta inicializacion de datos por un cargador completo ni por arranque del motor.

## Pruebas y auditoria

test_windows_static_tls.py ejecuta 200 ThreadProcs x86 recompilados que leen
el indice y FS:0x2C, modifican su bloque y verifican aislamiento y zero-fill.
Incluye adjuntar un hilo externo, presupuestos insuficientes, reservas invalidas,
rechazo de imagen ajena y que un bind rechazado no modifique IAT/indice. La
plantilla original se modifica deliberadamente despues de preparar el proceso:
los siguientes hilos deben recibir la copia original, incluso al reutilizar slots.

test_pe_tls.py comprueba 2000 casos de plantilla/alineacion/zero-fill y 16 casos
negativos. Es un modelo de datos, no un oraculo del cargador Windows. Las
regresiones independientes mantienen las comparaciones Windows ya existentes.
El enlace parcial ARM64 incorpora 353 unidades, con -Werror en codigo propio,
TLS host de libnx y externos de plataforma verificados. Aun falta probar este
bloque en la consola; no se genera un NRO nuevo.

La auditoria de solo lectura de la instalacion Steam conocida encontro una
plantilla de 708 bytes, sin cola de ceros y con lista de callbacks vacia. El
parser la acepta y utiliza alineacion predeterminada de 16 bytes. El SHA256 del
EXE se conserva antes/despues. No se ejecuta, modifica ni desempaqueta el EXE;
esto no demuestra compatibilidad del punto de entrada o de sus DLLs.

Contrato de formato: [TLS de PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#the-tls-section).
