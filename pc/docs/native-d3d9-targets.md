# Superficies, targets y Clear

El modulo COM agrega CreateRenderTarget/CreateDepthStencilSurface,
Set/GetRenderTarget, Set/GetDepthStencilSurface, Surface9 GetDesc/GetType y
Clear. El registro completo tiene 63 tokens. La vtable Surface9 ocupa 17
posiciones y conserva identidad guest32 para IUnknown, Resource9 y Surface9.
Los handles GPU host siguen fuera de la memoria guest.

## Referencias y targets iniciales

El proveedor conserva los targets iniciales, incluido el backbuffer implicito.
GetRenderTarget/GetDepthStencilSurface devuelven una referencia publica ya
adquirida; el bridge la adopta sin llamar AddRef una segunda vez. Si el handle
ya tiene wrapper, devuelve su misma celda. Los targets observados/enlazados
conservan pins internos independientes del contador publico y del padre.

Cuando un recurso enlazado llega a cero referencias publicas, sigue vivo pero
deja de retener al dispositivo. Un getter lo resucita. El dispositivo final
suelta sus bindings. Se comprobo ese comportamiento tanto en superficies
creadas como en el backbuffer implicito de Windows. NULLREF no proporciona
ese backbuffer: esta prueba usa un dispositivo D3D9 HAL propio en una ventana
oculta de 64x64, sin abrir Fallout.

La tabla soporta cuatro slots de color y uno de profundidad. Las capacidades
efectivas y combinaciones de formato, tamano y MSAA las valida el proveedor;
no se anuncian como soportadas por el solo hecho de transportar los parametros.
Los setters solo cambian los pins tras un resultado exitoso. El proveedor
aplica tambien los efectos implicitos de SetRenderTarget, como reiniciar el
viewport. El callback de estado consulta esa misma autoridad.

El descriptor es un D3DSURFACE_DESC de ocho DWORDs, copiado a metadata propia
inmutable. Si falla su consulta o se agota la tabla, se libera la referencia
temporal adquirida por el proveedor. Los objetos se reciclan al quedar sin
referencias ni pins. La prueba exige que el contador final del dispositivo
nativo llegue a cero, incluidos los casos de capacidad agotada.

## Clear

Los rectangulos se copian desde guest a un array temporal acotado a 1.024
D3DRECT. Los rangos se comprueban antes de llamar al proveedor. Los DWORDs
de color, stencil y float Z conservan sus bits. El proveedor copia los datos
que necesite antes de retornar y conserva recursos/comandos hasta que la GPU
termine. El bridge no inventa una senal de finalizacion ni hace una lectura
GPU para cada Clear.

## Prueba y limites

```sh
python pc/tests/test_d3d9_targets.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --shaders /juego/Data/Shaders \
  --output /privado/targets-host --cc /ruta/zig cc
```

Se traducen 31 callers x86. El caso principal crea 164 superficies y realiza
dos Clear: uno completo y otro rectangular. La lectura posterior del target
D3D9 HAL compara 2.048 pixeles de color con los esperados. GetRenderTargetData
y la superficie de lectura son parte del oraculo host; todavia no son bridges
guest implementados. El Clear de profundidad/stencil devuelve exito, pero
sus valores no se leen ni comparan en esta prueba.

Tambien se verifican GetDesc, QI/GetDevice, identidad de getters, vida de
superficies public0 enlazadas, viewport reiniciado a 32x32, target0 NULL
rechazado, profundidad NULL, errores inyectados, reciclado y rollback al
agotar capacidad o fallar el descriptor. Hay un caso separado que destruye
el dispositivo con targets enlazados y otro sin proveedor de creacion.
Las regresiones de estado y texturas siguen pasando. CMake ARM64 y el enlace
parcial se verifican aparte.

Este es un Clear de GPU en Windows, no un Draw ni un frame del juego. El
resultado separa `desktop_hal_clear_verified`, `gpu_draw_executed` y
`game_frame_rendered`. No se ha dibujado con este proveedor en Switch.
El NRO pc-diag01 de hardware permanece intacto y no necesita repetirse.

GetSurfaceLevel de texturas, GetContainer, surfaces offscreen, LockRect/DC,
StretchRect, GetRenderTargetData guest, resolve de MSAA y texturas RT/depth
siguen pendientes. Present, Reset y el backend Switch siguen pendientes. Las
[escenas y draws](native-d3d9-draw.md) ya tienen su pipeline propio probado.
Crear una superficie Lockable no implica que su LockRect guest este conectado;
ese metodo conserva un diagnostico estricto de pendiente.

Referencias: [GetRenderTarget](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-getrendertarget),
[SetRenderTarget](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setrendertarget),
[Clear](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-clear)
y [recursos y Reset](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices).
