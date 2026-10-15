# Renderer PC y optimizacion para Switch

Revision del 15 de octubre de 2026. Xbox sigue pausado; estas decisiones son
para el port nativo PC. Todavia no hay frames del motor PC en Switch que
permitan afirmar una mejora de FPS.

## Referencias revisadas

* [Peggle 2, plan](https://github.com/ChanseyIsTheBest/peggle2-nx/blob/a6fb9374c90a8ac2ef7bc5e92cccfa2c20e5eb40/docs/plan.md)
  y [notas](https://github.com/ChanseyIsTheBest/peggle2-nx/blob/a6fb9374c90a8ac2ef7bc5e92cccfa2c20e5eb40/docs/peggle2-notes.md).
* [SA2, guia de ports](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/porting-guide.md),
  [renderer de API](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/api-renderer.md)
  y [candidatos GPU](https://github.com/ChanseyIsTheBest/sa2-nx/blob/main/docs/gpu-optimisation-candidates.md).
* [Futari, notas](https://github.com/ChanseyIsTheBest/futari-nx/blob/main/docs/futari-notes.md),
  especialmente el coste de logs en SD, las cargas de memoria y el audio.

Estos ports usan PowerPC y D3D del XDK. Reutilizamos el metodo de medida y
validacion; sus direcciones, registros, shaders Xenos y reglas de eDRAM no
describen el ejecutable x86 de FNV PC. Los wrappers Android del mismo autor
parten de bibliotecas ARM ya existentes; tampoco reemplazan esta recompilacion.

## Interfaz grafica

El punto de entrada PC sera la API D3D9: Direct3DCreate9 y las interfaces
COM del dispositivo, recursos y swapchain. Las operaciones se traduciran a
estado host y recursos host. Esta es una adaptacion de la estrategia de API
nativa descrita por SA2/Peggle, no una reutilizacion de sus offsets XDK.

No introducir un procesador de comandos Xenos o eDRAM virtual en esta ruta:
el programa PC no los requiere. Los render targets seran imagenes host con
identidad, subrecursos y dependencias conocidas. Clears, StretchRect, resolves
de MSAA y lecturas CPU conservaran su semantica D3D9.

Mantener el backend separado de esa API. NVK es una primera opcion por la
infraestructura Switch conservada; deko3d sigue siendo una opcion de backend.
La seleccion definitiva requiere los mismos draws, shaders y escena visibles
en ambos y medidas de CPU/GPU. No atribuir el coste de la antigua emulacion
Xenos al coste de Vulkan del futuro renderer PC.

La instalacion Steam revisada contiene 16 archivos `Data/Shaders/shaderpackage*.sdp`.
Son candidatos para el inventario offline de shaders PC; aun no se ha validado
su parser ni se ha traducido su contenido. La ruta de shaders XenosRecomp de
los ports Xbox no convierte automaticamente estos shaders D3D9.

## Peggle: que se puede trasladar a D3D9 PC

Se revisaron tambien las fuentes del mismo commit: [draws de API](https://github.com/ChanseyIsTheBest/peggle2-nx/blob/a6fb9374c90a8ac2ef7bc5e92cccfa2c20e5eb40/app/src/me/peggle2_api_draw.cpp),
[hooks](https://github.com/ChanseyIsTheBest/peggle2-nx/blob/a6fb9374c90a8ac2ef7bc5e92cccfa2c20e5eb40/app/src/me/peggle2_me_hooks.cpp)
y [targets nativos](https://github.com/ChanseyIsTheBest/peggle2-nx/blob/a6fb9374c90a8ac2ef7bc5e92cccfa2c20e5eb40/app/src/me/masseffect/masseffect_native_targets.h).
La ruta es hibrida: algunos draws y estados producen registros nativos,
mientras otras operaciones siguen usando el ring PM4. Su validacion compara
las operaciones originales con las sustituidas antes de ampliar la cobertura.
No demuestra que todo el procesador de comandos haya desaparecido.

Para PC, las interfaces COM proporcionan un limite explicito sin identificar
las funciones internas D3D del XDK. El plan de implementacion es:

| Contrato D3D9 guest | Implementacion propia y comprobacion |
| --- | --- |
| Direct3DCreate9, CreateDevice y vtables | Objetos guest32 con tokens y bridges stdcall/thiscall segun cada contrato. Nunca truncar punteros host a 32 bits; verificar QueryInterface, AddRef y Release. |
| Texturas, surfaces, vertex/index buffers | Tabla de identidad, formato, subrecurso y vida; Lock/Unlock con pitch y regiones explicitas. Validar bytes subidos y referencias antes de reutilizar memoria. |
| SetRenderState, samplers, shaders y constantes | Estado D3D9 persistente con rangos sucios. Comparar el estado consumido por cada draw; no asumir que el orden de setters permite omitir una dependencia. |
| DrawPrimitive, DrawIndexedPrimitive y variantes UP | Registro de draw completo, indices, declaracion de vertices, shaders y bindings; UP copia los datos necesarios antes de retornar. Comparar primero un draw real con textura y profundidad. |
| Targets, Clear, StretchRect y Present | Imagenes host con subrecursos y dependencias; conservar viewport, scissor, depth/stencil, alpha y multisampling. Medir tiempos sin bloquear para obtener contadores. |
| Bytecode de shaders D3D9 | Inventario y traduccion propios de VS/PS PC, con identidad por contenido y opciones. XenosRecomp no es el traductor de ese bytecode. |

Primero registrar una secuencia D3D9 del original PC y reproducirla en el
backend; despues conectar los mismos bridges al CPU recompilado. Comparar
operaciones, datos y frames alineados, incluidos primera/tercera persona y
Pip-Boy. Un replay correcto acredita esa secuencia, no un arranque del motor.
Las operaciones pendientes deben detenerse con diagnostico, sin devolver
objetos o resultados de exito ficticios.

Esta tabla es un plan verificable: estos objetos COM y el renderer D3D9 PC
todavia no estan implementados. No se han trasladado codigo ni cifras de
rendimiento de Peggle a FNV.

## Medidas desde el primer frame

Registrar por ventana, con contadores en memoria y escritura agrupada:

| Componente | Datos requeridos |
| --- | --- |
| CPU | ms activos por hilo y por frame; espera, bloqueo, trabajo; nucleos autorizados y afinidad efectiva |
| GPU | timestamps no bloqueantes por pase, draws, clears, copias, resolves, barreras y volumen transferido |
| Presentacion | tiempo de Present, submit, fence, intervalo elegido y frames en vuelo |
| Recursos | memoria reservada/comprometida, picos, texturas/buffers, reasignaciones y datos subidos |
| SD | latencia y bytes por lectura, tiempo de descompresion por separado, misses de cache y coste del log |
| Audio | ocupacion del buffer, underruns, tiempo de mezcla/decodificacion y esperas |

Los timestamps se recuperan cuando la submission correspondiente termina;
no esperar la GPU solo para medirla. Identificar version/hash del driver
enlazado, ajustes efectivos, escena, guardado y relojes de la consola.

## Orden de optimizacion

1. Imagen correcta: profundidad, stencil, alpha, clipping y render targets;
   las escenas de primera persona, tercera persona y Pip-Boy deben compararse.
2. Estado y datos: rangos sucios de constantes, estado redundante, arenas
   reutilizables de vertices/indices/constantes con vida protegida por fences.
3. Pasadas: agrupar clears/copias compatibles y mantener el pase abierto
   cuando sea legal; eliminar trabajo solo si se demuestra que no se consume.
4. Sincronizacion: esperas por eventos con reevaluacion de la condicion;
   sincronizacion de productor/consumidor y coherencia de cache explicitas.
   No sustituir una espera de GPU real por un aviso falso de finalizacion.
5. Driver: barreras con dependencias correctas, caches de pipelines invalidadas
   por identidad de driver/shader/configuracion y vinculo a la libreria exacta.
6. CPU: perfilar el ARM64, optimizar grupos calientes y verificar registros,
   flags y memoria. LTO/PGO solo despues de disponer de ejecuciones representativas.
7. Resolucion dinamica: solo con tiempo GPU de escena separado del trabajo
   fijo; ajustar recursos de escena y conservar la UI legible, con limites e
   histeresis. No esperar que bajar pixeles resuelva una espera CPU.

Los perfiles de Peggle y SA2 ayudan a elegir que medir; sus costes y FPS no
son predicciones para FNV. En particular, la proporcion de tiempo dedicada a
resolves/eDRAM de SA2 no se extrapola a una escena PC de Gamebryo.

## Validacion

Cada cambio tendra un ajuste INI cuando pueda conmutarse sin romper recursos.
Agrupar cambios independientes en una compilacion, pero medirlos por separado
en el mismo guardado y recorrido. Comparar frames alineados y estado/logica,
con excepciones delimitadas para animaciones dependientes del reloj; no declarar
identidad por una media que oculta caras, HUD o transparencias incorrectas.

Conservar eventos/atomicos y cargas de polling necesarias. La opcion de hacer
registros locales requiere auditar callbacks, fragmentos y excepciones; los
ajustes PowerPC de LR/CR/XER no se aplican al x86 sin traducir su significado.
No activar fast-math, flush de denormales o vsync relajado como arreglos genericos.
El reloj de la logica, el de presentacion y el del audio necesitan pruebas propias.

Las notas de Peggle documentan una mala compilacion de ciertos permutes NEON
con early-ra de GCC 16.1.0. Es una alerta para pruebas de SIMD y ensamblado,
no una explicacion comprobada de las texturas de FNV. Las comprobaciones ARM64
de esta etapa usan `-mearly-ra=none`, `-fno-strict-aliasing` y `-ffp-contract=off`;
los flags finales se verificaran en los comandos reales de todos los targets.

## Revision de Prototype 2

Se revisaron el arbol completo y los tres documentos de desarrollo/instalacion
en el commit `e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f`, ademas de configuracion
y fuentes seleccionadas. [Build](https://github.com/penettrator/prototype2-nx/blob/e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f/docs/BUILD.ru.md)
y [cuarto nucleo](https://github.com/penettrator/prototype2-nx/blob/e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f/docs/FOURTH_CORE.ru.md)
describen una ruta Xbox 360/ReXGlue; no es un recompilador de EXE Windows.

La [configuracion publicada](https://github.com/penettrator/prototype2-nx/blob/e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f/config/prototype2.toml)
activa renderer nativo Vulkan, grabacion de comandos en otro hilo y precarga
de pipelines; presenta 640x352 mediante FSR. El README interno del renderer
aun describe FSI como predeterminado: para identificar la ruta usar la
configuracion efectiva y el log, no solo ese README. Estos ajustes no
acreditan una mejora de FNV ni prueban que necesite deko3d.

| Idea observada | Aplicacion a FNV PC |
| --- | --- |
| Atribucion de esperas GPU/WSI por hilo y alcance anidado | Medir tiempo inclusivo y exclusivo. La latencia desde una consulta pendiente hasta completarla no equivale a CPU bloqueada; no sumar alcances anidados como trabajo independiente. |
| Llamadas directas con una lista protegida de overrides | Antes de inlining/LTO, conservar bridges, hooks, callbacks y dispatch publico; comprobar simbolos y hashes del dispatch despues de transformar. |
| Identidad de copias de buffers dinamicos por contenido/epoca | Un buffer reescrito requiere invalidacion por escritura y vida protegida por fence. Comprobarlo una vez por frame no demuestra que no cambie entre dos draws. No introducir muestreo aproximado como prueba de identidad. |
| Afinidad y helper de skinning | Consultar permisos efectivos del proceso; intersectar mascaras solicitadas con nucleos permitidos. Core3 puede compartir servicios del sistema. Un INI no concede permisos adicionales. |
| Perfil de release con diagnosticos desactivados | Separar mediciones de desarrollo y distribucion; mantener contadores ligeros y evitar capturas/logs SD durante benchmarks. |

Referencias de codigo: [esperas](https://github.com/penettrator/prototype2-nx/blob/e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f/project/src/native/gpu_wait_diagnostics.cpp),
[llamadas directas](https://github.com/penettrator/prototype2-nx/blob/e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f/tools/prepare_direct_calls.py),
[copias dinamicas](https://github.com/penettrator/prototype2-nx/blob/e1a496bc3bb161fa9331dbecbf3ae93c3a58a35f/project/src/native/dynamic_buffer_stamp.h).

El perfil tambien reduce densidad/LOD, desactiva efectos y habilita skinning
aproximado. Esas concesiones visuales y sus hooks son especificos de Prototype;
no copiar sus valores ni atribuir todo el resultado a la API grafica. FSR se
evalua despues de medir la escena de FNV y separar su UI; no soluciona un
cuello de botella CPU. No se ha integrado codigo de ese renderer en PC.

## ByroRedux como referencia de contenido

Revision del commit `f44dacbdad0d88f4b6e56e3e64ed8fa63efcdf2d`:
[README](https://github.com/matiaszanolli/ByroRedux/blob/f44dacbdad0d88f4b6e56e3e64ed8fa63efcdf2d/README.md),
secciones de su [matriz](https://github.com/matiaszanolli/ByroRedux/blob/f44dacbdad0d88f4b6e56e3e64ed8fa63efcdf2d/docs/feature-matrix.md),
NIFAL y las interfaces de BSA/NIF/materiales. Es una reimplementacion en Rust
que consume datos Bethesda; no es un traductor x86, bridge Win32 o backend
D3D9 disponible para enchufar al ejecutable. No se ha compilado ni medido aqui.

Su lector BSA contempla v104 de FO3/FNV; las interfaces NIF y la traduccion de
materiales sirven como referencias offline para diagnosticar estructuras,
paletas de huesos, flags y canales de texturas. El renderer PC de esta ruta
recibe los recursos y estados D3D9 del motor original: no necesita reemplazar
su lector NIF para arrancar. Una herramienta basada en esos parsers seria
una validacion independiente del contenido, no una sustitucion del juego.

Su renderer anunciado usa Vulkan 1.3 y ray queries. Eso exige otro estudio
de capacidades/coste para Switch; no copiar su renderer de escritorio ni sus
cifras de una RTX como prediccion de Maxwell. La matriz distingue lectura de
SCPT de ejecucion de scripts y conserva gaps de gameplay. Que cargue una celda
de New Vegas no demuestra que reemplace fielmente toda su campana. Tampoco
intercambiar FNV ObScript con Papyrus de juegos posteriores.

No se importaron fuentes ni se cambio la arquitectura activa por este repo.
