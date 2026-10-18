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
