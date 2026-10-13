# Integración Horizon

El CMake dispone de una ruta de compilación con devkitA64/libnx y un SDK
ReXGlue Horizon externo. El target usa Cortex-A57, TLS local-exec y mantiene
`-ffp-contract=off`. Los observadores Windows quedan desactivados en este target.
El hook de rutas busca `default.xex`, `Data/` y el resto de archivos originales
en `sdmc:/switch/FNV-nx`, junto a `fnv-nx.nro`.

## Dependencias

Referencia del SDK: [SA2-NX](https://github.com/ChanseyIsTheBest/sa2-nx), commit
`8be5b45f4e03d40c5b2b7a7270b63f515674bdc8`, subdirectorio `sdk/`.
Su base upstream es ReXGlue `c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.
Las fuentes de terceros se recuperan con su herramienta `tools/fetch_thirdparty.py`.
Conservar sus licencias al preparar cualquier distribución.

El SDK gráfico debe contener `lib/libvulkan.a` y sus dependencias Horizon.
La [guía de NFSMW-NX](https://github.com/StevensND/nfsmw-nx/blob/main/mesa/README.md)
describe la construcción de Mesa/NVK. La instalación básica de devkitPro no
contiene ese SDK completo. El script detiene la configuración si falta.

El primer build completo usa Mesa 26.2.4 de `danfromtico/mesa-switch`, commit
`606441c90e459028a50fca352daebfc20211f890`, con adaptación local de rutas MSYS2.
Se construyeron `mesa_clc`, `vtn_bindgen2` y el driver Vulkan para Horizon.
La biblioteca `libvulkan.a` contiene 1.533 objetos ELF AArch64, sin objetos host.
SHA-256 del archive comprobado:
`5bd7bebe6997bc54582dea202064381a42d1351dd1bd63c3d06391f3e3b6b26c`.
No se aplicaron las optimizaciones específicas de NFSMW.

```powershell
./tools/build_switch.ps1 -HorizonSdk 'RUTA/sa2-nx/sdk' -NvkSdk 'RUTA/mesa-sdk/opt/devkitpro/portlibs/switch' -DevkitPro 'RUTA/devkitPro' -GeneratedDir 'RUTA/generated/default'
```

La ruta inicial conserva el plugin Xenos del SDK como referencia para el arranque.
No acredita rendimiento ni corrección gráfica de New Vegas en Switch. El renderer
directo del juego requiere identificar sus funciones D3D, layouts y shaders.
`-GuestOptimization 1` se usó para este primer build. El script permite 0–3,
con 2 por defecto. El empaquetado copia el nombre de target que usa el SDK al
nombre final solicitado, `fnv-nx.nro`.

## Compilación de CPU independiente del driver

Comprobación realizada con devkitA64 GCC 16.1.0 y la revisión XEX documentada:
232 particiones más inicialización y registro (**234 unidades**) compiladas a
objetos ARM. **72.169 aliases débiles** comprobados en la misma dirección que
sus implementaciones, sin errores. El enlace relocatable de los 234 objetos
terminó correctamente, con cero referencias nombradas `sub_*` del juego sin
resolver. También pasó la comprobación de compilación del bootstrap de la app,
incluido el hook de rutas SD. Estos resultados no verifican la semántica en
hardware, la concurrencia ni el enlace completo de los servicios del runtime.

`tools/check_switch_cpu.py` compila las particiones privadas del juego a objetos
ARM con headers Horizon. Comprueba que cada implementación tenga un alias débil
en la misma dirección, necesario para conservar hooks y llamadas originales.
No enlaza un NRO ni ejecuta el juego. Sus resultados y objetos quedan en
`local/switch-cpu/` y no se publican.

```powershell
python tools/check_switch_cpu.py --generated-dir 'RUTA/generated/default' --horizon-sdk 'RUTA/sa2-nx/sdk' --dependency-include 'RUTA/rexglue-sdk/include' --devkitpro 'RUTA/devkitPro' --jobs 4 --partial-link
```

Los headers de dependencias de la distribución host se usan únicamente en esta
prueba de compilación; sus bibliotecas Windows no se enlazan para Horizon.
El build completo necesita las fuentes de terceros del fork.
El enlace parcial combina los objetos y comprueba referencias a funciones del
juego; los servicios del SDK quedan pendientes del enlace final del runtime.

## Evaluación gráfica

La metadata del XEX de la revisión documentada identifica `D3D9`, `D3DX9`,
`XGRAPHC` y `XAUDIO2` con versión **2.0.11626.0**. La herramienta
`tools/inspect_xex_libraries.py` reproduce esta lectura sin descifrar ni exportar
código. Esta versión acota la búsqueda de funciones D3D; sus offsets de dispositivo
todavía deben verificarse en el juego antes de instalar hooks de renderizado.

[ProjectCane](https://github.com/NaGaa95/ProjectCane/blob/master/cmake/Switch.cmake)
aporta una referencia de backend deko3d: asignación de recursos, fences, swapchain,
audio libnx y compilación de shaders GLSL mediante UAM. Su backend atiende un motor
reconstruido con una interfaz gráfica propia. Aplicarlo a New Vegas exige una capa
para los shaders Xenos, estado D3D, formatos y resolves del juego.

NVK facilita reutilizar inicialmente la ruta Vulkan del SDK. deko3d sigue siendo
candidato para el renderer directo. No hay una comparación de rendimiento de
ambos backends con New Vegas en hardware que justifique declarar uno más rápido.

El primer NRO Xbox ya está enlazado y pasó `tools/check_switch_binary.py`:
AArch64, entrada ELF en cero, segmentos NRO válidos, título FNV-nx, icono
solicitado, fingerprint Mesa 26.2.4 y los dos hooks con sus originales.
La primera prueba en hardware llegó al menú con imagen, audio y controles.
Crear una partida produjo un abort del runtime por una llamada indirecta a
`0x82D1E938` sin registrar, confirmado en el log y la traza simbolizada. Véase
FIRST_BOOT.md para probar la compilación con los límites corregidos.

`tools/audit_adjustors.py` verifica 22 entradas de ocho bytes contra la imagen
original de la revisión compatible: `addi r3,r3,delta` y un salto relativo sin
link a una función registrada. Cada entrada tiene referencias alineadas en
los datos originales. `tools/codegen.ps1` declara sus límites; con
`--require-registered`, el auditor también comprueba sus cuerpos y registro
generados. Esta comprobación acota el fallo observado y ese patrón de saltos;
no acredita cobertura completa de llamadas indirectas ni gameplay.

La alpha02 superó esa llamada y reprodujo la cinemática inicial, lentamente;
el siguiente cierre durante la carga señaló `0x82C17A70`. La revisión amplia
de `tools/audit_function_entries.py` recorre valores alineados que apuntan a
secciones ejecutables desde las secciones de datos (excepto relocaciones).
Los compara con el registro y las extensiones PDATA. Verifica once funciones
originales pequeñas fuera de PDATA, incluyendo su extensión y cierre del
flujo de control. La alpha03 declara esos límites sin modificar el XEX.

Las direcciones internas de rangos PDATA y los valores que parecen punteros
quedan inventariados para revisión; no se registran automáticamente como
funciones independientes. El análisis no cubre punteros calculados en ejecución
ni prueba el despacho de excepciones. `--require-registered` comprueba también
la presencia de los once cuerpos y entradas generados, y `--entry-audit` en
el comprobador del binario verifica sus aliases enlazados.

El preset alpha03 activa el muestreo de stacks del SDK, con un flag propio
desactivado por defecto. La prueba pasiva mostró un hilo cercano al 100 %
de un núcleo; no identificó su función. El muestreo modifica los tiempos:
se usa para localizar el coste, no como benchmark ni prueba de una mejora.

La barrera portable de arranque está en `src/switch_startup_gate.cpp` y usa
dos wrappers propios sin editar los cuerpos generados. Su flag tiene valor
false por defecto; el preset experimental `config/switch-alpha.toml` la activa
para comparar con el checkpoint que permitió llegar al menú en Windows.

La referencia Windows produjo una captura privada de 95 s con 7 programas
Xenos observados, 5 vertex y 2 pixel. `--dump-shaders` en `probe_host.py`
y `tools/inventory_shaders.py` permiten reproducir e inventariar esa captura.
No acredita cobertura de todo el juego ni una biblioteca nativa deko3d.

La alpha03 alcanzó el tutorial y el creador de personaje en consola, con mundo
negro. El cierre de autoguardado señaló una entrada calculada (`0x82814F90`)
sin referencia literal en datos. La alpha04 añade seis forwarders de cuatro
grupos delimitados por un retorno anterior y una entrada registrada posterior.
El auditor comprueba sus cuatro instrucciones y conserva los once leaf y
22 adjustors previos. No se añadieron los cientos de fragmentos que coinciden
solo con el patrón, porque pueden ser finales internos de funciones.

Para capturar también SPIR-V del SDK Vulkan antes de compilar:

```powershell
python tools/prepare_horizon_shader_dump.py --sdk 'RUTA/sa2-nx/sdk'
```

Es un cambio diagnóstico opt-in usando la API de dump existente. La captura
queda privada en el destino de `dump_shaders`. El preset alpha04 conserva FBO,
desactiva consultas nativas de oclusión como comparación de compatibilidad y
vuelve al perfil pasivo. No acredita mejora ni integración deko3d. Véase
[la decisión gráfica](GRAPHICS_DECISION.md) para las guías SA2 y el siguiente hito.

## Recuperación de memoria alpha07

`tools/build_switch.ps1` instala también `prepare_horizon_recovery.py` en el SDK
fijado. Agrupa operaciones de protección solo dentro de la misma vista/chunk,
con la misma transición y sin redondear el rango solicitado. Registra permisos
después del éxito del kernel; un fallo conserva el estado anterior del tramo.
El perfil cuenta páginas vigiladas, chunks/vistas y operaciones/fallos acumulados.

Si los callbacks de invalidación ya se procesaron y la metadata guest permite
escribir, un remapeo fallido puede autorizar una única emulación mediante el
shadow existente. El handler requiere un store decodificado, FAR coincidente,
span dentro de la página autorizada y backing contiguo. Loads, atomics no
soportados, cruces de página y permisos guest insuficientes no se recuperan por
esta ruta. No se borran watches globalmente ni se reintenta una página inaccesible.

`config/switch-recovery.toml` usa FSI sin dumps ni capturas. A/B/X/Y se corresponden
con las letras de Switch (`input_xbox_layout=false`). La caché de disco de Mesa
ya está compilada; el app fija su carpeta a `/switch/FNV-nx/cache/mesa` antes del
GPU setup. La caché SDK de descripciones no equivale al binario compilado del
driver y actualmente `vkCreateGraphicsPipelines` recibe un cache nulo.

Las ocho capturas alpha06 validaron 19 descriptores/4.014.080 bytes. El cierre
se repitió en `sub_8286CCD0` después de 69 fallos `0xCE01`. Los logs registraron
la escritura y reapertura de un guardado manual, pero no explican aún el aviso
del usuario. Los fallos de GetDeviceName/State/Data ahora registran su ID sin
devolver éxitos ficticios. La recuperación y su coste necesitan validación en
hardware; no se anuncia una mejora ni un renderer deko3d integrado.

## Hilos y esperas observados

El proceso permite cores 0–2 (`0x7`). Main XThread, GPU Commands y Audio Worker
ya conservan esa máscara, con prioridades 0x3B, 0x2C y 0x2B respectivamente.
El SDK no fija los seis procesadores guest a seis núcleos host cuando hay menos
de seis; mantiene la numeración guest y permite al scheduler repartirlos.
Los workers de compilación usan la banda 0x3B y esperan una condición cuando
no hay tareas. No activar los experimentos de core exclusivo de otros juegos
sin identificar primero los hilos críticos de New Vegas.

NVK espera las fences mediante `nvFenceWait`, con timeouts para revisar errores.
Hay un bucle con yield para confirmar el reporte mapeado después de la fence;
su existencia no demuestra que cause el 99% del Main XThread. El command
processor usa `vkWaitForFences` al necesitar completar submissions para coherencia
o reutilización. Los tiempos de espera no equivalen a GPU ociosa, y sumar tiempos
de varios hilos no da tiempo de pared. Las ventanas sin envíos durante compilación
son distintas del gameplay con presión de cola. El perfil alpha06 no tenía
muestreo de stacks: la causa del uso del hilo principal sigue sin localizar.

La foto del guardado confirma una miniatura con ruido y el mensaje de dispositivo
seleccionado no disponible. El binario no importa XamContentGet/SetThumbnail:
no atribuir su miniatura a la conversión PNG de esas APIs. La captura/readback
del juego y la comprobación de almacenamiento requieren diagnósticos separados.
