# Ajustes de Switch

Desde alpha12, copiar [config/fnv-settings.ini](../config/fnv-settings.ini) como
sdmc:/switch/FNV-nx/fnv-settings.ini. Es el único archivo de ajustes del port.
Reiniciar después de editar. No se lee fnv-nx.toml, aunque siga en la SD.
El Fallout.ini original pertenece a los archivos del juego y se conserva.

El INI admite comentarios ; y #, UTF-8 con o sin BOM, y finales de línea
Windows. Requiere valores exactos: booleanos true/false, versión 1.
Los valores inválidos, desconocidos o duplicados se registran y se ignoran.
Una versión incompatible rechaza el archivo completo. Máximo 16 KiB.
Sin INI se mantienen los valores compilados. No hay lectura de ajustes por frame.

| Sección / clave | Valores | Efecto |
| --- | --- | --- |
| graficos / resolucion | 960x540, 1280x720 | Tamaño fijo de la creación auditada. No DRS ni reducción de todos los targets. |
| graficos / vsync | auto, off, on, relaxed | Preferencia de presentación Vulkan host. El modo efectivo depende del soporte y queda en el log. |
| graficos / renderer | fsi, fbo | FSI es la ruta que mostró el mundo. FBO tuvo mundo negro; es experimental. |
| optimizacion / espera_gpu_notificada | true/false | Espera hasta 250 µs por avance publicado del ring. El poll y su timeout originales siguen decidiendo. |
| optimizacion / backoff_gpu | true/false | Pausa de 50 µs si la notificación no reconoce la dirección. Se puede restaurar la espera alpha11 desactivando la notificación. |
| optimizacion / agrupar_envios_gpu | true/false | Prueba optativa: no enviar al final de cada primary buffer. Enviar antes de una espera de comandos, al presentar y en los puntos originales de sincronización/readback. |
| optimizacion / fsi_interlock_tardio | true/false | Alpha13: sombrear antes del interlock en shaders opacos compatibles. Conserva pruebas/escrituras de profundidad y color. Experimental: puede reducir contención o empeorar overdraw. |
| cpu / principal, comandos_gpu, audio | auto, 0, 1, 2 | Afinidad explícita para esos hilos. Auto conserva la del runtime. Solo núcleos permitidos por Horizon, sin cambiar prioridades. |
| pantalla / mostrar_fps | true/false | Texto propio sobre la salida final, actualizado con swaps del juego. Sin ImGui ni un envío adicional. |
| diagnostico / tiempos_cpu | true/false | Tiempos inclusivos de calls, esperas, memoria, I/O y procesamiento de primitivas/índices. |
| diagnostico / tiempos_gpu | true/false | Muestra uno de cada 16 envíos; grupos de draws, resolves y salida. Solo queries ya completadas, sin WAIT. |
| diagnostico / traza_guardado | true/false | Entradas/retornos del guardado y muestreo acotado si se detiene. |
| diagnostico / stacks_cpu | true/false | Muestreo de stacks que pausa hilos. Desactivado normalmente porque altera el rendimiento. |

VSync aquí controla presentación, no el contador emulado de vblank de Xbox.
relaxed es FIFO relajado de presentación; no elimina barriers o fences.
agrupar_envios_gpu tampoco omite trabajo gráfico, no fabrica completions y no
cambia la vigencia de recursos. Puede cambiar latencia y presión de cola, y
su mejora de FPS/fidelidad todavía necesita hardware.

El contador FPS usa los mismos swaps guest que el perfil. No representa Hz del
panel ni cuenta repintados del menú de depuración. El dibujo agrega pequeños
clears de rectángulos al pase host abierto; puede desactivarse para medir su coste.
No hace falta SaltyNX para ver este contador. SaltyNX/Status Monitor siguen
siendo útiles para relojes, carga GPU, RAM y temperaturas.

Dejar los tres valores CPU en auto inicialmente. Fijar todos al mismo núcleo
puede empeorar audio, streaming y GPU Commands. El log registra las afinidades
solicitadas y el resultado de Horizon. No se habilita el núcleo reservado ni OC.

Prueba conjunta: cargar una partida dentro de la casa, recorrer el exterior
durante 60–90 segundos, abrir Pip-Boy y guardar una vez. Conservar caché y
relojes de la sesión anterior, comprobar imagen/audio y enviar logs/.
Si se compara agrupación, editar solo agrupar_envios_gpu y repetir el mismo
tramo; no cambiar a la vez resolución, VSync, afinidad o relojes.
Los timestamps introducen dependencias de medición: para una comparación de
FPS sin ese coste, poner tiempos_gpu=false en ambos casos.

Alpha13 corrige la dirección física del ring E/F que impedía usar las
notificaciones en alpha12. fsi_interlock_tardio=true viene en el INI de prueba;
sin INI el valor compilado es false. Excluye discard, exportación de profundidad
o memoria, stencil y cobertura dependiente de alfa. No supone que las caras,
las miniaturas o los objetos visibles a través de paredes estén corregidos.
Para comparar, mantener la misma partida/cámara/relojes y cambiar solo esa
clave a false, reiniciar y conservar los logs de ambas ejecuciones. Un cambio
de versión de shaders puede compilar variantes al principio: medir después.
