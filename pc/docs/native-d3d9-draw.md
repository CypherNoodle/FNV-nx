# Escenas y dibujos desde COM x86

BeginScene, EndScene, DrawPrimitive y DrawIndexedPrimitive estan conectados.
El modulo registra 67 tokens. El test completo llama a 46 rutinas x86
traducidas por el SDK para preparar shaders, declaracion, buffers, textura,
targets, estados, constantes y dibujos de un pipeline propio.

## Orden y sincronizacion

El dispositivo preparado empieza fuera de una escena. Begin/End actualizan
el estado del bridge solo si el proveedor acepta la operacion. Un Begin
anidado, End sin Begin o draw fuera de escena devuelve INVALIDCALL. Un fallo
del proveedor permite reintentar conservando el estado previo de la escena.
EndScene encola trabajo; no se trata como una senal de GPU terminada.

Antes del draw se suben los rangos sucios de los bancos VS/PS F/I/B. El callback
copia los valores a su almacenamiento propio. Si una subida falla, no se
llama al proveedor de draw: el rango fallido y los posteriores siguen sucios.
Los anteriores aceptados permanecen limpios. Si falla el draw despues de una
subida correcta, la escena sigue abierta y el estado subido permite reintentar.

El flush publico tambien adquiere el mutex COM, de modo que una subida manual
no puede intercalarse con la preparacion de un draw. Se conserva el orden de
locks COM -> constantes. Los callbacks no pueden reentrar en el modulo ni
conservar punteros temporales. Los recursos y versiones consumidos necesitan
vida protegida por fences reales del proveedor; este bridge no elimina esas
dependencias ni introduce una espera por draw.

Los streams y texturas enlazados se revisan para detectar locks guest abiertos;
un draw no consume una copia que todavia espera Unlock. El indice tambien se
revisa para DrawIndexedPrimitive. BaseVertexIndex conserva sus bits signed32.
El proveedor valida rangos, primitiva y capacidades contra sus recursos reales.
En DrawPrimitive no se usan los campos base/min/vertices del callback.

## Pipeline probado

```sh
python pc/tests/test_d3d9_draw.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --shaders /juego/Data/Shaders \
  --output /privado/draw-host --cc /ruta/zig cc
```

La prueba usa un dispositivo D3D9 HAL propio en una ventana oculta, sin abrir
el juego. Compila dos shaders HLSL propios con el d3dcompiler_47 del sistema:
VS de posicion/UV/profundidad y PS de textura multiplicada por tint. Sus
bytecodes se incluyen en el catalogo de esta prueba. Se registran los hashes
de las DLL del sistema usadas por el oraculo; esas DLL no son dependencias
que se copien a la SD ni del futuro NRO.

Se crean y enlazan mediante los bridges:

* Vertex buffer de seis vertices y stride 16; index buffer de 16 bits.
* Declaracion POSITION float2 y TEXCOORD float2.
* Textura 2x2 A8R8G8B8 con subida por LockRect/UnlockRect.
* Target color A8R8G8B8 y depth/stencil D24S8 de 32x32.
* Estados de culling, profundidad y samplers, y constantes independientes VS/PS.

Los cuatro draws verifican 4.096 pixeles exactos mediante readback del target:
rojo a profundidad 0,75, verde delante a 0,25, azul a 0,9 rechazado por depth
(permanece verde) y azul visible al desactivar depth. La textura se actualiza
antes de los draws azules. Se prueban tanto DrawPrimitive como indexed, este
ultimo con BaseVertexIndex=-1 e indices desplazados. La secuencia realiza
diez subidas de rangos de constantes; el estado sin cambios no se vuelve a subir.

Se inyectan fallos de Begin/End, subida y draw, y se comprueba que los recursos
guest bloqueados impidan enviar el draw. Los bindings de stream/indices se
conservan. El dispositivo final llega a cero referencias con recursos enlazados
sin referencias publicas. Las regresiones cubren los 1.809 shaders de catalogo,
240 recorridos de constantes, 400 entre hilos, buffers/declaraciones y targets.

El fixture sintetico se amplio a dos paginas de codigo PE para alojar estos
callers sin solapar datos ni depender de bytes fuera de la seccion declarada.
CMake ARM64 y el enlace parcial de 354 unidades se verifican aparte.

## Limites

Este resultado prueba dibujos propios en Windows HAL. No se dibuja un frame
de Fallout, no se ejecuta el motor y no se ha conectado el proveedor Switch.
El catalogo del juego utilizado por los casos COM heredados permanece privado
y no es el shader dibujado en este pipeline. No hay una medida de FPS de FNV PC.

Faltan Present, variantes UP, state blocks, FVF, surfaces de texturas, copias,
resolves y Reset. La traduccion de los bytecodes D3D9 al backend Switch y la
gestion de sus formatos, comandos y fences aun necesitan implementacion.
No se sustituye esa traduccion por el exito del compilador HLSL de Windows.

Referencias: [BeginScene](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-beginscene),
[EndScene](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-endscene)
y [vertices e indices](https://learn.microsoft.com/en-us/windows/win32/direct3d9/rendering-from-vertex-and-index-buffers).
