# FVF como declaracion de vertices

SetFVF y GetFVF, slots COM 89 y 90, ya pasan por callers x86 recompilados.
El modulo prepara 92 metodos. El convertidor propio d3d9_fvf.h genera
D3DVERTEXELEMENT9 packed little endian y stride sin dependencias Windows.

La primera cobertura admite XYZ, XYZRHW y XYZW, normal cuando es compatible,
point size, diffuse/specular y cero a ocho conjuntos de textura con una a
cuatro componentes. Rechaza formatos de blending XYZB1-5/LASTBETA, bits
reservados, NORMAL con XYZRHW y dimensiones de conjuntos no declarados.
El resultado del convertidor no cambia cuando rechaza el formato.

SetFVF crea o reutiliza una declaracion equivalente y la enlaza al proveedor.
La referencia interna del binding sostiene su vida; no deja una referencia
publica oculta al device. GetVertexDeclaration puede devolverla y conservarla
mediante AddRef. Reenlazar esa declaracion mantiene su FVF asociado; una
declaracion explicita normal tiene FVF cero. La reserva se completa antes de
cambiar bindings: si falta capacidad se conserva el estado previo.

SetFVF(0) conserva la declaracion nula inicial o instala una declaracion
vacia cuando ya habia otra. El proveedor acepta END como declaracion vacia;
el draw sigue necesitando geometria valida. Reset borra el binding igual que
con declaraciones explicitas.

Esta implementacion describe el layout. No genera un vertex shader de
pipeline fijo: el draw sigue exigiendo un VS/PS del catalogo enlazado. XYZRHW
produce POSITIONT; no se renombra como POSITION ni se aplica una transformacion
de pantalla implicita. Iluminacion, matrices, skinning y texture stages fijos
siguen pendientes. Aceptar SetFVF no acredita toda esa funcionalidad.

## Verificacion

test_d3d9_fvf.py compara el convertidor contra GetVertexDeclaration de un
dispositivo HAL D3D9 Windows independiente. Pasan 360 combinaciones, incluidos
offsets, tipos, semantic indices y END, y reenlace/GetFVF. Doce negativos
comprueban el contrato restringido del convertidor; no se presentan como
resultado diferencial de llamadas invalidas de Windows.

```sh
python pc/tests/test_d3d9_fvf.py --output <carpeta-nueva> --cc <compilador-C>
```

El fixture Vulkan pasa 43 comprobaciones de FVF: layouts, referencias,
alternancia explicita/implicita, declaracion vacia, formato rechazado y
agotamiento de capacidad sin cambiar estado. Un draw UP con XYZ/UV y stride
20 verifica 1024 pixeles en GPU con los shaders programables existentes.

Evidencia privada: local/fvf-hal01, local/vk-fvf02 y
local/com-fvf-regression01. La suite completa pasa 88 callers, 38 draws,
217 submits/completions headless y 222 WSI, con cuatro Present. Conserva UP,
DXT, Reset y los cuatro controles negativos. La regresion COM registra 92
metodos y 400 rondas concurrentes. COM/backend/Horizon compilan ARM64 con
-Werror. No hay prueba FVF en Switch ni frame del motor en esta entrega.

Referencias: [D3DFVF](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dfvf)
y [SetFVF](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setfvf).
