# Prueba conjunta DX9/Vulkan en Switch: pc-renderer-hw02

La revision nueva es [pc-renderer-hw03](switch-renderer-hw03.md), que agrega
render a textura con dos pasadas. Lo siguiente conserva el alcance de hw02.

Este NRO ejecuta callers x86 sinteticos recompilados a ARM64 y el proveedor
D3D9/Vulkan propio con Mesa/NVK estatico. No arranca Fallout, no incluye su
codigo ni necesita EXE, DLL, assets o carpetas adicionales en la SD.

## Uso

Copiar solo `fnv-nx.nro` a `sdmc:/switch/FNV-nx/fnv-nx.nro`, sustituyendo el
NRO anterior. Abrir desde hbmenu en modo aplicacion, por ejemplo Spaira en
ese modo. Al iniciar deja un fondo azul visible mientras ejecuta las pruebas
fuera de pantalla. Despues muestra cuatro fases de 45 frames: azul liso y
cuadrantes de intensidad roja, verde y azul. Al terminar mantiene el azul.
Pulsar + para salir una vez completado el inicio.

El log nuevo es `sdmc:/switch/FNV-nx/fnv-pc-renderer-hw02.log` y se reemplaza
en cada ejecucion. Enviar ese archivo y confirmar si aparecieron las fases
y si + regreso al menu. Un fallo puede devolver al menu antes de las fases;
el ultimo `combined BEGIN` identifica el grupo alcanzado.

## Pruebas agrupadas

Las mismas funciones de `pc/switch/renderer_probe_checks.h` se ejecutan antes
en Vulkan de escritorio mediante `test_renderer_probe_checks.py`:

- Target temporal BGRA8/D24S8 32x32 y comprobacion de Clear.
- BC1/BC2/BC3: tres mips por formato, cero submits durante Unlock, readback
  bit-exacto de bloques y draw con comprobacion de color/alpha.
- DrawPrimitiveUP y DrawIndexedPrimitiveUP, mas limpieza de stream cero.
- SetFVF/GetFVF XYZ/UV y draw mediante declaracion implicita.
- State blocks de mascara, constantes, texturas y shaders; referencias publicas
  liberadas antes de Apply y Capture/Apply que restauran el estado.
- Reset sano del dispositivo preparado, restauracion del bloque retenido y
  draw posterior con nuevos targets de 32x32.

Son nueve grupos, nueve draws y 10.240 pixels comprobados. Luego se restaura
el backbuffer 1280x720 de NWindow y se ejecuta la presentacion visual con
cuatro puntos fuente comprobados por fase. El log separa los checks pequenos
de la presentacion; ninguno captura los pixels finales del compositor.

Hay 93 callers sinteticos disponibles y un catalogo VS/PS propio. La fabrica
se obtiene del dispositivo preparado con GetDirect3D y se llama CreateDevice
para fijar su configuracion. Esta prueba no valida un arranque PE/IAT del
motor. Caps completas, FFP completo, geometria en state blocks, StretchRect,
servicios Windows, instrucciones y shaders reales del juego siguen pendientes.

El bucle espera trabajo previo para aislar fallos; no es un benchmark ni una
medida de rendimiento de Fallout. Usa watchdogs de dos segundos en esperas
GPU y no modifica clocks. + se procesa en el bucle visual, despues del inicio.

## Compilacion reproducible

`pc/tools/build_switch_renderer_probe.py` exige un validation.json aceptado
con 93 callers, nueve grupos, 10.240 pixels y cuatro Present de escritorio.
Verifica el generated.c, fixture.exe, catalogo, SPIR-V, fuentes nativas y header
compartido contra los inputs probados. Compila con -Werror, ABI Horizon ARM64,
TLS soft, sin strict-aliasing ni contraccion FP. Verifica ELF AArch64/DYN,
simbolos fuertes resueltos y segmentos NRO, y registra hashes de inputs/NRO.

Requiere SDK x86 externo, Mesa Switch fijado, devkitPro/libnx e icono local.
No publica dependencias ni binarios al repositorio. Mesa sigue fijado en
606441c90e459028a50fca352daebfc20211f890, libvulkan.a SHA256
5bd7bebe6997bc54582dea202064381a42d1351dd1bd63c3d06391f3e3b6b26c.

Validacion escritorio: local/renderer-checks03, ambos modos pasan y cuatro
Present de escritorio. Compilacion NRO: local/pc-renderer-hw02-build03.
NRO SHA256 8ed6d7cf18e4c649ba0657a178d6010c70dc8bc5d1d4521f1d93f407bb470873.
**hw02 aun requiere prueba en Switch.** La evidencia historica siguiente
corresponde exclusivamente a hw01, que no cubria los avances agrupados.

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
