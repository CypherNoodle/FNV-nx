# MP3 nativo y conversion a PCM

mp3_stream.c usa minimp3 externo fijado a ea99364f61c14656440e8d77e9c233ccf3124633
(CC0). fetch_minimp3.py descarga dos headers y LICENSE, valida SHA256 antes de
escribir y verifica una carpeta existente sin sobrescribirla. Son dependencias
de compilacion, no archivos que el usuario copie a la SD. El decoder se enlaza
estaticamente; no interpreta DLL Windows ni modifica pistas del juego.

Preparacion: `python pc/tools/fetch_minimp3.py --output RUTA_EXTERNA` y configurar
CMake con `-DFNV_MINIMP3_ROOT=RUTA_EXTERNA`. CMake vuelve a comprobar los tres
hashes; targets fnv_mp3_stream/fnv_pcm_convert, junto con fnv_pcm_queue.

## Contrato

fnv_mp3_open recibe callbacks host read/seek con resultado de E/S separado de EOF.
El caller conserva el contexto hasta close. fnv_mp3_open_file acepta una ruta
host ya validada, solo archivo regular abierto rb; nunca pasar una ruta guest
sin comprobar. Un stream tiene un solo propietario, no lecturas concurrentes.
No usa FILE* ni callbacks x86 del motor: ese puente queda pendiente.

Streaming con buffer de E/S de 128 KiB, datos de salida acotados y sin cargar
la pista entera ni crear un indice de duracion. Busqueda inicial limitada para
datos sin frames validos. No hay asignaciones por muestra/bloque. Rewind vuelve
a abrir el decoder sobre los mismos callbacks y descarta el bloque pendiente;
no hay seek arbitrario ni posicion de reproduccion del dispositivo todavia.

Decodifica MPEG Layer III mono/stereo a s16. Mono se duplica a L/R. Se aceptan
frecuencias 8000..48000, con salida stereo 48000 Hz. pcm_convert.c conserva una
fase racional y una muestra de lookahead entre bloques; no cambia tono/duracion
por usar 44,1 kHz como 48 kHz. Conversion lineal inicial, no filtro sinc de alta
calidad; no acepta downsampling desde >48 kHz. El motor puede requerir despues
un mezclador/resampler de mayor calidad para muchas voces.

fnv_mp3_read devuelve PCM al caller; fnv_mp3_pump entrega bloques a la cola.
Si esta llena devuelve AGAIN y retiene los datos para reintento sin avanzar otro
bloque ni perder muestras. No hace espera ocupada. No intercalar read mientras
haya un bloque pendiente. EOF se distingue de error de formato y de E/S; en un
error puede haber un prefijo valido en frames, que el caller debe atender.
Stats source_frames incluyen lookahead; output_frames cuenta frames convertidos,
incluido el bloque pendiente, no audio reproducido por el dispositivo.

La aplicacion puede bombear desde su hilo de decodificacion mientras el worker
audout consume PCM. Aun no hay ese reproductor del motor ni un puente desde sus
APIs de MP3/DirectShow. Existe un [primer proveedor DirectSound8 PCM](native-directsound.md),
con volumen/pan, buffers y estado del listener3D; las voces espaciales siguen pendientes.
Bink y Vorbis tambien siguen pendientes en esta ruta.

## Relacion con DirectSound

El decoder se conserva como modulo opcional probado; no lo llama aun el motor.
DirectSound recibe PCM y no reemplaza un decoder MP3. fnv_mp3_read ya ofrece el
formato de salida48000Hz stereo s16 que puede consumir la ruta de audio propia.
Falta identificar y conectar la API real con la que el motor abre/controla MP3.
No basta con enlazar el decoder para anunciar musica funcional.

No bombear fnv_mp3_pump y DirectSound sobre una misma cola de salida: escribir
ambos consecutivamente intercalaria audio en vez de mezclarlo. El futuro puente
debe entregar PCM decodificado a una voz/buffer del mezclador, o usar una cola
independiente para una prueba de reproduccion aislada. No hacer E/S SD y
busquedas MP3 dentro del callback de mezcla de audout.

pcm_convert conserva lookahead y estado de un stream/EOF. El resampler de
DirectSound opera sobre buffers circulares con seek, looping y cambio de
frecuencia. Son contratos distintos; el conversor existente sigue usandose en
MP3 y no se elimina como duplicado por semejanza de formulas.

## Verificacion y limites

test_mp3_stream.py compila drivers propios y compara 70 casos de conversion
contra un modelo entero Python: 7 frecuencias, 5 longitudes (incluidas 0/1/2),
dos tamanos de bloque. Incluye error de fuente y limites de frecuencia.

Dos MP3 del usuario, 48 kHz y 44,1 kHz stereo, se decodifican enteros por bloques
de 1 y 4096 frames y mediante cola llena a proposito. Las tres salidas deben ser
identicas, su longitud debe cumplir ceil(frames_nativos*48000/frecuencia), y
rewind debe repetir el comienzo. Prueba E/S fallida al abrir y durante lectura,
fuente vacia, y no alteracion de SHA256/tamano/mtime de los archivos originales.
Las pistas y PCM resultante quedan privados. Mono solo esta implementado, no
validado aun con una pista mono real. No es un oraculo independiente del codec
MP3 de Windows ni un benchmark. Tres unidades ARM64 y targets CMake compilados;
sin ejecucion ARM/hardware ni audio del juego arrancado.

Upstream: [minimp3](https://github.com/lieff/minimp3),
[revision fijada](https://github.com/lieff/minimp3/commit/ea99364f61c14656440e8d77e9c233ccf3124633).
