# Bancos COM, versiones de constantes y fence Vulkan

El nuevo modulo C `d3d9_uniform_state.c` mantiene bancos globales por etapa y
snapshots por draw. Su callback de upload tiene la firma que ya consume COM.
Conserva float/INT/BOOL, copia planes al enlazar shaders y reutiliza el
[empaquetador reflejado](native-vulkan-uniforms.md). Compila para ARM64 con
mutex de libnx. No crea un dispositivo, buffers GPU ni swapchain.

## Vida de cada version

| Estado | Operacion | Resultado |
| --- | --- | --- |
| Libre | prepare con ambos planes enlazados | Copia ambos bloques a un slot y entrega un ticket nuevo |
| Reservado | cancel antes de enviar | Libera el slot; ticket antiguo invalido |
| Reservado | commit_batch despues de QueueSubmit exitoso | Publica atomicamente todos los tickets del envio bajo su serial |
| Enviado | complete con fence real observado | Libera los slots cubiertos por ese serial |

Los bytes entregados son inmutables hasta cancelacion o completion valida.
Una subida posterior o cambio de shader modifica bancos/planes actuales,
sin tocar snapshots reservados/enviados. Al cambiar de shader se empaquetan
los registros globales persistentes; no se exige que COM los marque sucios
de nuevo. Un plan invalido deja el binding anterior intacto.

El anillo tiene capacidad fija elegida al crear el modulo. Si se llena,
prepare devuelve WASSTILLDRAWING sin esperar ni sobrescribir. Destruir con
slots ocupados tambien falla. No se recicla memoria suponiendo que EndScene,
un timeout o una vuelta del hilo significan completion.

Se soporta una cola ordenada, con serial unico y estrictamente creciente por
submission. Varias versiones de un mismo envio se publican en un solo lote;
una lista duplicada o parcialmente invalida no publica nada. El backend debe
registrar el lote antes de poner el fence a disposicion del hilo observador.
Complete requiere que ese observador haya comprobado su fence real. El modulo
no puede verificar por si solo la procedencia de un serial del caller.

Hay mutex interno para API y metadata, sin callbacks externos bajo ese lock.
El acceso a punteros entregados requiere respetar la vida del ticket; destruir
requiere propiedad exclusiva del caller. Los limites son 64 slots, 64 KiB por
etapa y los del packer. Las caps del dispositivo siguen siendo responsabilidad
del proveedor. Estos son snapshots CPU: copiar a memoria GPU, aplicar coherencia
de cache y conservar recursos Vulkan requiere tambien ownership del backend.

## Pruebas propias

`test_uniform_state.py` pasa modelo de ciclo de vida, agotamiento, tickets
obsoletos, planes copiados, rebind sin upload, fallos atomicos y 800 uploads
con 400 snapshots entre tres hilos. Sus seriales son un modelo de prueba;
no se presentan como evidencia de un fence fisico.

`test_d3d9_uniform_state.py` traduce 24 callers x86 con el SDK usado por el
port. En el ejecutable host, las vtables COM ejecutan setters, getters, bind
y draws de prueba. El callback mantiene estos bancos y reserva tres versiones
con planes VS64/PS32 del fixture anterior. Se verifica fallo/reintento de upload,
BOOL API sin normalizar, scene cerrada y anillo lleno.

El mismo proceso usa esos bloques en Vulkan: siete draws, 4.096 pixeles exactos.
Tras vkQueueSubmit exitoso publica el lote de tres tickets. Solo despues de
vkWaitForFences exitoso se llama complete; se comprueban los tres slots libres,
reciclado y rechazo de tickets anteriores. Antes de ese fence, los datos
permanecen intactos y el anillo sigue ocupado. EndScene no los libera.

El alcance es la ruta de constantes/binds COM. Los shaders son propios, los
objetos COM de shader usan D3D9 NULLREF como oracle de referencias, y geometria,
texturas, estados y targets Vulkan siguen siendo el fixture explicito. Los
draw callbacks COM registran snapshots; no es un proveedor D3D9 Vulkan completo
ni una ejecucion de Fallout. No se dibuja el mundo del juego ni se mide FPS.

Resultados finales: uniform-state-unit03 PASS, uniform-state-com03 PASS
(fence real y 4.096 pixeles), regresion COM por defecto PASS con dos shaders
propios, 240 recorridos de constantes, 400 entre hilos y modos de fallo.
CMake fnv_d3d9_uniform_state ARM64 PASS. El NRO de diagnostico permanece intacto;
este lote no requiere otra prueba en consola.

## Reproducir

Preparar primero el fixture propio de [constantes](native-vulkan-uniforms.md).
Usar directorios nuevos para todas las salidas.

```powershell
python pc/tests/test_uniform_state.py --output local/state-unit --cc /tools/zig.exe cc
python pc/tests/test_d3d9_uniform_state.py --sdk /externo/xboxrecomp --python-deps /externo/python-deps --headers /externo/vulkan-headers/include --uniform-fixture local/uniform-fixture --output local/state-com --cc /tools/zig.exe cc
cmake --build /build/arm64 --target fnv_d3d9_uniform_state
```

El runner genera solo el SDP propio y el header de planes en una carpeta
privada de desarrollo. No genera carpetas de runtime para la SD ni requiere
copiar DLLs Windows al usuario.

La siguiente conexion es reemplazar los recursos/estados explicitos del fixture
por objetos y comandos del proveedor COM real, y agregar presenter NVK en
Switch con el mismo contrato de lifetime. El arranque del motor y servicios
Windows pendientes siguen siendo requisitos separados.
