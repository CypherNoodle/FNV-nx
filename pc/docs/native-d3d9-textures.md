# Texturas 2D en el bridge D3D9 PC

La capa COM agrega CreateTexture, GetType, GetLevelCount, GetLevelDesc,
LockRect/UnlockRect y Set/GetTexture para los dieciseis samplers de pixel.
El modulo prepara 47 tokens nativos y una vtable Texture9 de 22 posiciones.
QueryInterface admite Texture9, BaseTexture9, Resource9 e IUnknown con la
misma identidad guest32. GetSurfaceLevel y otras interfaces aun son pendientes.

## Datos y vida

Los formatos cubiertos son A8R8G8B8, X8R8G8B8, R5G6B5, A8, L8 y DXT1-5.
Los formatos fuera de esa lista se detienen con diagnostico de pendiente;
no se anuncia soporte universal. El proveedor valida usage, pool y capacidades
antes de aceptar un objeto. La implementacion limita dimensiones a 16384 y
conserva como maximo quince niveles. Levels cero crea la cadena completa.

La sombra CPU comparte el arena precommitted y reciclable de buffers. Guarda
los niveles consecutivos; el pitch de cada nivel se alinea a cuatro bytes.
El pitch es un valor de esta implementacion, no una prediccion del pitch del
driver Windows o de la GPU. Los uploads copian filas usando ambos pitches.
En DXT, una fila corresponde a bloques de 4x4, de ocho bytes en DXT1 o
dieciseis en DXT2-5. Incluso un mip de 1x1 ocupa un bloque completo.
No se descomprime ni se cambia endian para esta ruta PC.

LockRect devuelve el D3DLOCKED_RECT guest de ocho bytes: pitch y direccion
guest32. Los rectangulos usan coordenadas del nivel y se validan antes de
modificar el estado. Para DXT se respetan los limites de bloque y los bordes
del mip. UnlockRect sube solo la region bloqueada; READONLY no sube datos.
Un fallo mantiene el lock y los bytes pendientes para reintentar la subida.

DISCARD requiere una textura dinamica, nivel cero y ausencia de subrectangulo.
Invalida todos los mips y no puede pisar otro nivel bloqueado. El proveedor
debe crear o reciclar una version completa y proteger su vida mediante fences
reales. La sombra inicial a cero no inicializa la imagen GPU: sus datos sin
escribir, y los niveles no escritos tras DISCARD, siguen indefinidos.

Los bindings conservan pins internos separados de referencias publicas.
Liberar el ultimo contador publico suelta al dispositivo, pero mantiene una
textura enlazada viva; GetTexture vuelve a adquirir referencia publica y al
padre. El dispositivo final libera sus bindings. Fallos de binding conservan
la textura anterior. La vida se contrasta con D3D9 NULLREF de Windows.

## Limites actuales

Render targets, depth textures, autogeneracion de mipmaps y shared handles
siguen pendientes. Tambien cubemaps/volumes, samplers de vertices, AddDirtyRect,
NO_DIRTY_UPDATE, dirty tracking para UpdateTexture, surfaces y lecturas GPU.
Locks anidados se detienen con diagnostico. DEFAULT sin DYNAMIC no admite
LockRect en esta ruta. No se simulan transferencias ni compilacion de shaders.

El proveedor de prueba copia bytes a una textura real D3D9 NULLREF. El
proveedor Switch, estados de sampler/render, targets y draws aun necesitan
integracion. No hay una imagen PC renderizada ni una mejora de FPS medida.
El NRO pc-diag01 entregado para pruebas de runtime conserva su build original;
estas pruebas de texturas no requieren reemplazarlo.

## Verificacion

```sh
python pc/tests/test_d3d9_textures.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --shaders /juego/Data/Shaders \
  --output /privado/textures-host --cc /ruta/zig cc
```

El runner traduce 28 callers x86 con el SDK real. El caso principal crea
139 texturas, realiza 51 subidas y compara diez formatos y cuarenta mips
byte a byte con D3D9 de Windows. Se comprueban actualizaciones parciales,
preservacion tras subida fallida, retry, readonly, DISCARD dinamico, rectangulos
invalidos, nivel fuera de rango, bindings, referencias, reciclado y fallo de
creacion. Procesos separados verifican parada estricta ante formato no
implementado y lock anidado, y destruccion con textura enlazada.

Las regresiones conservan 263 buffers, 529 declaraciones/17 tipos de atributo,
1.809 shaders, 240 recorridos de constantes y 400 entre dos hilos. CMake ARM64
y el enlace parcial conservan 354 unidades de entrada; estos resultados no
sustituyen un draw en GPU. Los patrones de pixeles son sinteticos propios.
Los paquetes de shaders usados por el fixture permanecen privados.

Contratos: [LockRect](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dtexture9-lockrect),
[memoria de superficies](https://learn.microsoft.com/en-us/windows/win32/direct3d9/accessing-surface-memory-directly),
[texturas dinamicas](https://learn.microsoft.com/en-us/windows/win32/direct3d9/performance-optimizations)
y [mips DDS comprimidos](https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dds-file-layout-for-textures).
