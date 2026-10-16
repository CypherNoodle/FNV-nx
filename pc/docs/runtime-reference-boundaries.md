# Referencias de arquitectura: limites de reutilizacion

Revisadas el 16 de octubre de 2026, manteniendo activa la recompilacion PC
estatica y D3D9 propio sobre Vulkan/NVK. Ningun codigo de estos nuevos enlaces
se ha copiado ni incorporado como runtime a esta rama en esta revision.

[SH2-nx](https://github.com/cutarev/sh2-nx) recompila x86 PC y reimplementa
servicios Windows, incluido D3D8 sobre OpenGL. [Peggle2-nx](https://github.com/ChanseyIsTheBest/peggle2-nx)
parte de Xbox 360, ReXGlue y un renderer Vulkan nativo. Compartimos la estrategia
de recompilar y sustituir interfaces; las APIs y binarios son diferentes.

[PES13-NX](https://github.com/Ibnuard/pes13_nx) documenta una ruta Wine/FEX/DXVK.
Su [registro de procedencia](https://github.com/Ibnuard/pes13_nx/blob/main/docs/FEX-PORT-PROVENANCE.md)
separa FEX upstream, runtime Wine-NX/Autorun y adaptacion Horizon. Identifica
pruebas de memoria ejecutable, ABI, excepciones, reservas y contadores, con
limites de su evidencia HW. Son referencias de plataforma; no sustituyen
nuestro proveedor D3D9 ni demuestran FPS de FNV.

[FEX](https://github.com/FEX-Emu/FEX) ejecuta x86/x86-64 sobre ARM64 Linux y
puede combinarse con Wine. Adoptar esa ruta requeriria decidir sobre runtime,
traduccion dinamica, ABI y despliegue. El usuario no ha pedido cambiar la
arquitectura actual. Los adaptadores JIT/WOW64 no se agregan automaticamente
a un ejecutable ARM64 recompilado de antemano.

Antes de trasladar una tecnica concreta, revisar sus fuentes/licencia/revision,
comprobar si el problema existe en nuestro codigo y medirlo aqui. El trabajo
D3D9 actual se describe en el [proveedor nativo](native-vulkan-backend.md).
