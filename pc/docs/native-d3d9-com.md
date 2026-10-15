# Metodos COM desde x86 recompilado

El registro CPU y `native/d3d9_com.c/.h` permiten llamadas a vtables guest32
desde rutinas traducidas por el SDK real. La primera cobertura comprende
constantes, creacion de vertex/pixel shaders y sus interfaces de consulta.
La extension de [bindings y buffers](native-d3d9-buffers.md) agrega streams,
indices, Lock/Unlock y declaraciones; el registro completo tiene ahora 63 tokens tras agregar targets, Clear, estados y
[texturas 2D](native-d3d9-textures.md).
No hay todavia un backend grafico de Switch conectado a estos metodos.

## Registro CPU y ABI

Los imports estaticos conservan sus tokens `0xFE000000 + 4*slot`. Los metodos
nativos preparados usan un registro separado, desde `0xFE010000`, con limite
de 1.024 entradas. Los tokens nunca contienen punteros host truncados.
Registrar un lote valida todos sus handlers antes de modificar el registro
o los tokens de salida. Los indices retirados no vuelven a asignarse.

Se registran antes del primer attach. Despues no se pueden agregar o cambiar
handlers; retirarlos requiere que todos los hilos esten separados. CPU
destroy devuelve BUSY mientras queden handlers registrados. Los lookups
durante ejecucion leen tablas estables, sin un bloqueo por llamada. No es
una implementacion de LoadLibrary/GetProcAddress o descarga dinamica guest.

Los metodos COM D3D9 son stdcall x86: `this` es el primer argumento de pila,
no ECX. Las vtables contienen DWORD y las celdas guest un puntero guest de
32 bits a la tabla. Cada bridge consume los argumentos y devuelve HRESULT
o ULONG en EAX, sin modificar los registros no volatiles del caller.

`fnv_cpu_prepare_memory` permite comprobar el rango host durante setup.
`fnv_cpu_try_memory` es una comprobacion de limites sin salto por diagnostico;
no acredita permisos de pagina. El modulo tambien consulta el gestor VM
opcional para los rangos que lo intersectan. El codigo MEM generado sigue
sin ser un sistema SEH o una comprobacion de permisos por instruccion.

El proceso Windows expone una vista prestada `fnv_windows_runtime` para
preparar modulos. El proceso conserva su propiedad: usar sus attach/detach
Windows y destruir los modulos antes del proceso. El caller serializa
setup/teardown frente a attach y conserva vivos los rangos dedicados.

## Cobertura COM actual

| Interfaz | Metodos conectados |
| --- | --- |
| Dispositivo preparado | QueryInterface, AddRef, Release; CreateVertexShader/CreatePixelShader; los doce Set/Get constantes F/I/B de VS y PS; Set/GetShader, CreateVB/IB, streams, indices, frecuencias y Create/Set/GetVertexDeclaration; CreateTexture y Set/GetTexture; Set/GetRenderState, SamplerState, Viewport y ScissorRect; superficies color/depth, Set/Get targets y Clear. |
| Vertex/pixel shader | QueryInterface, AddRef, Release, GetDevice y GetFunction. |
| Vertex/index buffer | QueryInterface, AddRef, Release, GetDevice, GetType, GetDesc, Lock y Unlock. |
| Declaracion de vertices | QueryInterface, AddRef, Release, GetDevice y GetDeclaration. |
| Texture2D | QueryInterface, AddRef, Release, GetDevice, GetType, GetLevelCount, GetLevelDesc, LockRect/UnlockRect. |
| Surface9 | QueryInterface, AddRef, Release, GetDevice, GetType y GetDesc. |
| Resto de la vtable Device9 | Handler estricto de metodo pendiente; nunca devuelve exito. Incluye FVF, copias/resolves, escenas, draws y Present. |

Se crea una vtable Device9 de 119 posiciones, una de shader de cinco y otra
de buffer de catorce.
QueryInterface admite IUnknown y la interfaz concreta; conserva identidad,
incrementa la referencia al acertar y devuelve NULL/E_NOINTERFACE al fallar.
Un recurso con referencias publicas mantiene vivo a su dispositivo. Los
bindings internos tienen un contador separado, descrito en la extension.
Los objetos liberados se reciclan
y el limite se aplica a objetos vivos; un uso de puntero liberado es invalido,
incluido cuando su celda ya fue reutilizada para otro objeto.

Los setters/getters usan el banco probado de constantes; el renderer host
puede consumir sus rangos mediante flush. Esto no presenta frames ni
implementa state blocks, Reset o D3DCREATE_MULTITHREADED completo.

## Shaders y proveedor

El caller entrega un catalogo de bytecodes preparado y un proveedor de
dispositivo real. El modulo copia el catalogo y busca coincidencia completa
antes de CreateShader; no intenta encontrar END interpretando floats como
instrucciones. Un shader fuera del catalogo detiene la ejecucion con un
diagnostico. La carga o compilacion dinamica de nuevos bytecodes necesita
una ruta de validacion/traduccion adicional.

El proveedor debe crear un shader valido y devolver su identidad host opaca
solo al tener exito. Se admite compartir una identidad para el mismo shader,
con su referencia adicional; se rechaza reutilizar una identidad viva para
otro bytecode. Fallos y agotamiento de capacidad dejan la salida NULL y
liberan los recursos temporales. Los handles host no se escriben al guest.

El proveedor recibe una referencia inicial transferida al dispositivo.
Cada recurso suyo retiene al padre mientras tenga referencias publicas.
Los bindings prolongan su vida sin mantener vivo al dispositivo. AddRef/Release y
GetDevice mantienen referencias coherentes entre ambas capas. Los callbacks
se ejecutan bajo el mutex del modulo, sin reentrar, excepciones o longjmp.
El backend de prueba es D3D9 NULLREF de Windows; un backend Switch tendra
que implementar estos contratos con sus recursos y shaders traducidos.

GetFunction conserva el bytecode original. El oraculo Windows devuelve el
bytecode completo y su longitud aunque el valor de longitud de entrada sea
menor; ese valor no se usa como capacidad. El bridge comprueba el rango real
que necesita antes de escribirlo. Los permisos fuera del pool VM se limitan
al contrato del arena bootstrap, como en los otros bridges actuales.

No se exporta aun Direct3DCreate9/CreateDevice al motor. La construccion del
dispositivo es una funcion de setup host para la integracion y sus pruebas;
no anuncia un adaptador o capacidades ficticias al juego.

## Verificacion y compilacion

```sh
python pc/tests/test_d3d9_com.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --shaders /juego/Data/Shaders \
  --output /privado/com-host --cc /ruta/zig cc
```

El runner traduce veinte rutinas x86 y llama realmente a las vtables.
Los 1.809 shaders locales pasan creacion/GetFunction/liberacion mediante
los bridges y el proveedor Windows. Se comprueban 240 recorridos de
constantes con flush al proveedor y 400 entre dos hilos CPU host con pilas
y datos separados. Tambien identidad, referencias, vida del padre, alias
del proveedor, reciclado, agotamiento y fallo de creacion sin filtraciones.

Cuatro procesos verifican parada estricta ante shader fuera del catalogo,
metodo pendiente, objeto liberado y token retirado. Se comprueban registro
atomico, bloqueo de altas tras attach y rechazo de teardown prematuro.
Las regresiones CPU, TLS/TEB y memoria siguen pasando. No se ejecuto el
motor ni se dibujo con esta ruta; NULLREF no verifica shaders en la GPU.

CMake agrega `fnv_native_d3d9_com` con FNV_X86_SDK. Para integrar los objetos
Switch conservados, `tools/build_d3d9_com.py` exige el C/header CPU anterior
con los hashes de la base aceptada. Comprueba que ni los C reutilizados ni
sus headers incluyen el header CPU reemplazado. Conserva el SDK y ABI
generado, reutiliza 347 objetos y recompila siete unidades propias, incluido
CPU y los modulos Windows que incluyen su header: 354 unidades en total.
Los objetos/capturas/catalogos permanecen privados; no se produce un NRO.

```sh
python pc/tools/build_d3d9_com.py \
  --base-validation /privado/cpu-runtime-switch/revalidation.json \
  --previous-cpu-source /privado/cpu-anterior.c \
  --previous-cpu-header /privado/cpu-anterior.h \
  --platform-validation /privado/cpu-runtime-switch/platform-symbols.json \
  --analysis /privado/analisis-derivado --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --libnx /ruta/libnx \
  --cc /ruta/aarch64-none-elf-gcc --ld /ruta/aarch64-none-elf-ld \
  --nm /ruta/aarch64-none-elf-nm --objdump /ruta/aarch64-none-elf-objdump \
  --output /privado/com-switch
```

El siguiente paso grafico es conectar formatos host, escenas y draws,
con su traduccion de shaders y un draw completo. Heap, sincronizacion, DLL y
el punto de entrada original siguen pendientes para arrancar el motor.

Fuentes de contrato: [QueryInterface](https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-queryinterface%28refiid_void%29),
[GetDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexshader9-getdevice)
y [GetFunction](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexshader9-getfunction).
Los offsets/GUID se contrastan con los headers D3D9 durante la prueba Windows;
no se incorpora codigo de Wine ni DLLs.
