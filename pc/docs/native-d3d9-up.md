# DrawPrimitiveUP y DrawIndexedPrimitiveUP

Los slots COM 83 y 84 ya reciben vertices e indices desde memoria guest
mediante callers x86 recompilados. El modulo prepara 90 metodos. La ruta
admite TRIANGLELIST, TRIANGLESTRIP y TRIANGLEFAN, indices 16/32 bits y una
declaracion de vertices que solo use stream cero. Requiere shaders del
catalogo enlazado, igual que los draws de buffers existentes.

Antes de despachar se validan rangos guest, stride, cantidades y overflow.
No se ejecutan punteros guest como codigo ni se conservan para otra llamada.
El proveedor copia los datos a versiones Vulkan HOST_VISIBLE propias;
esas versiones viven hasta el fence del draw. Comparten validacion de
geometria, descriptores, shaders, estados y envio con los draws de VB/IB.
No consumen identificadores COM ni slots de recursos publicos temporales.

Tras un intento despachado al proveedor se limpia stream cero, incluidos
offset y stride; en UP indexado tambien se limpia el indice. UP no indexado
conserva el indice previo. Los rechazos previos del bridge (por ejemplo un
puntero fuera del arena o una declaracion multistream) no despachan trabajo
ni modifican bindings. No se ha contrastado exhaustivamente el comportamiento
de todas las llamadas invalidas de Windows; este es el contrato admitido.

MinVertexIndex se usa como indice sobre el origen de vertices. El primer
proveedor copia hasta MinVertexIndex+NumVertices, incluido el prefijo; no es
una optimizacion de rangos dispersos. Rechaza stride cero, cero primitivas,
lineas/puntos y las rutas FVF/fixed-function aun no implementadas. Sigue
creando buffers y pipeline por draw; no acredita ganancias de rendimiento.

## Pruebas

El fixture vk_backend_up_fixture.h entra por los dos callers nuevos:
86 callers en la suite completa. Verifica seis dibujos (ambas llamadas,
tres topologias, indices 16/32 y MinVertexIndex distinto de cero), bindings
y datos propios, errores de allocation/map/rangos y BUSY con dos draws
pendientes. Sobrescribe la memoria guest antes de leer la GPU para demostrar
que no depende de su persistencia. Siete lecturas suman 7168 pixels; ocho
draws UP se completan, incluidos los dos simultaneos.

test_d3d9_up.py usa un dispositivo HAL D3D9 Windows independiente y confirma
stream cero nulo con offset/stride cero, indice conservado para UP y nulo
para UP indexado. Usa el pipeline fijo de Windows solo como referencia de
bindings; el proveedor Vulkan propio sigue usando shaders programables.

```sh
python pc/tests/test_d3d9_up.py --output <carpeta-nueva> --cc <compilador-C>
```

Evidencia privada: local/vk-up02/validation.json (37 draws totales,
212 submits/completions headless, 217 WSI y cuatro Present),
local/up-hal01/validation.json y local/com-up-regression01/validation.json
(90 metodos, 400 rondas concurrentes). Conserva Reset, DXT y cuatro controles
negativos previos. COM/backend/Horizon compilan ARM64 con -Werror. No hay
prueba UP en Switch ni ejecucion del motor en esta entrega.

Tambien se corrigio la consulta de filtrado lineal del draw para texturas BC:
usa el formato BC real. El fixture DXT oculta solo su capacidad lineal y
comprueba que el draw se rechace; consultar RGBA8 haria fallar ese test.

Contratos de Microsoft: [DrawPrimitiveUP](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawprimitiveup)
y [DrawIndexedPrimitiveUP](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-drawindexedprimitiveup).
