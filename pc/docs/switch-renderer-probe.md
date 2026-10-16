# Prueba conjunta DX9/Vulkan en Switch: pc-renderer-hw01

Esta prueba ejecuta callers x86 propios recompilados a ARM64, sus llamadas COM
DX9 y el proveedor Vulkan con Mesa/NVK estatico y superficie NWindow. No carga
Fallout, no incluye codigo del juego y no necesita FalloutNV.exe ni DLL externas.
Su objetivo es contrastar en hardware la ruta que paso las pruebas de escritorio.
No mide el rendimiento de Fallout ni demuestra que DX9 este completo.

## Uso

Copiar unicamente el NRO a `sdmc:/switch/FNV-nx/fnv-nx.nro` y abrir desde hbmenu
en modo aplicacion (por ejemplo, Spaira en ese modo). No hacen falta carpetas
de Wine, assets del juego ni archivos de shader externos para este diagnostico.
El ejecutable integra un shader VS/PS propio y una textura sintetica 2x2.

Se ejecutan automaticamente cuatro fases de 45 frames: fondo azul y cuatro
cuadrantes de distinta intensidad en rojo, verde y azul. Al terminar mantiene el
patron azul. Pulsar + para salir. La primera compilacion del pipeline puede
demorar; no se atribuye ese tiempo al rendimiento del motor.

El resultado se escribe en `sdmc:/switch/FNV-nx/fnv-pc-renderer-hw01.log`.
Si falla la inicializacion o un chequeo, puede volver al menu sin mostrar imagen;
el log identifica el ultimo paso/error. Enviar ese archivo y confirmar si se
vieron las cuatro fases y si + regreso normalmente. No necesita captura de
shaders ni guardar una partida.

## Cobertura

- Device Vulkan/NWindow 1280x720 BGRA8 y D24S8, FIFO, dos slots de comandos.
- 82 callers sinteticos traducidos disponibles; el probe usa el subconjunto
  necesario de setters DX9, recursos, Clear, DrawIndexedPrimitive y Present.
- Shader catalog propio, enlace VS/PS, declaracion FLOAT2 posicion/UV, vertex
  buffer e indices con offset y base firmado, textura 2x2 y UBO float/INT/BOOL.
- Lectura GPU de cuatro puntos por fase, con color/intensidad esperados; registra
  diferencias en el log. Esto valida el target fuente, no captura el compositor.
- Present COM de backbuffer completo por copia GPU y swapchain NVK, retencion
  hasta fences, contadores y salida normal. Diagnostico de fallos del runtime CPU.

El bucle espera trabajo previo al comenzar cada frame para aislar errores de
contenido/sincronizacion. Es una prueba de correccion, no un benchmark de frames
en vuelo ni evidencia de optimizacion. Cada espera usa un watchdog de dos segundos;
un timeout deja el device perdido y se registra como fallo. No modifica clocks.

No cubre Reset, caps completas, state blocks, FVF, draws UP, StretchRect,
formatos comprimidos, shaders o instrucciones del juego, servicios Windows,
heap/hilos/SEH completos, ni el punto de entrada del EXE.

## Compilacion reproducible

Herramienta propia: `pc/tools/build_switch_renderer_probe.py`. Requiere un
`validation.json` aceptado del test COM/Vulkan con 82 callers y cuatro presents
reales, los generated.c/fixture.exe con SHA coincidente y su catalog.bin, el par
SPIR-V exacto registrado en esa prueba, un SDK x86 externo, headers/archivo Mesa
Switch y devkitPro/libnx. No necesita el AOT de Fallout del diagnostico pc-diag01.

Compila todas las unidades con ABI Horizon ARM64, TLS soft, -Werror, sin
strict-aliasing ni contraccion FP. Enlaza Vulkan estatico, verifica ELF AArch64/DYN,
ausencia de simbolos fuertes sin resolver y segmentos NRO. Registra hashes de
fuentes, SDK headers, shaders, compilador, archivos estaticos y NRO. No publica
los binarios ni las dependencias al repositorio.

Mesa local fijado en 606441c90e459028a50fca352daebfc20211f890, archivo libvulkan.a
SHA256 5bd7bebe6997bc54582dea202064381a42d1351dd1bd63c3d06391f3e3b6b26c.
La compilacion no confirma su funcionamiento en consola: hace falta este log HW.

## Primera validacion en hardware: PASS

El log recibido del usuario para pc-renderer-hw01 termina con PASS y
destroy=00000000: cuatro fases, 16 puntos leidos de GPU sin errores, 3114
llamadas traducidas y 545 presentaciones aceptadas. La ventana de 180 frames
registra 135 draws, tres imagenes y modo FIFO. Los dos submits aun pendientes
en esa ventana no son un error: al terminar la destruccion se completo normalmente.

El log se conserva privado en local/hardware/pc-renderer-hw01 junto a su
validation.json. SHA256 del log:
7245918bf634adacea09d518e983cf2642f476b1c84d85c8f809ff3da5d8d290.
NRO probado: 43b45ddee213b1014f75d74b0ac53a1d8d83e66e39abb2bd768dd2469cf30509.

Esto valida el subconjunto sintetico DX9/COM/Vulkan/NVK descrito, no el juego,
todo DX9, A8 (el probe usa ARGB8), ni la fidelidad del compositor por captura.
El log no proporciona un benchmark del motor ni una confirmacion visual del
usuario; los chequeos de imagen son los puntos fuente leidos por GPU.
