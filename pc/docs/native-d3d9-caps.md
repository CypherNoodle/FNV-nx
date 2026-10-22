# Capacidades limitadas de D3D9 sobre Vulkan

fnv_vk_backend_device_caps produce una imagen x86 D3DCAPS9 de304 bytes. La
fabrica y el dispositivo COM publican el resultado por el callback kind1,
conservando la salida ante un fallo. Adapter0/HAL; REF y adaptadores inexistentes
siguen sin estar disponibles. No es una implementacion completa/conforme de
D3D9, ni una declaracion de soporte general de shaders.

Los limites de textura2D, indice, streams y stride se acotan con las propiedades
reales del VkPhysicalDevice. MaxStreams nunca supera16. El indice se limita
ademas a24 bits porque no habilitamos fullDrawIndexUint32. Solo se anuncia un
color target, anisotropia1 y un presupuesto conservador de65534 primitivas.
El rango de repeticion de coordenadas anunciado es1; no se anuncia el rango
fisico completo sin verificarlo. MaxVertexW/point size se fijan a1. Las
capacidades de filtrado lineal dependen de las propiedades del formato RGBA8.
La presentacion solo se anuncia si hay WSI y para el modo preparado.

Culling, mascara de color, blend op, separate alpha, factores implementados,
scissor, comparaciones de profundidad (con auto depth) y mipmaps2D corresponden
a operaciones existentes. Border arbitrario no se anuncia: nuestro sampler
solo admite tres colores de borde. Tampoco se anuncian stencil, cube/volume,
MRT, sRGB, luces, hardware T&L fijo, clip planes, instancing ni fixed function.
Un shader Vulkan preparado para un caso sintetico no acredita esas funciones.

VertexShaderVersion, PixelShaderVersion y los limites de instrucciones/constantes
de shader quedan en0. El catalogo offline puede aceptar shaders concretos sin
que eso implique cobertura general de un Shader Model. La futura ampliacion
necesita validar traduccion, declaraciones/semanticas, enlaces VS/PS, UBO,
samplers y pixeles. Nunca se copian las caps del driver Windows del PC.

Pruebas privadas vigentes: local/vulkan-device-caps02/validation.json PASS,
94callers x86 traducidos,70checks de fabrica,46draws sinteticos y251 envios
completados. Cotejo de limites con una enumeracion Vulkan independiente;
validacion de bits con las constantes/estructura del header Windows;
fabrica/dispositivo, adapter, tipoREF, salidaNULL, tamano incorrecto, canarios y
rollback de proveedor fallido. Conserva los controles negativos de pixeles y
las inyecciones de fallos existentes. Compilacion ARM64/libnx con ABI x87
extendido y Werror PASS; no es enlace de NRO ni nueva ejecucion en consola.

El arranque capturado ya supera GetDeviceCaps HAL y consulta formatos de color
y profundidad. Aun termina en la peticion al launcher, detenida sin ejecutar
ShellExecute. No hay un frame del juego y no se ha demostrado arranque frio.

Auditoria adicional de shaders Steam
----------------------------------

La lectura estructural de los SDP instalados encontro15791 registros,
1809 bytecodes unicos. Incluyen VS1.1/2.0/3.0, PS2.1 y PS3.0. Una auditoria con
el adaptador propio y traductor externo congelados acepta1808 y rechaza1.
El rechazo es PARTICLE.vso: cinco referencias relativas no encajan en el rango
de constantes descrito por su CTAB. No se altera el bytecode original ni se
amplian rangos de metadatos sin verificar la semantica.

Esta pasada normaliza centroid solo temporalmente para evaluar el parser:
965 shaders contienen esa declaracion; la auditoria no la restaura en un modulo
enlazado. Aceptacion NO significa SPIR-V enlazado/validado, catalogo listo,
pares VS/PS correctos ni pixeles del juego. Informes y bytecodes quedan privados.

Repetir la auditoria con shader_packages.py --export-bytecode y
 audit_spirv_catalog.py --centroid sobre un manifest de SHA256; outputs privados
nuevos. Mantener la identidad del binario linker y su build.json comprobada.
Las herramientas propias estan en pc/tools; no se publican assets ni generacion.

Referencias primarias:
[D3DCAPS9](https://learn.microsoft.com/en-us/windows/win32/api/d3d9caps/ns-d3d9caps-d3dcaps9),
[GetDeviceCaps](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-getdevicecaps).
