# Presentacion Vulkan nativa: primer enlace WSI

El backend ahora tiene una ruta host de presentacion real, separada del readback
de diagnostico. `fnv_vk_backend_create_window` prepara instancia, surface,
familia graphics/present y device con swapchain. La creacion headless anterior
se conserva. `fnv_vk_backend_present` copia el backbuffer implicito con
`vkCmdCopyImage`, transiciona la imagen a PRESENT_SRC y ejecuta QueuePresentKHR.
No descarga pixels a CPU. Tambien se conecta al metodo COM IDirect3DDevice9::Present
mediante el proveedor opcional y su variante bloqueante.

## Contrato inicial

La superficie debe admitir BGRA8 UNORM con espacio SRGB_NONLINEAR, extent exacto
del backbuffer, TRANSFER_DST, transformacion identidad, alpha opaco y una capa.
FIFO para vsync=1 e IMMEDIATE para vsync=0; se rechaza un modo ausente, sin
sustituirlo silenciosamente. No se escala ni se convierten formatos. Entre
1 y 8 imagenes segun los limites reales, se solicitan 3 si es posible.

Se habilitan KHR_surface, KHR_get_surface_capabilities2, EXT_surface_maintenance1
y la extension de plataforma proporcionada. El device requiere KHR_swapchain,
EXT_swapchain_maintenance1 y su feature consultada. Una sola familia sirve
graphics/present: no se inventa soporte ni se implementa ownership entre familias.
Si falta una extension, feature o formato requerido, falla la creacion.

El callback de plataforma entrega una superficie propia al backend; devuelve
NULL al fallar. El caller conserva ventana, servicios y loader hasta que el
destroy del backend termine correctamente. No se conserva el callback despues
de create. `d3d9_vk_horizon.c` ofrece `fnv_vk_backend_create_horizon` con un
NWindow libnx valido y VK_NN_vi_surface. No inicializa servicios ni cambia su
tamano. El caller debe preparar dimensiones coincidentes.

## Sincronizacion y fallos

Se reutiliza el ring de command pools/fences del backend. Cada slot tiene un
semaforo y fence de adquisicion; cada imagen de swapchain tiene su propio
semaforo de presentacion y fence de maintenance1. El fence de render prueba
el consumo del semaforo de adquisicion; el fence de adquisicion se observa
separadamente antes de resetearlo. El semaforo de presentacion se reutiliza
solo para su imagen, tras adquirirla de nuevo y encadenar esa espera. Antes de
reutilizar el fence de presentacion se espera su finalizacion.

Present requiere estar fuera de Begin/EndScene y contenido previo en el
backbuffer. Envia clears pendientes y despues hace la copia por la misma cola.
Si el ring esta lleno devuelve BUSY antes de adquirir otra imagen. El caller
debe poll/wait y reintentar; no hay sleeps internos ni espera infinita. Cada
espera WSI usa el timeout finito del caller; UINT64_MAX se rechaza. El timeout
no es un presupuesto total de todos los pasos y QueuePresent puede bloquear
en el driver. SUBOPTIMAL acepta el frame; no recrea la swapchain.

OUT_OF_DATE/SURFACE_LOST detienen el device. Un error despues de adquirir o
presentar conserva recursos y sincronizacion incierta hasta una limpieza
segura. DeviceWaitIdle no sustituye la observacion de los fences de
presentacion/adquisicion. Destroy puede devolver BUSY y conservar el contexto
si el WSI aun no ha terminado; ante finalizacion desconocida no se destruyen
objetos ni se afirma exito. DEVICE_LOST real permite destruir el device.

Stats expone presentation_requests (solicitudes aceptadas, no prueba de scanout),
acquire_timeouts, swapchain_images y present_mode. El contador de memoria propia
no incluye memoria que el driver/WSI reserva para las imagenes de swapchain.

## Evidencia

`local/vk-wsi-present08/validation.json` PASS en Vulkan Windows real, ventana
oculta propia y sin D3D9 Windows. 16 solicitudes normales (8 FIFO y 8 IMMEDIATE),
20 comandos de copia incluyendo fallos posteriores, 2048 pixels fuente comprobados
por readback GPU, cinco fallos de setup y cinco de runtime. Comprueba salida sin
contenido/escena invalida, timeout de acquire y fence, ring lleno antes de acquire,
contadores, perdida de superficie y limpieza de todos los handles registrados.
Una presentacion rechazada antes de encolar se mantiene en cuarentena; solo se
libera tras enviar esa solicitud desde el test y observar su fence real.
No se capturan/comparan los pixels del compositor: esta prueba no demuestra
fidelidad visual del scanout ni un frame del juego.

`local/vk-wsi-com-regression02/validation.json` conserva los 81 callers x86
traducidos, 57 consultas de fabrica, 26 draws, 141 submits/completions, controles
negativos y escenarios de fallo de la ruta headless. SDK externo intacto.
CMake ARM64 -Werror compila fnv_native_d3d9_com, fnv_d3d9_vk_backend y el nuevo
fnv_d3d9_vk_horizon. El enlace NWindow aun no se ha ejecutado en Switch/NVK.

Runner reutilizable: `pc/tests/test_d3d9_vk_present.py --headers <vulkan-headers>
--output <carpeta-nueva> --cc <compilador>`. Sus resultados son privados de
desarrollo, no assets/carpetas requeridos para la SD.

## Pendiente

El slot COM 17 ya despacha cinco argumentos x86 stdcall (this incluido).
Acepta DISCARD de backbuffer completo: source/dest/dirty NULL y override HWND
NULL o igual a la identidad guest preparada. No transforma ese HWND en un host
pointer. CreateDevice recuerda hDeviceWindow, o focus si el primero es NULL.
Rechaza Present dentro de una escena y antes de CreateDevice si hay fabrica.
Provider ausente o watchdog host deshabilitado devuelve NOTAVAILABLE.

`fnv_vk_backend_present_sync` espera un slot libre solo cuando hace falta,
mediante WaitForFences(waitAny) y observacion/retirada de fences reales. No hace
spin ni vacia siempre todos los frames. Cada espera tiene el watchdog finito
configurado por el host; el timeout pierde el device y devuelve DEVICELOST,
sin exito ficticio ni WASSTILLDRAWING al caller base D3D9. No espera scanout.
Esta politica de timeout es propia del host, no conformidad completa Windows.

Prueba COM actual: 82 callers, 57 consultas de fabrica y 26 draws en cada modo.
El modo WSI ejecuta cuatro Present traducidos, nueve comprobaciones y 146
submits/completions; headless conserva 141 y comprueba NOTAVAILABLE. El test WSI
separado agrega watchdog de acquire/cola: 18 presents y siete fallos de runtime.
La regresion NULLREF separada conserva 400 constantes concurrentes y 86 metodos.

Faltan ventanas guest generales, swap effect COPY y rectangulos;
Reset/recreacion y recuperacion de superficie;
capacidades completas, catalogo del juego, batching/cache y prueba NVK en
hardware. La [prueba conjunta Switch](switch-renderer-probe.md) ya se puede
compilar para esa validacion. Esto no completa DX9 ni arranca Fallout desde EXE.

Referencias: [semaforos por imagen](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html),
[AcquireNextImageKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkAcquireNextImageKHR.html),
[QueuePresentKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).
El enlace de plataforma se contrasto con el WSI del checkout Mesa-Switch local,
commit 606441c90e459028a50fca352daebfc20211f890; no se modifico su renderer.
