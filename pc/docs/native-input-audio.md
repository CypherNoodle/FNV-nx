# Controles y salida PCM nativos

Implementados como proveedores opcionales. No hay motor PC arrancado ni audio
del juego por este cambio. Existe ahora un [primer proveedor DirectSound8 PCM](native-directsound.md)
con buffers/mezcla hacia esta cola y productor audout; falta el listener3D que
pide el arranque del juego. Vorbis/Bink y otras interfaces Windows siguen
pendientes; no se anuncian exports exitosos ficticios para ocultarlas.
DirectInput8A tiene ahora un [subconjunto COM de teclado/raton](native-directinput.md),
con enumeracion y snapshots; no implica que la entrada del motor este completa.
El [decoder MP3](native-mp3.md) ya entrega PCM, separado del puente del motor.

## XInput

xinput.c exporta XInputGetState/SetState por nombre y ordinal 2/3 de
xinput1_3.dll. Son los dos ordinales importados por el EXE Steam auditado;
una consulta a la DLL oficial Windows confirmo nombres/ordinales y error 160
para indices >=4. Se serializan 16 bytes XINPUT_STATE y 4 bytes de vibracion
Windows32; no se copian structs/punteros host al guest. LastError se conserva.
Desconectado devuelve 1167 sin tocar el destino. Buffers invalidos con dispositivo
conectado detienen el guest con diagnostico, fuera de locks. Se respeta VM si existe.

Crear fnv_xinput antes del attach; agregar fnv_xinput_exports al binder y al
catalogo de modulos si se usa carga dinamica. fnv_xinput_update publica snapshots
para hasta cuatro indices. El numero de paquete cambia al cambiar el estado o
la conexion; multiples lecturas iguales no simulan cambios. El callback rumble
recibe los dos motores sin deadzones ni escalado a escondidas. Destruir solo
despues de detach; registro nativo mantiene vivo el proceso mientras exista.
Setup/teardown y updates host se serializan externamente durante destruccion.

input_horizon.c conecta un mando principal de libnx al indice 0. App inicializa
HID; fnv_horizon_pad_open crea proveedor; poll una vez por frame host y close
despues del detach. No hay IPC HID por cada consulta x86. labels=1 corresponde
A/B/X/Y por letra Nintendo; labels=0 por posicion fisica Xbox. Sticks mantienen
signo y rango, ZL/ZR digitales dan 0/255. Otros indices estan desconectados.
La vibracion queda solicitada hasta el siguiente poll. Se detiene al cerrar,
desconectar o cambiar dispositivo; errores HID no se convierten en exito.
Single Joy-Con no tiene implementada esta salida de dos motores (error 50).

## PCM y Horizon

pcm_queue.c ofrece cola limitada de frames stereo s16 a 48000 Hz. No asigna
memoria por bloque. FULL no consume datos: caller reintenta o decide su politica.
El consumidor rellena silencio al faltar frames. Stats: queued/capacity,
written/read/silence/full_writes; no se presenta silencio como audio decodificado.

audio_horizon.c abre audout con tres buffers alineados a 4096 de 512 frames
(32 ms en el servicio), mas la capacidad elegida de cola. Un hilo propio espera
el evento de buffer liberado, con timeout de 20 ms para poder cerrar. No espera
al render ni usa un bucle de sondeo continuo. Cache flush antes del submit,
formato del dispositivo validado y error accesible por audio_error. El caller
debe entregar PCM antes de abrir para evitar silencio inicial y mantener la cola.
Solo un propietario audout; no mezclar con otro usuario/SDL que use esa sesion.
En close se detiene/junta el hilo y se cierra el servicio antes de liberar DMA.
Si close falla, retiene objeto/cola: caller no debe liberarlos.
La cola debe vivir hasta que el backend cierre. No hay mixer 3D; el resampler
MP3 corre antes de esta cola, no dentro del backend audout.

## Verificacion

test_windows_xinput.py traduce dos callers x86 por ordinal real: 5000 estados y
5000 pedidos de vibracion, guardas, paquete estable, LastError/stdcall, indices,
desconexion, vida util y dos controles negativos de memoria. Audio: 60000 pasos
contra modelo de cola y 80000 frames con productor/consumidor concurrentes,
comparando todas las muestras y el silencio de underflow.
build_horizon_media.py exige ese fixture congelado, compila los cuatro modulos
ARM64 con -Werror, enlaza parcialmente y verifica definiciones de libnx. No es
un enlace final del motor ni validacion hardware del mando, rumble o DAC.
Targets CMake: fnv_native_xinput, fnv_horizon_input, fnv_pcm_queue,
fnv_horizon_audio. Falta conectarlos a la futura aplicacion del motor.

Contratos primarios: [XInputGetState](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate),
[controles y vibracion](https://learn.microsoft.com/windows/win32/xinput/getting-started-with-xinput),
[audout de libnx](https://github.com/switchbrew/libnx/blob/master/nx/source/services/audout.c).
