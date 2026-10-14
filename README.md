# FNV-nx — rama PC

La ruta activa de esta rama `pc-port` es **Fallout: New Vegas PC / Steam para
Nintendo Switch**. El trabajo Xbox queda pausado y conservado en `main`.

La primera prueba integral usa Autorun/Wine, FEX o Box64, y D3D9 mediante
DXVK/Mesa NVK. Es un punto de partida con traduccion dinamica, no un motor
recompilado estaticamente ni una version PC ya jugable en consola.

El runtime y el adaptador de arranque compilan para Switch. La instalacion
Steam examinada pasa la auditoria PE/BSA y la cobertura de imports estaticos
del ejecutable y sus cuatro DLL propias. Las DLL de Wine/DXVK/FEX preparadas
coinciden con el runtime. Falta verificar en hardware el arranque original de
Steam, la entrada al motor y la inicializacion real de graficos/audio/input.

Instrucciones, configuracion y compilacion: [pc/README.md](pc/README.md).
Los ajustes PC se hacen en [pc/fnv-pc.ini](pc/fnv-pc.ini), sin utilizar el INI
Xbox. El NRO se coloca en `sd:/switch/FNV-nx/fnv-nx.nro`; cada usuario aporta
su instalacion PC en `sd:/switch/FNV-nx/pc/`. No se incluyen archivos del juego.

La version original examinada arranca desde la seccion `.bind` de Steam;
el ejecutable permanece intacto. Ninguna comprobacion de archivos o de imports
constituye prueba de que su autenticacion o el juego funcionen en Horizon.
