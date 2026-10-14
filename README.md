# FNV-nx

Proyecto experimental para portar **Fallout: New Vegas de Xbox 360 a Nintendo
Switch**, recompilando PowerPC mediante ReXGlue y usando los archivos originales
que aporte cada usuario.

## Estado actual

Alpha09 permite usar el Pip-Boy y salir de la casa en la prueba del usuario.
La prueba alpha11 confirma la creación a 960×540: mejora algo el interior,
pero el exterior sigue lento, con unos 1.800 draws por frame y esperas de envío
largas. Los 30 FPS del menú se observaron con OC y no acreditan 30 FPS en juego.
Alpha12 incorpora espera por avance real del ring, una prueba optativa de
agrupación de envíos, medición GPU por grupos y contador FPS propio.
Todos los ajustes de Switch se hacen en [fnv-settings.ini](config/fnv-settings.ini);
el port ya no lee fnv-nx.toml. Alpha12 sigue con una mediana de 2,9 FPS en
intervalos pesados; no demuestra una mejora útil en el exterior.
Alpha13 corrige la notificación del ring y prueba reducir el tramo del shader
que mantiene bloqueada la eDRAM, con fsi_interlock_tardio en el INI.
Compilación y shaders se verifican en host; la mejora y fidelidad requieren consola.
La prueba alpha13 sigue lenta y cambió de relojes durante la sesión, por lo que
no establece una mejora ni una regresión comparable. La notificación del ring
sigue sin coincidir. El INI actual desactiva esa opción, el interlock tardío y
las consultas GPU; funciona con el mismo NRO alpha13. FBO permanece experimental.
El usuario confirmó que pudo guardar en el exterior en la última prueba.
Su bloqueo anterior no tiene causa demostrada; caras y objetos visibles a
través de paredes siguen siendo fallos gráficos pendientes.
El aviso de guardado y las miniaturas siguen pendientes. Detalles en
[PERFORMANCE.md](docs/PERFORMANCE.md). Sigue usando NVK/FSI.

### Historial de correcciones

Alpha07 prueba una recuperación del fallo de remapeo al salir de la casa:
agrupa transiciones contiguas sin ampliar watches y emula únicamente stores
autorizados por la memoria guest cuando el remapeo falla. Añade contadores de
protección, diagnóstico de dispositivos de guardado y controles por letra de
Switch. El preset `config/switch-recovery.toml` desactiva las capturas. La caché
de Mesa se dirige a `cache/mesa`; no demuestra mejora de FPS ni resolución del
cierre hasta comprobarlo en consola. El juego sigue usando NVK/FSI.

La prueba alpha07 no registró fallos de protección; el cierre siguiente fue una
entrada de dispatch omitida (`0x82A83288`). Alpha08 registra siete adaptadores
originales delimitados del mismo grupo y añade observación del resultado de
guardado. El preset permite una única ventana de muestreo CPU de 60 segundos
al detectar gameplay de FPS bajos; después vuelve a contadores pasivos. El
aviso de almacenamiento, las miniaturas y la mejora de rendimiento siguen pendientes.

Alpha08 señaló la misma entrada omitida, `0x826207B8`, tanto en el Pip-Boy como
al salir de la casa. Alpha09 incorpora su adaptador original de 32 bytes y una
prueba de backoff en la espera GPU verificada: tras 32 consultas sin avance,
solicita una pausa de 50 microsegundos. Conserva resultados y timeout originales;
no garantiza más FPS. El preset vuelve a perfil pasivo para comparar costes.

La prueba alpha09 permitió navegar por el Pip-Boy y salir al exterior, según
el usuario, sin nuevos fatals o fallos de protección en los logs recibidos.
Para probar carga por píxel con ese mismo NRO, los presets
`config/switch-resolution-540p.toml` y `switch-resolution-720p.toml` solicitan
modos de vídeo guest de 960×540 y 1280×720. Son pruebas fijas: el juego puede
conservar targets internos o elegir otro modo. No implementan resolución dinámica.

El banco de referencia Windows ejecuta el código Xbox recompilado y muestra
el menú original en español a 1280×720. Una prueba de 95 segundos verificó ocho
capturas diferentes. El arranque conserva una barrera experimental y la prueba
usó START/A sintéticos dentro del runtime.

**Existe un primer NRO Xbox experimental**, enlazado con ReXGlue Horizon y NVK.
Pasó las comprobaciones de estructura y metadatos. En la primera prueba real
de Switch, el usuario confirmó menú, imagen, sonido y controles. Crear una
partida cerró el programa: el log y la traza identifican una llamada a
`0x82D1E938` sin registrar. Se añadieron límites explícitos para ese pequeño
salto y otros 21 del mismo patrón, verificados contra el ejecutable original.
La alpha02 superó ese fallo y reprodujo la cinemática inicial, lentamente. Al
saltarla, la carga abortó por otra función sin registrar (`0x82C17A70`). La
alpha03 incorpora once entradas originales verificadas tras revisar las
referencias estáticas de las secciones de datos. Incluye un preset de muestreo
para localizar el coste CPU; aún requiere prueba en consola y no hay gameplay
validado. La alpha03 alcanzó el tutorial y el creador de personaje, con mundo
negro y bajo rendimiento; el cierre de autoguardado señaló `0x82814F90`.
La alpha04 añade seis forwarders, una prueba de compatibilidad de oclusión y
captura de shaders. Consulta [la evaluación gráfica](docs/GRAPHICS_DECISION.md) y la
[prueba de arranque](docs/FIRST_BOOT.md).

La prueba alpha04 mostró HUD pero mantuvo el mundo negro y bajo rendimiento.
La captura real de 65 shaders Xenos y 79 variantes SPIR-V ya fue analizada:
la conversión directa a UAM falla por layouts que requieren adaptación.
Existe una comparación FSI de configuración para el mismo NRO; deko3d todavía
no está integrado y no se ha demostrado una mejora de rendimiento.

Comenzó un [prototipo deko3d separado](experimental/deko/README.md): adapta
constantes y samplers de shaders capturados, compila 69/79 variantes FBO con
UAM y dispone de un núcleo AArch64 de carga de módulos y registro de draws.
Falta conectarlo con los recursos y las llamadas del juego y validar imagen
en GPU; no hay todavía un NRO deko3d jugable.

La comparación FSI ya mostró el interior, con artefactos y rendimiento bajo.
El cierre al salir de la casa es un fallo de mapeo de memoria aún pendiente.
Se añadió una captura optativa de geometría real para alimentar el prototipo
deko3d; el preset es `config/switch-deko-capture.toml`. Ese NRO de captura sigue
renderizando con NVK y no representa una mejora de rendimiento.

La captura ya aportó ocho draws de geometría real. El proyecto experimental
compila además un NRO de replay exclusivamente deko3d, con esos vertex shaders
y color plano, separado del juego. La primera prueba en consola mostró algunas
figuras amarillas sobre fondo oscuro; no incluye texturas, la escena completa
ni gameplay. La v0.2 corrige la creación del registro y muestra fallos de inicio.

El registro v0.2 confirmó ocho draws cargados, 794 frames completados y salida
normal. La nueva [herramienta offline](experimental/shader-tools/README.md)
compila las 68 variantes alpha05 usando la ruta deferred-kill del SDK, incluidos
los 38 pixel shaders antes bloqueados. Alpha06 añade captura conjunta de
texturas CPU, geometría y descriptores. La dirección del renderer es una
[capa propia en las llamadas D3D de Xbox 360 con deko3d](docs/NATIVE_RENDERER.md).
La imagen completa, la integración con el juego y la mejora de FPS siguen pendientes.

## Archivos del juego

No se distribuyen ni versionan `default.xex`, `Data/`, código generado, shaders
derivados o capturas del juego. Debes aportar tu propia copia extraída de Xbox 360.
Revisión documentada: título `425307E0`, media `4AC955AA`, versión `0.0.0.3`;
SHA-256 del XEX `4bda9f98ec3c46865f78aa678016e2678bd66625f6737d41cdd17f3bab0b40ac`.

La disposición prevista para Switch es:

```
sdmc:/switch/FNV-nx/
  fnv-nx.nro
  fnv-settings.ini
  default.xex
  Fallout.ini
  Data/
  ... resto de archivos originales de Xbox 360
```

El repositorio y la carpeta de Switch usan **FNV-nx**.
`icon.jpg` es el icono solicitado, integrado en el NRO.

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

La ruta CMake Horizon y el empaquetado NRO ya completaron su primer build.
Consulta [la integración Switch](docs/SWITCH_INTEGRATION.md) para sus dependencias
y las comprobaciones de CPU y binario. El menú ya se probó en hardware;
el objetivo actual es mejorar el rendimiento de gameplay y el guardado.
Consulta [los ajustes del INI](docs/SETTINGS.md). No borres partidas ni cachés
para cambiar de compilación; los TOML de pruebas anteriores quedan como
referencia histórica y no se cargan en alpha12.
## Performance diagnosis (alpha10)

The 540p video-mode trial still produced mostly 1280-wide surface pitches.
The current priority is profiling calls and dependencies, with an opt-in diagnostic
build that separates draw/resolve preparation, memory work, GPU fences, ring idle,
guest polling and interrupt callbacks. See [PERFORMANCE.md](docs/PERFORMANCE.md).
These measurements do not implement dynamic resolution or establish an FPS gain.

