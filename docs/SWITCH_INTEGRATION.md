# Integración Horizon

El CMake dispone de una ruta de compilación con devkitA64/libnx y un SDK
ReXGlue Horizon externo. El target usa Cortex-A57, TLS local-exec y mantiene
`-ffp-contract=off`. Los observadores Windows quedan desactivados en este target.
El hook de rutas busca `default.xex`, `Data/` y el resto de archivos originales
en `sdmc:/switch/FNV-nx`, junto a `fnv-nx.nro`.

## Dependencias

Referencia del SDK: [SA2-NX](https://github.com/ChanseyIsTheBest/sa2-nx), commit
`8be5b45f4e03d40c5b2b7a7270b63f515674bdc8`, subdirectorio `sdk/`.
Su base upstream es ReXGlue `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.
Las fuentes de terceros se recuperan con su herramienta `tools/fetch_thirdparty.py`.
Conservar sus licencias al preparar cualquier distribución.

El SDK gráfico debe contener `lib/libvulkan.a` y sus dependencias Horizon.
La [guía de NFSMW-NX](https://github.com/StevensND/nfsmw-nx/blob/main/mesa/README.md)
describe la construcción de Mesa/NVK. La instalación básica de devkitPro no
contiene ese SDK completo. El script detiene la configuración si falta.

```powershell
./tools/build_switch.ps1 -HorizonSdk 'RUTA/sa2-nx/sdk' -NvkSdk 'RUTA/mesa-sdk/opt/devkitpro/portlibs/switch' -DevkitPro 'RUTA/devkitPro' -GeneratedDir 'RUTA/generated/default'
```

La ruta inicial conserva el plugin Xenos del SDK como referencia para el arranque.
No acredita rendimiento ni corrección gráfica de New Vegas en Switch. El renderer
directo del juego requiere identificar sus funciones D3D, layouts y shaders.

## Compilación de CPU independiente del driver

Comprobación realizada con devkitA64 GCC 16.1.0 y la revisión XEX documentada:
232 particiones más inicialización y registro (**234 unidades**) compiladas a
objetos ARM. **72.169 aliases débiles** comprobados en la misma dirección que
sus implementaciones, sin errores. El enlace relocatable de los 234 objetos
terminó correctamente, con cero referencias nombradas `sub_*` del juego sin
resolver. También pasó la comprobación de compilación del bootstrap de la app,
incluido el hook de rutas SD. Estos resultados no verifican la semántica en
hardware, la concurrencia ni el enlace completo de los servicios del runtime.

`tools/check_switch_cpu.py` compila las particiones privadas del juego a objetos
ARM con headers Horizon. Comprueba que cada implementación tenga un alias débil
en la misma dirección, necesario para conservar hooks y llamadas originales.
No enlaza un NRO ni ejecuta el juego. Sus resultados y objetos quedan en
`local/switch-cpu/` y no se publican.

```powershell
python tools/check_switch_cpu.py --generated-dir 'RUTA/generated/default' --horizon-sdk 'RUTA/sa2-nx/sdk' --dependency-include 'RUTA/rexglue-sdk/include' --devkitpro 'RUTA/devkitPro' --jobs 4 --partial-link
```

Los headers de dependencias de la distribución host se usan únicamente en esta
prueba de compilación; sus bibliotecas Windows no se enlazan para Horizon.
El build completo necesita las fuentes de terceros del fork.
El enlace parcial combina los objetos y comprueba referencias a funciones del
juego; los servicios del SDK quedan pendientes del enlace final del runtime.

## Evaluación gráfica

La metadata del XEX de la revisión documentada identifica `D3D9`, `D3DX9`,
`XGRAPHC` y `XAUDIO2` con versión **2.0.11626.0**. La herramienta
`tools/inspect_xex_libraries.py` reproduce esta lectura sin descifrar ni exportar
código. Esta versión acota la búsqueda de funciones D3D; sus offsets de dispositivo
todavía deben verificarse en el juego antes de instalar hooks de renderizado.

[ProjectCane](https://github.com/NaGaa95/ProjectCane/blob/master/cmake/Switch.cmake)
aporta una referencia de backend deko3d: asignación de recursos, fences, swapchain,
audio libnx y compilación de shaders GLSL mediante UAM. Su backend atiende un motor
reconstruido con una interfaz gráfica propia. Aplicarlo a New Vegas exige una capa
para los shaders Xenos, estado D3D, formatos y resolves del juego.

NVK facilita reutilizar inicialmente la ruta Vulkan del SDK. deko3d sigue siendo
candidato para el renderer directo. No hay una comparación de rendimiento de
ambos backends con New Vegas en hardware que justifique declarar uno más rápido.

Todavía no se ha enlazado ni probado un NRO Xbox de FNV-nx. Primero deben completarse
las dependencias gráficas y del runtime y verificarse el arranque real.
