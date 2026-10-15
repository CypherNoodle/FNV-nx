# Bindings y buffers D3D9 desde x86

El modulo COM conecta Set/GetVertexShader, Set/GetPixelShader, vertex/index
buffers, streams, indices y frecuencias de streams. La extension de declaraciones suma ahora
40 tokens nativos.
Las llamadas proceden de callers x86 traducidos por el SDK. El proveedor de
prueba es D3D9 NULLREF de Windows; todavia no hay un draw ni backend Switch
conectado. Compilar este modulo a ARM64 no demuestra renderizado del juego.

## Identidad y referencias

Las vtables guest usan celdas de 32 bits. Los handles del proveedor permanecen
en el host. Hay una tabla Device9 de 119 slots, una de shader de cinco, una
de buffer de catorce y otra de declaracion de cinco. Los objetos empiezan en el offset 1024 del rango dedicado.
El limite `resource_capacity` cuenta shaders, buffers y declaraciones vivos
conjuntamente. Las declaraciones no implementan IDirect3DResource9.

Los bindings mantienen un recurso vivo mediante pins internos, separados de
su contador COM publico. Al llegar el contador publico a cero se libera la
referencia al dispositivo; el recurso enlazado sigue existiendo. GetStreamSource,
GetIndices o GetShader vuelven a adquirir una referencia publica y al padre.
Al destruir el dispositivo se sueltan todos los bindings. Esta separacion,
contrastada con Windows, evita un ciclo dispositivo-recurso-dispositivo.

El proveedor debe respetar ese contrato, incluido AddRef desde cero publico
en un recurso enlazado. El dispositivo inicial tiene bindings nulos y
frecuencias de stream iguales a uno. Los setters solo actualizan el estado
guest si el proveedor acepta la operacion; un fallo conserva el estado previo.
Los callbacks se ejecutan bajo el mutex del modulo, sin reentrar.

## Memoria y Lock/Unlock

El caller entrega un arena de sombra precommitted, alineado a 16 bytes y
separado del rango de objetos, imagen, pilas y otros datos. Debe conservarlo
vivo y escribible hasta destruir el modulo. Retener el gestor VM impide
destruirlo entero, pero no impide que un caller incorrecto libere sus paginas.

Un allocator first-fit divide el arena y fusiona rangos contiguos al liberar.
No hay llamadas al kernel para reservar o reproteger paginas por buffer o Lock.
Esto reduce operaciones de gestion de memoria; no es una mejora de FPS medida.
Cada buffer tiene su copia CPU y su objeto host, con presupuestos separados.

CreateVertexBuffer/CreateIndexBuffer, GetType, GetDesc y QueryInterface de
Resource9/VB/IB estan conectados. Los indices admiten INDEX16 e INDEX32.
Lock devuelve una direccion guest dentro de la copia CPU. Un size cero cubre
el resto del buffer; los rangos fuera de limites se rechazan. Unlock copia el
rango al proveedor y solo termina el lock tras aceptar la subida. Si falla,
el mismo Unlock se puede reintentar conservando los bytes pendientes.

READONLY no sube datos. DISCARD invalida el buffer completo y NOOVERWRITE
conserva la promesa de no pisar datos consumidos por draws pendientes. Ambos
requieren DYNAMIC. El proveedor futuro necesita versiones y fences reales:
copiar a memoria host no prueba que la GPU haya acabado. Los datos GPU sin
escribir inicialmente, o fuera del rango escrito tras DISCARD, son indefinidos.
La sombra CPU se inicializa a cero, pero no se sube todo el buffer al crearlo.

Esta primera ruta sirve para buffers producidos por CPU. ProcessVertices y
lecturas de buffers modificados por GPU requieren sincronizacion adicional.
Los locks anidados, aunque D3D9 los contempla, se detienen como pendientes.
Los shared handles, private data, prioridad y PreLoad tambien estan pendientes.
No se simula su exito. Dibujar, texturas, targets,
state blocks, Reset y Present aun necesitan implementacion.

## Declaraciones de vertices

CreateVertexDeclaration, Set/GetVertexDeclaration y GetDeclaration estan
conectados. El bridge copia como maximo 64 elementos mas el terminador;
cada D3DVERTEXELEMENT9 ocupa ocho bytes. Comprueba el rango guest de cada
lectura y rechaza listas sin terminador dentro del limite. El proveedor
valida tipos, usos, metodos y capacidades antes de aceptar la declaracion.
No se anuncia soporte GPU de un formato por haber conservado sus bytes.

GetDeclaration devuelve todos los elementos, incluido END; pNumElements es
salida, no capacidad de entrada. Se comprueba el rango completo antes de
copiar. El test utiliza una salida fisica suficiente con contador inicial
cero, y la contrasta contra Windows. La consulta sin array devuelve el numero.
La copia permanece propia hasta liberar el objeto; los bindings usan los
mismos pins separados de referencias publicas que los otros recursos.

SetFVF/GetFVF, conversion de formatos para el backend GPU y validacion de los
atributos consumidos por un draw siguen pendientes. No se ha conectado la
compilacion de shaders ni dibujado los buffers en Switch.

## Verificacion reproducible

```sh
python pc/tests/test_d3d9_geometry.py --sdk /ruta/xboxrecomp \
  --python-deps /ruta/dependencias --shaders /juego/Data/Shaders \
  --vertices /captura/vertices.bin --indices /captura/indices.bin \
  --output /privado/geometry-host --cc /ruta/zig cc
```

El fixture recibe vertices de stride 20 e indices de 16 bits acotados. Los
datos capturados permanecen privados. El test escribe esos bytes en la
direccion guest devuelta por Lock; los callers COM de Lock/Unlock y bindings
si pasan por el codigo x86 traducido. Se compara la lectura del buffer D3D9
nativo con los bytes originales, sin afirmar que se haya dibujado una imagen.

La ejecucion aceptada tradujo 40 callers, creo 263 buffers y realizo tres
subidas y diez bindings en el caso principal. Comparo 80 bytes de vertices y
12 de indices conservados. Verifico descriptores, identidad, resurreccion de
referencias, dos streams compartidos, fallos de binding/subida, readonly,
reciclado, fragmentacion, fusion de rangos y recuperacion del arena completo.
Seis casos comprueban parada estricta, incluido Draw aun pendiente y lock
anidado. Tambien ejecuta los casos de vida del padre, alias y capacidad del
fixture COM anterior. La extension compara 17 tipos de atributo contra Windows y realiza 529
creaciones de declaraciones, con consultas byte a byte, bindings, identidad,
fallos de creacion/binding, reciclado, clase de objeto incorrecta y listas
sin terminar/fuera del arena. Incluye destruccion del padre con una declaracion
enlazada y sin referencias publicas. No prueba todos los layouts posibles.
La regresion general de 1.809 shaders, 240 recorridos de constantes y 400
entre hilos tambien paso mediante `test_d3d9_com.py`.

CMake ARM64 y `build_d3d9_com.py` pasan: 347 objetos reutilizados verificados
y siete unidades propias recompiladas, 354 en el enlace parcial. Los 36
externos de plataforma tienen definiciones en las bibliotecas identificadas;
se mantiene TLS soft de libnx. No es un enlace final del motor ni un NRO.

Fuentes del contrato: [Lock de vertex buffers](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexbuffer9-lock),
[flags de Lock](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dlock),
[SetStreamSource](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setstreamsource)
[SetIndices](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setindices),
[CreateVertexDeclaration](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createvertexdeclaration)
y [GetDeclaration](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexdeclaration9-getdeclaration).
