# Estados de dispositivo desde COM x86

Set/GetRenderState, Set/GetSamplerState, Set/GetViewport y Set/GetScissorRect
estan conectados mediante ocho bridges nuevos. El modulo completo registra
55 tokens. Los slots se contrastan con los headers D3D9 y se invocan desde
28 callers x86 traducidos por el SDK en la prueba de estados.

## Contrato del proveedor

El callback `state` recibe una clase de estado, sampler, nombre y hasta seis
DWORDs. Los estados escalares usan uno; viewport usa seis y scissor cuatro.
Los datos se copian a variables locales, nunca se pasan punteros guest al
proveedor. Los valores float y las coordenadas signed conservan sus bits,
sin conversion numerica a unsigned ni canonicalizacion de NaN o cero negativo.

El proveedor es la autoridad de los valores iniciales y actuales. No existe
un segundo banco de defaults estimados en el bridge. Debe validar estados y
capacidades, inicializar los defaults D3D9 adecuados a su dispositivo y
conservar el estado si un setter falla. Puede rechazar un estado no cubierto;
no puede decir que lo aplico sin implementarlo. Si falta el callback completo,
el bridge se detiene con diagnostico de pendiente.

Los getters solo escriben la memoria guest despues de un resultado exitoso.
Un proveedor que escribe su temporal y luego falla no altera la salida guest.
Todos los rangos se validan antes del callback; viewport ocupa 24 bytes y RECT
16 en el ABI guest. Las llamadas usan stdcall y this explicito en la pila.
Los callbacks corren bajo el mutex COM y no pueden reentrar ni conservar el
puntero temporal. La API de este callback no implementa state blocks.

## Cobertura comprobada

```sh
python pc/tests/test_d3d9_states.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --shaders /juego/Data/Shaders \
  --output /privado/states-host --cc /ruta/zig cc
```

El proveedor de prueba aplica y consulta estado en D3D9 NULLREF de Windows.
Se verifican valores iniciales y actualizados, con 461 comprobaciones
escalares y 128 de viewport/scissor. Incluye profundidad, alpha test/blend,
stencil de ambas caras, culling, color write, sRGB, bias y mascaras; samplers
de pixel 0 a 15 con address/filter/LOD/aniso/border/sRGB. Una secuencia de
256 valores de blend factor comprueba que las escrituras consecutivas no se
pierden. Se conservan tambien bits de NaN y cero negativo en estados float.

Se inyectan fallos para comprobar preservacion del estado y de la salida,
y se rechazan rangos truncados sin llamar al proveedor. Otros procesos cubren
la ausencia de proveedor, los casos COM de identidad/referencias y los fallos
estrictos anteriores. La regresion de texturas sigue pasando: 139 creaciones,
51 uploads y diez formatos/cuarenta mips. CMake ARM64 y el enlace parcial se
verifican aparte. Estos contadores no acreditan renderizado en la GPU.

Todavia faltan surfaces/targets, Clear, escenas, Draw, Present y la traduccion
de shaders/formatos hacia un proveedor Switch. El callback de Windows demuestra
el transporte de estado; no implementa por si mismo las dependencias de
profundidad, mezcla o muestreo en NVK/deko3d. Los state blocks, Reset y las
modificaciones implicitas por cambio de target tambien requieren integracion.

Referencias: [SetRenderState](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setrenderstate),
[tipos de render state](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3drenderstatetype),
[SetViewport](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setviewport)
y [viewports y clipping](https://learn.microsoft.com/en-us/windows/win32/direct3d9/viewports-and-clipping).
