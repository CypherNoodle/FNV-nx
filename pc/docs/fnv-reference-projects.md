# Referencias del topic Fallout New Vegas

Revisado 2026-10-18 a partir del [topic aportado](https://github.com/topics/fallout-new-vegas).

* [xNVSE](https://github.com/xNVSE/NVSE): referencias de estructuras y funciones
  del motor original, utiles para futuras auditorias de callers y layouts.
  Es un extensor Windows; su loader y sus DLL no son un runtime Horizon.
  Direcciones y estructuras requieren cotejo con la identidad exacta del EXE.
* [nv-rs](https://github.com/slaterain/nv-rs): implementacion experimental en
  Rust/Bevy; publica planes de motor y de renderer propio sobre una capa D3D9.
  Puede servir para contrastar interfaces y formatos. No se incorporo su codigo
  ni se verificaron sus cifras de progreso/jugabilidad. No equivale a una ruta
  lista para Switch ni sustituye el traductor x86 de este proyecto.

Se mantiene PC AOT con Vulkan/Mesa. No se agregaron loaders de mods, Bevy ni
dependencias de esos proyectos. Primero se verifican llamadas observables del
binario propio; las referencias ayudan a formular hipotesis, no prueban ABI.

## reNut y el enfoque de los ports PC

Revisado 2026-10-21: [reNut](https://github.com/masterspike52/reNut) recompila
Banjo-Kazooie: Nuts and Bolts mediante Rexglue. Su README describe hooks D3D
nativos sobre Plume y shaders XenosRecomp compilados dentro del ejecutable.
Esa base Xbox 360 no sustituye el traductor x86/Win32 de este port PC.

Las interfaces revisadas ofrecen referencias de arquitectura concretas:

* [render_thread.h](https://github.com/masterspike52/reNut/blob/main/src/renut_engine/gpu/backend/render_thread.h):
  cola ordenada, capturas del estado/datos modificables, progreso de fences y
  limite de frames adelantados. Las reglas de vida de memoria invitada de
  Xbox deben redisenarse segun los locks y recursos D3D9 de Windows.
* [queued.h](https://github.com/masterspike52/reNut/blob/main/src/renut_engine/gpu/backend/queued.h):
  copia de vertices/indices UP antes de devolver la llamada al juego; el
  trabajador no debe leer despues un puntero temporal ya modificado.
* [frame_stats.h](https://github.com/masterspike52/reNut/blob/main/src/renut_engine/perf/frame_stats.h)
  y [gpu_profile.h](https://github.com/masterspike52/reNut/blob/main/src/renut_engine/gpu/backend/gpu_profile.h):
  frames contados al presentar, timestamps GPU recogidos tras el fence y
  medidas separadas de trabajo CPU/render y esperas.

Se documentan como referencias; no se copio su implementacion ni se agrego
Rexglue/Plume al port PC. La ruta sigue siendo EXE x86 a C/ARM64, APIs Win32
nativas y D3D9 sobre Vulkan/Mesa para Horizon. En la misma familia de AOT,
[SH2-nx](https://github.com/cutarev/sh2-nx#how-it-works) recompila x86 a C/ARM64
y reimplementa Windows, pero su D3D8 utiliza OpenGL. Compartir el enfoque no
significa compartir exactamente la pila grafica o el estado de compatibilidad.
