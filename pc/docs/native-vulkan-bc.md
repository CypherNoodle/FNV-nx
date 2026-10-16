# DXT1, DXT3 y DXT5 en el proveedor Vulkan

Las llamadas CreateTexture/LockRect/UnlockRect de la capa COM ya llegan a
imagenes comprimidas Vulkan: DXT1 usa BC1_RGBA_UNORM, DXT3 BC2_UNORM y DXT5
BC3_UNORM. Los bloques se conservan comprimidos en el shadow del proveedor,
staging e imagen GPU. No se agrega un decoder CPU ni una conversion a RGBA8
para estos formatos. DXT2 y DXT4 siguen rechazados por este proveedor.

Al crear el dispositivo se consulta y habilita textureCompressionBC solo si
esta disponible. CheckDeviceFormat consulta ademas capacidades del formato
y filtrado solicitado; no anuncia BC si la feature no esta habilitada.
Un dispositivo sin BC sigue admitiendo las texturas sin compresion previas.
No hay fallback de descompresion ni soporte sRGB, cubemaps, volumenes,
autogen o render-to-texture en esta entrega.

El tamano de cada mip usa filas de bloques 4x4: ocho bytes por bloque DXT1 y
dieciseis en DXT3/5. Un mip menor que 4x4 conserva un bloque entero. Los
parches exigen offsets alineados a cuatro texeles; sus extremos pueden
coincidir con el borde del mip. La copia usa el pitch del origen y el del
shadow, sin confundir texeles con bloques. Las copias Vulkan usan dimensiones
en texeles y offsets de buffer alineados al bloque.

Se conserva el modelo de versiones completas: cada upload crea otra imagen,
sube todos los mips y mantiene la anterior hasta sus fences. Un fallo de
reserva conserva los datos anteriores y permite reintentar UnlockRect. La
cantidad de submits y allocations por upload sigue siendo una limitacion
del proveedor; esto no acredita una mejora de FPS del juego.

El readback de diagnostico fnv_vk_backend_read_texture devuelve bloques BC
compactos para DXT y RGBA8 para los formatos convertidos. No es un metodo
D3D9 expuesto al juego ni un decoder.

## Pruebas

El fixture propio vk_backend_bc_fixture.h se ejecuta dentro de
test_d3d9_vk_backend.py, mediante callers x86 recompilados:

- 30 comprobaciones de transferencia: cuatro mips de una textura 12x8 por
  formato, lectura de bloques GPU, parche parcial, fallo de reserva/reintento,
  capacidad de salida y rechazo de rectangulos/pitches incorrectos.
- Tres draws y 3072 pixeles de muestreo real, incluyendo transparencia BC1,
  alpha explicito BC2 y alpha interpolado BC3. Tolerancia maxima: un nivel.
- Control negativo que fuerza alpha BC1 a uno: falla precisamente en el
  cuadrante transparente, con actual=255 y expected=0.
- Dispositivo con la feature BC ocultada: los tres formatos se rechazan sin
  alterar el identificador de salida, y los handles se liberan.

Evidencia privada: local/vk-bc04/validation.json. La suite completa pasa
84 callers, 29 draws, 189 submits/completions headless y 194 en WSI con
cuatro Present COM; mantiene las pruebas de Reset y los controles negativos
anteriores. El backend compila ARM64 con -Werror. No hay validacion de esta
ruta DXT en Switch ni ejecucion de shaders/assets de Fallout todavia.

Referencias primarias: [formatos comprimidos Vulkan](https://docs.vulkan.org/spec/latest/appendices/compressedtex.html)
y [reglas de copia por bloques y bordes](https://docs.vulkan.org/spec/latest/chapters/copies.html).
