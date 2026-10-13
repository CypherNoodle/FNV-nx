# Dirección del renderer nativo

La ruta objetivo es interceptar la API gráfica de Xbox 360 usada por el juego,
mantener una interfaz de recursos/estados propia y ejecutar un backend deko3d.
El traductor PowerPC y los servicios de audio, controles y archivos continúan
en ReXGlue. La ruta NVK/Xenos actual sirve como referencia y capturador durante
la transición; el renderer nativo final no debe depender de traducir PM4 ni de
simular el framebuffer eDRAM en un fragment shader con interlock.

```text
New Vegas Xbox 360 recompilado
         -> hooks D3D del XDK del juego
         -> recursos, estados y dibujos propios
         -> deko3d / Maxwell
```

## Contratos que hay que verificar

El juego usa el XDK 2.0.11626.0. Antes de sustituir funciones hay que identificar
sus entradas, parámetros y layout de objetos en esta revisión: crear/destruir
recursos, Lock/Unlock, constantes/shaders, streams/índices/texturas, estados,
Draw, Clear, Resolve, Present y fences/queries. La API de Xbox 360 no es la
interfaz COM D3D9 de Windows. Puede haber variantes o código inline/LTCG.
Los offsets y hooks de otros juegos no demuestran estos contratos.

Cada Set actualiza el estado CPU y sus marcas de cambios. Draw emite solo los
grupos modificados: rasterizer, blend, depth/stencil, buffers y descriptores.
Deko evita un PSO monolítico, pero enums, alpha test, máscaras, gamma, MSAA,
orientación, profundidad y coherencia necesitan traducción explícita.

## Recursos y shaders

- Los 256 vec4 de constantes por etapa son 4 KiB. El ABI debe conservar slots,
  valores y alineación; nuestros shaders adaptados usan mapas de constantes
  compactos y SystemConstants aparte. No basta con subir cualquier UBO de 4 KiB.
- Reutilizar la traducción Xenos a SPIR-V del SDK, adaptar el ABI y generar
  GLSL/UAM/DKSH offline. La ruta deferred-kill del SDK evita demote sin adelantar
  el rechazo a través del flujo guest que necesita derivadas.
- Usar atributos cuando un formato vfetch pueda expresarse fielmente; conservar
  storage buffers para formatos o accesos no expresables. El replay actual
  mantiene vertex pulling y sirve como referencia al cambiar esa entrada.
- Convertir tiling y endian antes de subir texturas. Conservar mips, swizzles,
  signedness, gamma y sampler. BC/DXT pueden conservar compresión cuando el
  formato y la vista nativa corresponden. CTX1 necesita conversión compatible;
  no tiene una equivalencia directa automática con BC1.
- Reemplazar eDRAM por targets host con identidad/lifetime y conservar el
  contenido requerido por resolves/copias, aliasing, MSAA y reinterpretaciones.
  Clear/Resolve no se sustituyen por operaciones vacías.
- Registrar transiciones y visibilidad con barriers y fences de deko3d. Evitar
  esperas por draw; el replay aislado sí espera por frame para diagnosticarlo.

Los shaders derivados y los assets del usuario pueden seguir como archivos
junto al NRO en `sdmc:/switch/FNV-nx/`. RomFS es una opción de empaquetado, no
un requisito. Ningún recurso privado del juego se añade al repositorio.

## Evidencia y siguientes pasos

El replay nativo cargó ocho draws, completó 794 frames y salió normalmente.
El usuario observó geometría plana parcial; no valida una escena del juego.
La herramienta offline ahora compila las 68 variantes de esa captura, incluidos
los pixel shaders. Aún no están conectados con texturas y estados completos.

Alpha06 añade captura conjunta de geometría, texturas CPU, descriptores,
formatos y layouts de mips para alimentar el backend. No es el nuevo renderer:
conserva NVK/FSI durante la captura. Los datos de texturas escritas por GPU
pueden requerir readback; un snapshot CPU no prueba su coherencia.

Tras verificar esos recursos, validar materiales/targets y el censo de API,
conectar los hooks al backend por grupos manteniendo una referencia visual.
El cierre de mapeo 0xCE01 al salir de la casa sigue pendiente; esta arquitectura
no demuestra por sí misma que lo corrija ni garantiza FPS concretos.

Referencias:
[deko3d](https://github.com/devkitPro/deko3d),
[SA2 API renderer](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/api-renderer.md),
[UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp).
[FlatOut 2](https://github.com/antoxa2584x/flatout2-sw/blob/main/README.md) usa el
Xbox original y NV2A/Vulkan; sus cachés, perfiles y sincronización sirven como
referencia, pero no comparte los contratos D3D/Xenos de Xbox 360 ni usa deko3d.
