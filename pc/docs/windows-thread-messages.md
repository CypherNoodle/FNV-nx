# Cola de mensajes de hilo USER32

El proveedor de escritorio virtual implementa PeekMessageA para las consultas
con hWnd=0 o hWnd=-1, filtros de rango de 16 bits y PM_REMOVE/PM_NOREMOVE,
con PM_NOYIELD opcional. Los mensajes publicados por el adaptador nativo se
guardan en orden FIFO y pertenecen a un identificador de hilo guest concreto.
WM_QUIT supera el filtro numerico. Al no encontrar un mensaje no se modifica
el buffer MSG del invitado; LastError se conserva.

La estructura guest es MSG x86 de 28 bytes, independiente del MSG del host de
64 bits. No se conserva ningun puntero al buffer guest. La comprobacion de
acceso escribible precede a retirar un mensaje. Un fallo de forma, memoria,
ventana concreta o flags QS aun no implementados es explicito.

fnv_windows_user32_post_thread recibe un mensaje nativo por valor y un ID de
hilo guest no nulo. Comparte el mutex del escritorio con PeekMessageA y devuelve
NO_MEMORY si las 256 entradas globales estan ocupadas, sin descartar mensajes.
El productor conserva el escritorio durante la llamada; el ciclo de vida sigue
siendo crear antes del attach y destruir despues del detach de todos los hilos.
El productor debe usar IDs vivos; aun no hay purga automatica por salida de hilo.

Esta cola NO implementa mensajes enviados sin cola, dispatch de callbacks,
mensajes dirigidos a ventanas o hijos, WM_PAINT sintetizado, timers, conversion
Unicode/ANSI, GetMessage, PostMessage guest ni PostQuitMessage guest. Tampoco
esta conectada al sondeo HID o a un teclado real. El motor con una cola vacia
puede consultar correctamente si hay mensajes; eso no prueba entrada interactiva.

Validacion: test_windows_messages.py traduce un PE sintetico y compara 2001
consultas con USER32 de Windows real. Comprueba PM_REMOVE, PM_NOREMOVE,
cola vacia, filtros, WM_QUIT, aislamiento entre dos IDs guest, saturacion a 256,
orden de drenaje, ABI stdcall y cuatro errores explicitos. Los campos de tiempo
y posicion son los suministrados por el adaptador nativo, no los del escritorio
Windows. Las pruebas de ventanas y pantalla se ejecutan por separado y conservan
sus comparaciones existentes. El proveedor compila con devkitA64/libnx tanto en
modo x87 heredado como extendido; no se ha ejecutado esta cola en hardware.

La prueba de regresion de ventanas detecto durante el desarrollo un vector de
tokens demasiado pequeno al registrar dos funciones nativas; se corrigio antes
de guardar el cambio. Los contadores de ventanas ya no se pisan con tokens.

Contrato de referencia:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-peekmessagea
