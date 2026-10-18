# Puentes nativos de audio y video (pendientes)

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
