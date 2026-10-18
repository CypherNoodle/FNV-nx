# Puentes nativos de audio y video (pendientes)

Ya existe [salida PCM y XInput](native-input-audio.md), con backend audout/HID
compilado para Horizon. Tambien existe [MP3 streaming nativo](native-mp3.md).
Vorbis/Bink y el enlace de estos componentes al motor siguen pendientes.

## MP3 y formatos encontrados

El inventario de solo lectura de esta instalacion encontro 247 archivos sueltos
con extension .mp3 y un .bik. Dos muestras MP3 tienen cabecera ID3; no es un
inventario de audio dentro de BSA ni una validacion completa de todos los frames.
MP3 ya tiene decoder minimp3 y conversion streaming a stereo 48000 s16, con
pruebas reales de pistas a 44,1/48 kHz. La cola no remuestrea: esa conversion
ocurre antes en pcm_convert. Vorbis y audio Bink siguen pendientes; FFmpeg es
una opcion para Bink. Los bridges de audio Windows tampoco estan implementados.

WINMM importa mmioOpenA/Descend/Close/Read/Ascend/GetInfo/Advance y timeGetTime;
ole32 incluye CoCreateInstance. DSOUND usa ordinal 11; DINPUT8 importa
DirectInput8Create. Esto no demuestra que MP3 pase por mmio o DirectShow:
faltan callers y objetos COM concretos. Un decoder no sustituye esas interfaces.
No se cargara quartz.dll x86 en ARM64 ni se modificaran los MP3 del usuario.

Referencias MP3: [mpg123](https://www.mpg123.de/),
[minimp3](https://github.com/lieff/minimp3).

La instalacion Steam auditada importa normalmente nueve funciones de binkw32.dll:
`_BinkSetSoundSystem@8`, `_BinkClose@4`, `_BinkNextFrame@4`, `_BinkWait@4`,
`_BinkPause@8`, `_BinkOpenDirectSound@4`, `_BinkOpen@8`, `_BinkDoFrame@4` y
`_BinkCopyToBufferRect@44`.

Importa seis de libvorbisfile.dll: `ov_open_callbacks`, `ov_clear`, `ov_pcm_total`,
`ov_seekable`, `ov_read` y `ov_info`. Libvorbis es dependencia del DLL Vorbisfile,
no una importacion normal directa de este EXE. Esta lista no descarta cargas
dinamicas. Se obtiene sin ejecutar DLL ni EXE con audit_native_imports.py y
audit_install.pe_info; no es prueba de compatibilidad.

La ruta propuesta es libvorbisfile/libvorbis/libogg compiladas para ARM64, con
un puente x86 cdecl para esos seis exports. Los callbacks de lectura/seek/tell/
close son direcciones guest32, no punteros C host: deben volver al runtime con
el contexto correcto. El registro OggVorbis_File host se guarda por separado,
asociado al objeto guest, y ov_info necesita una vista de vorbis_info con ABI
Windows32. ov_pcm_total devuelve un entero de 64 bits (EDX:EAX en x86).
Nunca pasar un FILE*, struct o callback x86 directamente a libvorbisfile ARM64.

SDL2 puede recibir PCM por una cola limitada o callback de salida; tambien se
puede usar audren. SDL no reemplaza por si solo los exports Vorbis ni el device
DirectSound del motor. Hay que preservar formato, posicion, loops, mezcla y
sincronizacion del juego. Una cola sin limite ocultaria una fuga/latencia; medir
relleno, underruns y tiempos de decodificacion. Los callbacks guest requieren
analisis de reentrada antes de trasladar la decodificacion a otro hilo.

Para Bink se propone FFmpeg (demuxer Bink y decodificadores de video/audio),
frames al renderer Vulkan y audio al mismo mezclador/reloj. Falta implementar
los nueve wrappers stdcall, su vida util y un objeto BINK guest32: el juego puede
leer campos del handle directamente. Una funcion con el mismo nombre no prueba
compatibilidad del layout. Pause/Wait/NextFrame y CopyToBufferRect deben conservar
temporizacion, pitch, formato y limites del destino. No convertir videos del
usuario ni incorporar DLL x86 al NRO esperando que funcionen nativamente.

Primero: auditoria de callers/layouts y callbacks, despues fixtures propios con
PCM y video conocidos, luego comparacion de muestras/frames y prueba Switch.
Ninguno de estos proveedores esta implementado por esta documentacion.
atimgpud.dll y GDFFalloutNV.dll quedan excluidos del paquete Switch; no se
modifica la instalacion Steam ni se fabrican exports exitosos.

Referencias primarias:
[callbacks Vorbisfile](https://xiph.org/vorbis/doc/vorbisfile/ov_open_callbacks.html),
[lectura PCM](https://xiph.org/vorbis/doc/vorbisfile/ov_read.html),
[SDL_QueueAudio](https://wiki.libsdl.org/SDL2/SDL_QueueAudio),
[decodificador Bink de FFmpeg](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/bink.c).
