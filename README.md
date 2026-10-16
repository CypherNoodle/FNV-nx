# FNV-nx — rama PC nativa

El objetivo activo de `pc-port` es recompilar Fallout: New Vegas PC a ARM64
para Nintendo Switch, siguiendo el modelo de
[SH2-nx](https://github.com/cutarev/sh2-nx) y
[SH3-nx](https://github.com/cutarev/sh3-nx). Xbox queda pausado en `main`.

El codigo del juego se traducira antes de compilar el NRO. En la consola,
el port leera `FalloutNV.exe` y `Data` desde su propia carpeta y resolvera
las llamadas Windows mediante implementaciones nativas incluidas en el NRO.
No requiere una biblioteca de juegos de Autorun ni un prefijo Wine en la SD.
El backend grafico activo sera Vulkan sobre Mesa/NVK; deko3d queda pausado.

**Todavia no existe un NRO PC nativo que arranque el motor.** La biblioteca
actual carga los datos PE32 y el runtime CPU integra estado por hilo,
imports por tokens y diagnosticos para el codigo previamente traducido.
Todavia faltan las API que necesita el juego. El laboratorio
anterior llega a parte de la inicializacion CRT con codigo capturado, antes
del motor, y no acredita un arranque desde el EXE original.

Ya existe un [NRO de diagnostico PC](pc/docs/switch-diagnostic.md) para
comprobar lectura del EXE y runtime en Switch. No ejecuta la entrada del motor.

La [prueba grafica Vulkan propia](pc/docs/native-vulkan-shaders.md) ya convierte
shaders D3D9 a SPIR-V y verifica textura y profundidad con 4.096 pixeles en
la GPU del PC. Aun no es un frame de Fallout ni un renderer conectado en Switch.
El adaptador propio ya restaura centroid en pares de prueba. La auditoria de
copias temporales llega a 1.808/1.809 shaders traducibles; falta comprobar sus
pares reales, cobertura MSAA y conexion al motor.

Diseno, estado verificable y pruebas: [pc/README.md](pc/README.md).
Herramientas reutilizables y sus dependencias: [pc/tools/README.md](pc/tools/README.md).
Contrato y validacion de CPU/TLS/imports: [runtime nativo](pc/docs/native-cpu-runtime.md).
El primer [modulo Windows por hilo](pc/docs/native-windows-thread.md) agrega
ocho imports de TLS, LastError e identificadores guest; el resto de los
servicios y el arranque del motor siguen en desarrollo.
Los builds PC01–PC03 con Wine se conservan como
[experimento anterior](pc/docs/wine-experiment.md), fuera de la ruta activa.
No deben confundirse con el futuro port nativo.

No se incluyen ejecutables, assets, codigo generado del juego ni capturas.
