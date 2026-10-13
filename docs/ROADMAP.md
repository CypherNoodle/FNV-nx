# Próximos hitos

1. Superar el cierre al crear una partida en Switch. La primera prueba llegó
   al menú con imagen, sonido y controles. La alpha02 llegó a la cinemática;
   la alpha03 incluye once entradas omitidas más y diagnóstico CPU para la
   reproducción lenta y la carga tras saltarla.
2. Validar cinemática, carga, escena jugable, controles, audio y guardado en
   una misma sesión útil. Revisar los logs ante cualquier nuevo bloqueo.
3. Identificar el layout Direct3D de la revisión XDK documentada. Adaptar el
   renderer nativo y sus shaders usando la referencia Windows para comparar.
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
