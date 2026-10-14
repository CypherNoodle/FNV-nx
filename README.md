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

La version original examinada coincide con SteamStub 2.1 y usa Steamworks;
el ejecutable permanece intacto. Copiarlo con steam_api.dll no elimina la
dependencia del cliente Steam. Detalles en [pc/docs/steam-dependency.md](pc/docs/steam-dependency.md).
La primera prueba hardware encontro los archivos del juego, pero el runtime
abrio la biblioteca de Autorun al rechazar sus DLL. La entrada dedicada pc02
elimina ese fallback y notifica el fallo; no elimina DRM ni demuestra arranque.
