# Próximos hitos

1. Integrar el SDK Horizon: memoria invitada, hilos, archivos, mandos y audio.
2. Compilar un NRO que lea los archivos propios junto a fnv-nx.nro y ejecute
   código real de Xbox; registrar el último punto alcanzado.
3. Identificar la revisión XDK y el layout Direct3D del juego. Adaptar el
   renderer nativo y sus shaders usando la referencia Windows para comparar.
4. Mostrar y navegar el menú en Switch. Agrupar verificaciones de gráficos,
   entrada y servicios en una ejecución útil de hardware.
5. Cargar una partida, validar gameplay y perfilar en el Cortex-A57 y GPU de
   Switch antes de adoptar optimizaciones.

Los backends de consola requieren evaluación. No se ha establecido que
deko3d, NVK o NXVK sean intercambiables ni que el renderer de otro juego funcione
directamente con New Vegas.

Referencias: [ReXGlue](https://github.com/rexglue/rexglue-sdk),
[guía de SA2-NX](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/porting-guide.md),
[NFSMW-NX](https://github.com/StevensND/nfsmw-nx),
[XenosRecomp](https://github.com/hedge-dev/XenosRecomp).

La ruta PC/x86 previa está pausada; este repositorio se centra en Xbox 360.
