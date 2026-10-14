# Dependencia de Steam: ejecutable original examinado

La instalacion Steam original examinada tiene su punto de entrada dentro de
`.bind`. Una comparacion de solo lectura con las firmas del detector publico
de Steamless encuentra la firma de **SteamStub variante 2.1 x86**. No se
desempaqueto, parcheo ni ejecuto el binario para esa comprobacion.

Ademas, el ejecutable importa `SteamAPI_Init`, `SteamAPI_IsSteamRunning`,
interfaces de usuario/amigos/logros y callbacks. Su `steam_api.dll` original
contiene el diagnostico de inicializacion fallida por no encontrar una instancia
de Steam o `steamclient.dll`. Estos imports van mas alla de que exista un nombre
de DLL en la tabla PE.

Valve documenta que el wrapper DRM verifica la propiedad del juego e inicia
Steam antes del juego. Tambien documenta que la inicializacion de Steamworks
necesita un cliente ejecutandose para proporcionar sus interfaces.
Por tanto, **copiar el ejecutable original y steam_api.dll a Switch no elimina
la dependencia de Steam**. Wine/FEX/DXVK no aportan por si solos ese cliente.
El modo offline de Steam no equivale a ejecutar sin el cliente.

Hay dos capas distintas: el wrapper del ejecutable y la integracion Steamworks.
La propia documentacion de Steamless distingue esas dos capas; identificar o
desempaquetar el wrapper no demuestra que desaparezcan las comprobaciones del
juego a Steamworks. No se han implementado sustitutos de autenticacion.

En hardware, la prueba pc01 solo alcanzo `preflight=PASS`: el usuario vio la
biblioteca de Autorun y un aviso de sus DLL. La fuente del runtime tiene un
fallback a esa interfaz cuando `horizon_dlls_ready` falla. No se aporto el
log detallado del runtime, por lo que la DLL concreta y un eventual fallo de
Steam en Horizon siguen sin observarse. No atribuir ese aviso al DRM.

La entrada pc02 elimina la biblioteca y downloader del camino de inicio:
si faltan DLL del runtime, informa de la causa y permite salir con +. Si las
dependencias estan presentes, intenta cargar solamente FalloutNV.exe.
Esta correccion no elimina SteamStub ni acredita arranque del motor.

GOG ofrece oficialmente Fallout: New Vegas Ultimate Edition sin DRM y sin
necesitar Galaxy. Es una distribucion apropiada para evitar la dependencia
del cliente Steam. Su ejecutable todavia no ha sido aportado ni auditado en
este proyecto: adaptar y comprobar sus imports y arranque antes de afirmar
compatibilidad. Desde pc03 el prearranque identifica imports y solicita las
DLL propias de cada archivo; ya no exige steam_api.dll universalmente. La ruta
del ejecutable sigue siendo `FNV-nx/pc/FalloutNV.exe` y los assets `pc/Data`.

Fuentes primarias:

* [Valve: Steam DRM](https://partner.steamgames.com/doc/features/drm)
* [Valve: SteamAPI_Init](https://partner.steamgames.com/doc/api/steam_api#SteamAPI_Init)
* [Detector SteamStub 2.1](https://github.com/atom0s/Steamless/blob/master/Steamless.Unpacker.Variant21.x86/Main.cs)
* [Steamless: alcance del proyecto](https://github.com/atom0s/Steamless)
* [GOG: Fallout New Vegas Ultimate Edition](https://www.gog.com/en/game/fallout_new_vegas_ultimate_edition)
