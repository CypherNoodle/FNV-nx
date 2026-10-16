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
Tambien pasa la [subida nativa de constantes float/INT/BOOL](pc/docs/native-vulkan-uniforms.md)
con planes reflejados y dibujo Vulkan propio; el modulo ya compila para ARM64.
La [prueba COM de constantes](pc/docs/native-vulkan-uniform-state.md) ya pasa
por callers x86 recompilados y conserva tres versiones hasta un fence Vulkan
real. La [geometria desde COM](pc/docs/native-vulkan-geometry.md) tambien pasa
por uploads VB/IB y validacion de rangos antes del dibujo Vulkan. Aun faltan
el gestor general de recursos, estados y presenter del proveedor completo.
La [textura desde COM](pc/docs/native-vulkan-texture-upload.md) tambien llega
a Vulkan con versiones conservadas, parches y reintentos de upload. Es un
fixture de integracion; aun no es el renderer del motor.
La [profundidad y samplers](pc/docs/native-vulkan-state.md) ya proceden de
estados COM en esa prueba, con UV fuera de rango y un control negativo que
detecta el sampler incorrecto. Faltan otros estados y el proveedor general.
El [proveedor Vulkan propio](pc/docs/native-vulkan-backend.md) ya crea dispositivo,
superficies, buffers de vertices/indices, texturas 2D, modulos shader, declaraciones y Clear desde COM sin un dispositivo D3D9 Windows, con dos envios
en vuelo y retencion por fences. Ya ejecuta draws propios texturizados; aun no hay frames del juego.
La [primera ruta WSI](pc/docs/native-vulkan-presentation.md)
ya presenta frames sinteticos en Vulkan Windows y compila el enlace NWindow ARM64;
Present COM tambien pasa con callers x86 traducidos. La [prueba conjunta para Switch](pc/docs/switch-renderer-probe.md)
ya paso en hardware para su subconjunto sintetico: cuatro fases sin errores de
pixels comprobados y 545 presentaciones. Aun no arranca Fallout. La [fabrica DX9 inicial](pc/docs/native-d3d9-factory.md) ya
entrega el device preparado mediante Direct3DCreate9/CreateDevice; caps completas
y recreacion siguen pendientes.

El [Reset inicial](pc/docs/native-d3d9-reset.md) ya reinicia el dispositivo
sano de tamano fijo, conserva recursos MANAGED y limpia constantes y bindings.
Pasa pruebas Vulkan de escritorio y compila para ARM64; faltan recuperacion
de superficie perdida, cambio de resolucion y su validacion en consola.

Las [texturas DXT1/3/5](pc/docs/native-vulkan-bc.md) ya se suben comprimidas
como BC1/2/3 y se muestrean en Vulkan. Pasan pruebas GPU de escritorio de
color, alpha, mipmaps y parches; falta comprobar esta ampliacion en Switch.

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
