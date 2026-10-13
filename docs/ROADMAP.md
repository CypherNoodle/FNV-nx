# Próximos hitos

1. Superar el cierre de autoguardado y mostrar el mundo. La alpha03 llegó al
   tutorial y al creador de personaje con escena negra; la alpha04 añade seis
   entradas y una comparación de oclusión, con captura privada de shaders.
2. Validar cinemática, carga, escena jugable, controles, audio y guardado en
   una misma sesión útil. Revisar los logs ante cualquier nuevo bloqueo.
3. Censo D3D y layout XDK para un frontend directo, siguiendo las guías SA2.
   Evaluar conversión de shaders y backend deko3d con la captura de escena.
4. Perfilar el runtime y las lecturas de SD sobre la escena jugable.
5. Medir gameplay en el Cortex-A57 y GPU de
   Switch antes de adoptar optimizaciones.

Los backends de consola requieren evaluación. No se ha establecido que
deko3d, NVK o NXVK sean intercambiables ni que el renderer de otro juego funcione
directamente con New Vegas.

Referencias: [ReXGlue](https://github.com/rexglue/rexglue-sdk),
[guía de SA2-NX](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/porting-guide.md),
[NFSMW-NX](https://github.com/StevensND/nfsmw-nx),
[XenosRecomp](https://github.com/hedge-dev/XenosRecomp).

La ruta PC/x86 previa está pausada; este repositorio se centra en Xbox 360.
