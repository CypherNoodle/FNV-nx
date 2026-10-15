# FNV-nx — rama PC nativa

El objetivo activo de `pc-port` es recompilar Fallout: New Vegas PC a ARM64
para Nintendo Switch, siguiendo el modelo de
[SH2-nx](https://github.com/cutarev/sh2-nx) y
[SH3-nx](https://github.com/cutarev/sh3-nx). Xbox queda pausado en `main`.

El codigo del juego se traducira antes de compilar el NRO. En la consola,
el port leera `FalloutNV.exe` y `Data` desde su propia carpeta y resolvera
las llamadas Windows mediante implementaciones nativas incluidas en el NRO.
No requiere una biblioteca de juegos de Autorun ni un prefijo Wine en la SD.

**Todavia no existe un NRO PC nativo que arranque el motor.** La biblioteca
actual carga los datos PE32 y permite enlazar imports con tokens nativos;
no ejecuta x86 ni implementa las API que necesita el juego. El laboratorio
anterior llega a parte de la inicializacion CRT con codigo capturado, antes
del motor, y no acredita un arranque desde el EXE original.

Diseno, estado verificable y pruebas: [pc/README.md](pc/README.md).
Herramientas reutilizables y sus dependencias: [pc/tools/README.md](pc/tools/README.md).
Los builds PC01–PC03 con Wine se conservan como
[experimento anterior](pc/docs/wine-experiment.md), fuera de la ruta activa.
No deben confundirse con el futuro port nativo.

No se incluyen ejecutables, assets, codigo generado del juego ni capturas.
