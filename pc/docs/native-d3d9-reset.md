# Reset del dispositivo preparado

Los slots COM 16 (Reset) y 3 (TestCooperativeLevel) ya pasan por callers x86
recompilados y el proveedor Vulkan propio. El modulo prepara 88 metodos.
Esta primera ruta reinicia un dispositivo sano con su configuracion fija.
No recupera una swapchain perdida ni cambia resolucion, formato, MSAA o
modo de presentacion.

Exige el hilo creador, parametros admitidos, ausencia de escena abierta y
liberacion de superficies publicas y recursos DEFAULT. Tambien rechaza locks
abiertos: es una restriccion conservadora del port, no una afirmacion sobre
todos los locks MANAGED de Windows. Un fallo de parametros o recursos deja
TestCooperativeLevel en DEVICENOTRESET; un reintento valido lo restablece.
Un timeout GPU deja el proveedor en DEVICELOST. Los otros metodos aun no
aplican completamente las restricciones del estado perdido de D3D9.

Reserva targets y bancos de constantes nuevos antes de cambiar bindings.
Si falla una reserva, conserva los targets anteriores y limpia las reservas
candidatas. Espera fences reales con plazo finito antes de soltar referencias
internas. Conserva la swapchain WSI existente. El reset borra bindings y
constantes y restaura estados iniciales; los objetos MANAGED, shaders y
declaraciones con referencias publicas sobreviven. Los targets nuevos reciben
identificadores nuevos.

## Verificacion

`test_d3d9_vk_backend.py` traduce 84 callers. Headless y WSI pasan 28
comprobaciones de Reset y cuatro resets correctos por proceso. Cubre
backbuffer retenido, buffer DEFAULT, parametros invalidos, hilo incorrecto,
fallos de imagen/view/memoria, constantes y bindings, datos MANAGED y timeout.
Los 26 draws posteriores y los controles negativos de sampler, culling y
blending siguen pasando. Headless completa 144 submits; WSI, 149 con cuatro
Present COM. Se liberan todos los handles contabilizados.

`test_d3d9_reset.py` usa un device HAL Windows independiente como referencia:
constantes cero, culling inicial, stream vacio, 16 bytes MANAGED conservados y
dos fallos/reintentos con DEVICENOTRESET. No usa el backend propio como oraculo.
Windows devolvio parametros normalizados 32x32, un backbuffer XRGB8; el perfil
propio usa ARGB8. No se exige esa salida particular entre todos los drivers.

```sh
python pc/tests/test_d3d9_reset.py --output <carpeta-nueva> --cc <compilador-C>
```

Evidencia local: `local/vk-reset-com02`, `local/reset-hal02`,
`local/com-reset-regression01` (88 metodos, 400 rondas concurrentes).
COM/backend/Horizon compilan para ARM64 con -Werror. Reset aun no se ha
ejecutado en Switch; pc-renderer-hw01 es anterior y acredita su subconjunto.
No se arranco el motor de Fallout.

Referencia: [IDirect3DDevice9::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset).
