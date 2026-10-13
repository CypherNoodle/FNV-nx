# FNV-nx

Proyecto experimental para portar **Fallout: New Vegas de Xbox 360 a Nintendo
Switch**, recompilando PowerPC mediante ReXGlue y usando los archivos originales
que aporte cada usuario.

## Estado actual

El banco de referencia Windows ejecuta el código Xbox recompilado y muestra
el menú original en español a 1280×720. Una prueba de 95 segundos verificó ocho
capturas diferentes. El arranque conserva una barrera experimental y la prueba
usó START/A sintéticos dentro del runtime.

**Todavía no existe un NRO de esta ruta.** No se ha validado gameplay, navegación
entre opciones ni ejecución Horizon. La integración gráfica de Switch sigue
pendiente; el backend Xenos/D3D12 actual es la referencia Windows.

## Archivos del juego

No se distribuyen ni versionan `default.xex`, `Data/`, código generado, shaders
derivados o capturas del juego. Debes aportar tu propia copia extraída de Xbox 360.
Revisión documentada: título `425307E0`, media `4AC955AA`, versión `0.0.0.3`;
SHA-256 del XEX `4bda9f98ec3c46865f78aa678016e2678bd66625f6737d41cdd17f3bab0b40ac`.

La disposición prevista para Switch es:

```
sdmc:/switch/FNV-nx/
  fnv-nx.nro
  default.xex
  Fallout.ini
  Data/
  ... resto de archivos originales de Xbox 360
```

El repositorio y la carpeta de Switch usan **FNV-nx**.
`icon.jpg` es el icono solicitado para el NRO futuro.

## Referencia Windows

Requisitos: SDK ReXGlue 0.10.0, Clang 20.1.8, CMake ≥3.25, Ninja, Python ≥3.10,
herramientas C++ de VS 2022 y Windows SDK. Usa Developer PowerShell y añade
Clang, CMake, Ninja y rexglue al PATH. `REXGLUE_SDK` identifica la instalación del SDK.

```powershell
./tools/codegen.ps1 -GameRoot 'RUTA/A/TU/JUEGO'
cmake -S . -B out/host -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="$env:REXGLUE_SDK" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/host --parallel 4
python tools/probe_host.py --game-root 'RUTA/A/TU/JUEGO' --startup-barrier --barrier-phase pre-loop --visible --test-pad --seconds 95
```

La generación se ejecuta explícitamente, evitando repetir el análisis durante
el build. Las copias de diagnóstico conservan los cuerpos originales y se
guardan en directorios ignorados. El mando sintético y la barrera están
desactivados en el proceso salvo que se soliciten en la prueba.

Consulta [el punto de partida](docs/BASELINE.md), [los próximos hitos](docs/ROADMAP.md)
y [las dependencias](THIRD_PARTY_NOTICES.md).

## Trabajo para Switch

La ruta CMake Horizon, el hook de carpetas SD y las herramientas de compilación
están en preparación. Consulta [la integración Switch](docs/SWITCH_INTEGRATION.md)
para sus dependencias y la comprobación de CPU con devkitA64. La generación de
objetos ARM no acredita el enlace ni el arranque del NRO.
