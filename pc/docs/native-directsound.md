# DirectSound8 y salida PCM de Switch

Primer proveedor COM PCM, limitado y opcional. No ejecuta una DLL de Windows.
El catalogo y la IAT admiten DirectSoundCreate8 por nombre y ordinal11. Son
identidades x86 propias con tablas nativas registradas antes de attach, nunca
punteros host copiados al invitado. El dispositivo requiere una cola PCM real.

## Ruta de datos

Lock entrega uno o dos segmentos en una arena invitada dedicada. Unlock valida
la pareja completa y copia las muestras a un almacenamiento nativo propio.
El audio solo lee ese almacenamiento: nunca accede a memoria invitada desde
el worker y no ejecuta callbacks x86. No hay datos compartidos sin mutex entre
Unlock, controles, Release y el mezclador. No se mantiene un lock al abortar.

fnv_dsound8_pump mezcla hasta512 frames de voces PCM mono/estereo8/16 bits,
con frecuencias originales8000..48000Hz, hacia stereo s16 48000Hz. Usa fase
racional y interpolacion lineal; suma en64 bits y satura al final. Admite bucle,
Stop, seek, volumen/pan y frecuencia100..200000Hz con0=original. El control
logaritmico se calcula al modificar volumen/pan, no en cada muestra. La cola
PCM llena revierte todos los cursores/estados de playback; no pierde tiempo.

La posicion publicada corresponde a frames producidos para la cola, antes de
la latencia del dispositivo. El write cursor usa un margen512 frames; todavia
no reproduce el calendario exacto de un driver Windows. No hay oraculo de
calidad/interpolacion de DirectSound: se verifican muestras contra un modelo
PCM escalar independiente. No hay afirmacion de mezcla bit-exact con Windows.

Se reutiliza audio_horizon.c: fnv_horizon_audio_open_source ejecuta un productor
antes de cada lectura512, incluido el llenado inicial. Despues usa los mismos
buffers DMA, espera por evento y worker audout que ya existian. El productor
no agrega un hilo adicional ni depende de frames de GPU. Un fallo del productor
se devuelve durante open o queda visible en audio_error durante playback.
La API anterior fnv_horizon_audio_open permanece disponible sin productor.

## Integracion y vida util

1. Crear cola PCM dedicada(capacidad>=512) y reservar una arena invitada RW,
   disjunta de EXE/TEB/stacks/heaps/otros proveedores.
2. fnv_dsound8_create antes del attach, con arena, VM, cola, foco inicial y un
   validador de HWND invitado. Agregar fnv_dsound8_exports al binder/catalogo.
3. Abrir fnv_horizon_audio_open_source(queue,fnv_dsound8_produce,sound,&audio).
   Mantener sound/queue hasta cerrar audout. El loop host actualiza foco con
   fnv_dsound8_set_focus; el worker no consulta ventanas ni HID. Sin foco se
   silencian voces normales pero avanzan cursores; GLOBALFOCUS sigue audible.
4. Tras detener guest, cerrar/juntar audout, detach de todos los hilos, destruir
   proveedor y liberar arena/cola. Si cerrar audout falla, conservar ownership.

CMake: fnv_native_dsound8. Solo enlace parcial verificado con libnx; no existe
una nueva aplicacion del motor/NRO por este cambio. El diagnostico Windows
conecta una cola headless y no reproduce audio real ni simula audout.

## Limites actuales

256 identidades de vida completa y arena elegida8192..64MiB. El descriptor
consume36 bytes x86. Los primeros4096 bytes son tablas/objetos y el resto staging.
Release libera el shadow nativo pero no recicla identidad/staging: el agotamiento
devuelve E_OUTOFMEMORY. No es aun un allocador para una sesion larga del juego.

Primary permite formato y controles, no escritura/cursor primario. Las capacidades
publican mezcla software, PCM y formatos implementados, sin voces hardware ni
certificacion. Otros formatos, dispositivos por GUID, CTRL3D, notificaciones,
efectos, sticky-focus, write-primary, DuplicateSoundBuffer y metodos pendientes
no se convierten en exito ficticio. QueryInterface solo publica IUnknown,
DirectSound/8 y Buffer/8 realmente modelados; el primario no ofrece Buffer8.
No hay Vorbis/MP3/Bink conectados a este ABI, ni listener/audio espacial 3D.

El replay privado ya supera DirectSoundCreate8, SetCooperativeLevel y GetCaps.
El siguiente pedido del juego es un buffer primario flags0x91(PRIMARYBUFFER,
CTRL3D,CTRLVOLUME): se detiene explicitamente antes de crearlo. Falta implementar
el listener3D y las capacidades que pida despues. No hay frame ni sonido del
motor PC demostrado, ni arranque frio desde EXE ni prueba hardware de esta ruta.

## Verificacion

test_dsound8.py:26 callers x86 traducidos,12598 muestras exactas,20 combinaciones
de formato/rate, mono/estereo8/16bits, bucle, final de playback, wrap Lock/Unlock,
mezcla/saturacion, mute/pan, foco y rollback con cola llena. Un host thread sin
attach produce PCM. Diez comparaciones ABI con DirectSound Windows; buffers
nativos nunca se ponen a reproducir. Cuatro casos negativos: salida invalida,
identidad retirada, formato pendiente y flags3D que realmente pide el juego.
Se repite con el ABI x87 extendido. build_dsound_audio.py comprueba el proof
vigente, compila3 unidades ARM64/libnx con Werror y verifica enlace interno
DirectSound/PCM/audout y exports de libnx. No enlaza ni entrega NRO.

Referencias primarias:
[DirectSoundCreate8](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416762(v=vs.85)),
[Lock](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418073(v=vs.85)),
[Play](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418074(v=vs.85)).
