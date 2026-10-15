# Primer componente del renderer D3D9 PC

`native/d3d9_constants.c/.h` implementa bancos globales de constantes de
vertex y pixel shader, separados de cualquier backend grafico. Es codigo
propio. Todavia no esta conectado a objetos COM, al motor o a una GPU.

## Contrato y subidas

El llamador configura el numero de registros float4 que anunciara el
dispositivo: hasta 256 VS y 224 PS. Hay 16 registros int4 y 16 BOOL por
etapa. Los bancos empiezan a cero y su contenido se conserva como bits de
32 bits, sin conversion aritmetica. Se mantienen NaN, infinito, subnormales,
cero negativo y BOOL no canonicos; la interpretacion de verdad del shader
corresponde al backend. Se comparo este comportamiento con D3D9 de Windows.

Set/Get validan todo el rango antes de modificar datos. Una operacion con
cero registros permite puntero NULL; las demas exigen datos. El mutex
bloqueante protege lecturas, escrituras y el consumo de rangos sucios. La
destruccion requiere que no existan usuarios concurrentes. Este bloqueo no
implementa por si solo D3DCREATE_MULTITHREADED para el resto del dispositivo.

Un setter que recibe los mismos bits no vuelve a ensuciar el registro.
Flush agrupa registros sucios consecutivos y llama al consumidor con etapa,
tipo, primer registro, cantidad y sus datos. El consumidor debe copiarlos
a almacenamiento propio antes de aceptar el rango; no puede guardar un
puntero al banco mutable para consumirlo mas tarde.

Un rango solo se limpia cuando el consumidor lo acepta. Si este falla,
ese rango y los siguientes siguen pendientes; los anteriores aceptados
permanecen limpios. No se debe dibujar tras un flush fallido. El callback
se ejecuta bajo el mutex y no puede reentrar, lanzar excepciones ni hacer
longjmp. No equivale a esperar la GPU ni gestiona la vida de una submission.

El backend debe invalidar el banco cuando reemplaza el almacenamiento o su
layout: una asignacion nueva de ring no contiene las constantes anteriores.
Cada draw debe conservar sus datos hasta que su fence permita reutilizarlos.
Float4 puede transportarse como vec4 std140; el layout host de int/BOOL y
el mapa de registros del traductor necesitan adaptacion explicita. Este
modulo no asume que los antiguos UBO compactados de UAM usen indices D3D9.

Las constantes locales DEF/DEFI/DEFB pertenecen al shader; no deben escribir
estos bancos globales ni afectar GetShaderConstant al cambiar de shader.
State blocks, Reset y perdida del dispositivo requieren integracion futura.

## Paquetes y verificacion

`tools/shader_packages.py` lee SDP con limites de tamano, cuenta, nombres,
registros y bytecode, comprueba version/marcador final y exige consumo exacto.
Su inventario contiene metadatos; `--export-bytecode` escribe opcionalmente
copias privadas nombradas por hash, nunca usando los nombres como rutas.
Esto valida estructura, no instrucciones o semantica del shader.

La instalacion Steam local conserva 16 paquetes, 15.791 registros y 1.809
bytecodes distintos. El nuevo oraculo crea los 1.809 objetos shader en D3D9
NULLREF de Windows y comprueba que GetFunction devuelve los mismos bytes.
NULLREF no compila ni ejecuta esos shaders en la GPU de Switch; este resultado
no acredita traduccion a SPIR-V/DKSH o equivalencia visual.

La prueba del banco paso 600 actualizaciones aleatorias y 7.362 comparaciones
completas con los bancos de Windows, incluidos float con patrones especiales,
int y BOOL sin canonizar. Tambien verifica rangos invalidos, setters vacios,
escrituras repetidas, agrupacion a traves del limite de 64 bits, reintento
tras un fallo parcial, invalidacion y 1.000 actualizaciones en dos hilos.

Se reutilizo el texto conservado de un frame de menu: 24 limites de draw,
39 subidas y 976 bytes de cambios de constantes, despues de la subida inicial
de los bancos. Es una medida de datos de esa secuencia, **no una mejora de
FPS**. Los floats proceden del dump textual redondeado; no se recuperaron
bits exactos ni se reconstruyo todo el estado anterior. No se reproducen
dibujos, texturas, depth/stencil o efectos de state blocks en esta prueba.

```sh
python pc/tests/test_shader_packages.py
python pc/tools/shader_packages.py --shaders /juego/Data/Shaders --output /privado/inventario
python pc/tests/test_d3d9_constants.py --shaders /juego/Data/Shaders \
  --capture /privado/frame-capturado.txt --output /privado/constantes --cc /ruta/zig cc
```

El oraculo necesita Windows/D3D9 NULLREF y usa una ventana oculta propia.
No ejecuta Fallout ni Steam. La captura es opcional para desarrollar el
componente, pero obligatoria en este runner que verifica su secuencia.

CMake construye `fnv_d3d9_constants`. `tools/build_d3d9_constants.py`
verifica los inputs y objetos del conjunto Windows/VM aceptado, compila
este modulo con flags Switch y agrega una unidad mediante enlace parcial:
353 unidades, sin externos propios pendientes; 35 externos de plataforma
con definiciones verificadas en bibliotecas identificadas por hash.

```sh
python pc/tools/build_d3d9_constants.py \
  --base-validation /privado/windows-memory-switch/validation.json \
  --libnx /ruta/libnx --cc /ruta/aarch64-none-elf-gcc \
  --ld /ruta/aarch64-none-elf-ld --nm /ruta/aarch64-none-elf-nm \
  --output /privado/d3d9-constants-switch
```

Los datos extraidos, bytecodes, capturas y objetos permanecen privados.
No se ha modificado MojoShader ni integrado su fork historico. Las anteriores
conversiones masivas son antecedentes, no prueba del nuevo renderer.

## Siguiente integracion

Implementar registro de metodos nativos/vtables guest32 para los objetos
COM y conectar setters/getters a este banco. El registro IAT actual del CPU
solo contempla imports estaticos; no ofrece aun ese dispatch COM dinamico.
Despues conectar shaders, recursos y un draw completo al backend, compararlo
con el original y ampliar cobertura. Heap, sincronizacion, DLL y cold-entry
siguen siendo requisitos independientes del arranque. No hay un NRO nuevo
del motor PC ni una prueba HW necesaria para este componente aislado.

Referencias de contrato: [SetVertexShaderConstantF](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setvertexshaderconstantf),
[registros PS3](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0)
y [constantes locales/globales](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-vs-registers-constant-boolean).
