# Renderer después de la carga del tutorial

La alpha03 alcanzó audio del tutorial y el creador de personaje, según la
prueba del usuario. El mundo permaneció negro. El cierre tiene una causa
concreta distinta del renderer: llamada sin registrar a `0x82814F90` en la
ruta observada del autoguardado. La alpha04 añade ese salto y otros cinco de
grupos con límites verificados. No se registraron los 390 fragmentos encontrados
solo por su forma: muchos pueden ser finales internos de funciones.

NVK repite avisos de presión de envío nativo (`0xd5c`), retirando trabajo
anterior y reintentando. El perfil muestreado muestra aproximadamente un núcleo
ocupado por el hilo principal y esperas del hilo GPU. Los últimos intervalos
registran 3,3–3,8 frames/s con CPU 1785 MHz, GPU 921,6 MHz y memoria 1600 MHz.
El muestreo altera los tiempos; no es un benchmark pasivo. No se demostró una
causa única del mundo negro ni que sustituir NVK resuelva el coste CPU.

## Prueba inmediata

La alpha04 conserva Xenos Vulkan/FBO y desactiva las consultas nativas de
oclusión, usando el resultado visible de fallback del SDK. Es una comparación
de compatibilidad para el mundo ausente, no una corrección demostrada.
Desactiva el muestreo invasivo y conserva contadores pasivos. Captura shaders
observados en una carpeta privada de la SD.

Antes de compilar, prepara el pequeño cambio opt-in del SDK:

```powershell
python tools/prepare_horizon_shader_dump.py --sdk 'RUTA/sa2-nx/sdk'
```

El SDK ya captura microcode Xenos. El cambio añade `Translation::Dump` tras
traducir a Vulkan para obtener SPIR-V, con el mismo flag `dump_shaders`.
No cambia shaders ni draws. Los archivos derivados del juego permanecen
privados; solo cubren variantes observadas, no todo New Vegas.

## Trabajo para deko3d

[deko3d](https://github.com/devkitPro/deko3d) expone dispositivos, colas,
comandos y recursos sobre Tegra X1. Su flujo habitual usa shaders nativos
compilados en PC con UAM; no reemplaza automáticamente un driver Vulkan.
UAM está disponible localmente, pero todavía no se convirtieron ni validaron
shaders de New Vegas para deko3d.

1. Inventariar shaders y variantes de una escena real, ampliando los siete
   programas observados anteriormente en el menú Windows.
2. Probar SPIR-V a GLSL y UAM, conservando bindings, constantes, interpoladores
   y operaciones que requieran emulación Xenos.
3. Implementar recursos, formatos, depth/stencil, blending, resolves y
   presentación. Reutilizar CPU, archivos, audio y mandos ya integrados.
4. Elegir entre interceptar D3D XDK 2.0.11626.0 (offsets por verificar) y adaptar
   el command processor Xenos; comparar una escena y medir CPU/GPU por separado.

[ProjectCane](https://github.com/NaGaa95/ProjectCane) sirve de referencia de
recursos, sincronización y shaders deko3d. Su renderer no traduce automáticamente
los comandos o shaders Xenos del juego; su código no se ha integrado. La
preferencia por deko3d es una dirección de desarrollo, no una comparación de
rendimiento ya realizada con New Vegas.

## Guías de SA2 aportadas por el usuario

Se revisaron las copias locales de
[api-renderer](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/api-renderer.md),
[direct-renderer](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/direct-renderer.md),
[gpu-optimisation-candidates](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/gpu-optimisation-candidates.md),
[cpu-rounds](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/cpu-rounds.md) y
[enhancements](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/enhancements.md).

El renderer API de SA2 mantiene Vulkan y sustituye funciones D3D y transporte
PM4 por operaciones nativas, trabajando para eliminar la emulación EDRAM.
Ese frontend propio no está incorporado aquí: FNV-nx usa el plugin Xenos completo
del SDK. Cambiar el driver a deko3d no elimina por sí solo ese trabajo.

El siguiente hito es un censo D3D y verificar el layout de New Vegas, antes de
sustituir Draw, Clear, Resolve, presentación o espera. El perfil señala
`sub_8278FC10` como candidato caliente, pero su contrato de API aún no está
demostrado. Después se pueden reducir transfers, submits y reenvíos de estado
con comparación de imagen y comportamiento. Los offsets, flags y optimizaciones
matemáticas de SA2 requieren comprobación específica; no se copian a ciegas.

## Resultado de alpha04 y captura real

El usuario observó HUD sobre mundo negro, 2,3–4,4 FPS y uso GPU del 99 %.
El registro aportado no contiene un nuevo FATAL ni archivo de crash; eso no
prueba salida normal. Continúan los avisos NVK de presión de envío. Desactivar
oclusión no mostró el mundo, por lo que esa comparación no resolvió el fallo.
Los intervalos de perfil corresponden a fases distintas (algunos superan
30 FPS); no deben presentarse como rendimiento de la escena negra.

La captura contiene 65 programas Xenos únicos y 79 variantes SPIR-V (35 VS,
44 PS). Las 79 pasan `spirv-val --target-env vulkan1.2 --scalar-block-layout`.
El SDK habilita esa característica y el log la confirma. La validación sin
scalar layout falla por alineación de constantes; no es una prueba de shaders
inválidos bajo las características realmente habilitadas. La validación estática
tampoco prueba que bindings, matrices, texturas o resultados de dibujo sean correctos.

El experimento directo SPIRV-Cross/UAM produjo cero módulos nativos de las 79
variantes: el modo GLSL normal rechaza sus layouts de buffers; el modo Vulkan
emite GLSL, pero UAM rechaza `GL_EXT_scalar_block_layout`. Se conservaron los
errores y las salidas privadas. No se eliminaron extensiones ni se cambiaron
offsets para conseguir una compilación que ocultara cambios de ABI.

`tools/check_deko_shader_compat.py` reproduce esa prueba. La siguiente tarea
de conversión necesita una interfaz nativa de constantes y recursos explícita,
con verificación de offsets y comportamiento; no basta con recompilar estos
shaders automáticamente.

## Comparación visual FSI sin otro NRO

`config/switch-world-fsi.toml` cambia FBO por FSI, una ruta ya incluida en
el NRO alpha04. FSI implementa packing, blending y depth/stencil manualmente.
La comparación intenta separar problemas de render targets FBO de otros
errores de estado o shaders; no promete arreglar el mundo ni mejorar FPS.
Puede ser mucho más lenta. Se prueba una vez, conservando logs y shaders,
y se restaura el preset FBO después. La configuración se lee al reiniciar
y debe copiarse como `sd:/switch/FNV-nx/fnv-nx.toml`.
