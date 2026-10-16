# Texturas COM hasta Vulkan

`test_d3d9_uniform_state.py` conecta constantes, geometria y textura desde 37
callers x86 recompilados. CreateTexture, LockRect/UnlockRect y SetTexture pasan
por vtables COM guest. Los bytes ARGB8 en orden BGRA little endian se convierten
a RGBA8 para staging Vulkan. Colores asimetricos detectan intercambio R/B.

Cada draw conserva una version de textura, geometria y constantes. Tres
versiones diferentes comprueban parches parciales, fallos de upload con datos
anteriores intactos y reintento de UnlockRect. Un fallo de SetTexture conserva
el binding, comprobado con GetTexture. Despues del registro se pone a cero la
textura original. Las copias conservadas producen 4.096 pixeles exactos en
siete draws Vulkan. Los recursos GPU se destruyen despues de su fence real.

NULLREF Windows es el oracle de objetos/referencias, no el renderer de estos
pixeles. El caso usa textura 2x2, mip cero, nearest, sin sRGB y a 1x. El shader
verifica RGB y depth; su alpha viene de una constante. La conversion del alpha
se comprueba en tests CPU, no se atribuye a este readback GPU.

## Conversor reusable

`pc/native/d3d9_texture_upload.c/.h` contiene `fnv_d3d9_texture_rgba8`, sin
Windows/Vulkan, allocs ni locks. Recibe un parche de mip, pitch y longitud
del origen, y un destino RGBA8 con pitch y longitud. Valida todos los rangos
y rechaza solapamiento antes de escribir. Conserva padding y pixeles ajenos
al parche. El caller garantiza buffers validos, source inmutable y acceso
exclusivo al destino.

| D3D9 | Conversion RGBA8 |
| --- | --- |
| A8R8G8B8 (21) | BGRA a RGBA, conserva alpha |
| X8R8G8B8 (22) | BGRA a RGBA, alpha 255 |
| R5G6B5 (23) | Canales redondeados a UNORM8, alpha 255 |
| A8 (28) | RGB 255 y alpha del origen |
| L8 (50) | RGB luminancia y alpha 255 |

RGB565 tiene cuantizacion a 8 bits; no garantiza equivalencia de filtrado con
565 nativo. No hace sRGB ni premultiplicacion. DXT/BC y otros formatos devuelven
error sin tocar el destino. Es una opcion de staging para estos formatos,
no exige convertir todas las texturas del juego a RGBA8. El backend puede
elegir formatos BGRA/565 o BC nativos con pruebas de swizzle y caps propias.
La conversion ocurre al upload, no por draw.

El modulo no conoce handles, mips, DISCARD, caches GPU ni fences. El proveedor
gestiona esas propiedades. El callback de prueba convierte en un candidato
temporal y publica su copia CPU despues del upload de referencia exitoso.

## Evidencia y limites

- `texture-upload-unit01`: 3.000 casos diferenciales entre cinco formatos y
  23 rechazos; canales, alpha, parches, pitch, padding y limites.
- `texture-com-gpu02`: 37 callers, tres versiones por recurso, tres uploads
  fallidos/reintentados, 4.096 pixeles y fence Vulkan real.
- `texture-vulkan-regression01`: fixture Vulkan independiente PASS.
- `texture-com-regression01`: oracle COM PASS con 10 formatos y 40 niveles mip.
  No convierte BC a Vulkan ni dibuja GPU en ese test.
- Target CMake `fnv_d3d9_texture_upload`: AArch64 PASS con `-Werror`.

Los snapshots CPU se conservan hasta fin del test: no son un cache/anillo
general ni prueba de reciclado de texturas en vuelo. Sampler, estados, targets
y pipeline siguen explicitos. El proveedor completo y presenter NVK siguen
pendientes. No se ejecuta Fallout ni NVK/Switch. No hay nuevo NRO ni FPS del port.
La [ampliacion de estados](native-vulkan-state.md) conecta tambien sampler y
profundidad; los resultados texture-com-gpu02 anteriores conservan su alcance.

```powershell
python pc/tests/test_texture_upload.py --output local/texture-unit --cc /tools/zig.exe cc
python pc/tests/test_d3d9_uniform_state.py --sdk /externo/xboxrecomp --python-deps /externo/python-deps --headers /externo/vulkan-headers/include --uniform-fixture local/uniform-fixture --output local/texture-com --cc /tools/zig.exe cc
cmake --build /build/arm64 --target fnv_d3d9_texture_upload
```

Preparar antes el [fixture de constantes](native-vulkan-uniforms.md), usando
directorios nuevos. Carpetas de desarrollo y DLLs host no se copian a la SD.
