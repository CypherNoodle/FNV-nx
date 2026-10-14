# FNV-nx: ruta PC / Steam

Esta rama `pc-port` pausa el trabajo Xbox y prepara una prueba del ejecutable
PC original en Nintendo Switch. La primera ruta usa Autorun/Wine con FEX o
Box64 y D3D9 mediante DXVK/Mesa NVK. Es traduccion dinamica de CPU; todavia no
es la recompilacion estatica del motor ni demuestra rendimiento del juego.

## Que esta preparado

* Auditoria de PE32, imports, DLL originales, cabeceras BSA y archivos requeridos.
* Adaptador que carga `fnv-pc.ini`, comprueba archivos y entrega el ejecutable
  real a Wine; no reproduce capturas del menu.
* Perfiles FEX/Box64 y DXVK/Wine, contador FPS, muestreo del runtime, vsync,
  limite de frames y presentacion framebuffer/compositor.
* Resolucion 640x360, 960x540 o 1280x720 desde el INI; inicio a 960x540, ventana
  y MSAA desactivado. Los INI del juego se inicializan desde su propio
  `Fallout_default.ini` y se fusionan conservando las demas opciones.
* Mapeo XInput por letras o posicion: `labels` hace coincidir A/B/X/Y de Switch
  con las indicaciones Xbox del juego. La vibracion XInput del pin upstream
  aun no esta implementada.
* Dependencias fijadas en `upstream-lock.json`; no se mezclan las bibliotecas
  NVK de Xbox con utilidades Mesa de otra version.

La auditoria de la instalacion Steam examinada pasa. Su entrada original esta
en `.bind`, el arranque de Steam. No se modifica ese ejecutable ni se suplanta
su autenticacion. Que Wine, Steam, el motor, audio y D3D9 puedan inicializarse
juntos en Horizon requiere comprobarlo con el NRO y archivos propios.
Tambien pasa la cobertura de imports estaticos del ejecutable y sus DLL
originales Steam, Bink y Vorbis; que exista un export no prueba su comportamiento
ni cubre DLL cargadas dinamicamente.

## Archivos en SD

```text
sd:/switch/FNV-nx/fnv-nx.nro
sd:/switch/FNV-nx/fnv-pc.ini
sd:/switch/FNV-nx/pc/FalloutNV.exe
sd:/switch/FNV-nx/pc/steam_api.dll
sd:/switch/FNV-nx/pc/binkw32.dll
sd:/switch/FNV-nx/pc/libvorbis.dll
sd:/switch/FNV-nx/pc/libvorbisfile.dll
sd:/switch/FNV-nx/pc/Fallout_default.ini
sd:/switch/FNV-nx/pc/Data/...
sd:/switch/wine/...                  # runtime compartido: DLL, NLS y fuentes
```

Copiar una instalacion PC propia completa dentro de `pc/`, incluyendo `Data` y
DLL originales. Los archivos Xbox no sirven aqui. El INI Xbox no se lee.
Wine necesita su conjunto compatible de DLL ARM64X/i386, las fuentes y NLS;
copiar solo el NRO no basta. El paquete no incluye archivos de Fallout.

El INI se aplica cada arranque y genera `pc/FalloutNV.wine-nx.txt` para el
runtime. Ese archivo es interno; editar `fnv-pc.ini`. `four_cores=1` solo puede
funcionar cuando el proceso dispone de ese permiso; no lo concede el INI.
La primera prueba mantiene tres nucleos y desactiva generacion de frames.

Los registros son `FNV-nx/logs/fnv-pc-bootstrap.log` y
`wine/logs/autorun_runtime.log`. Un `preflight=PASS` solo significa archivos y
perfil preparados; no demuestra ejecucion del motor. El registro de Wine
debe identificar por separado LOWVA, WoW64/FEX, attach del ejecutable y D3D9.

Los PE32 pueden requerir direcciones bajas y un forwarder de espacio 32 bits
sin alias, ademas del soporte del kernel que documenta Autorun. El build aqui
no empaqueta ni instala cambios de Atmosphere/Hekate. Revisar los requisitos
de lanzamiento antes de decidir una modificacion de arranque de la consola.

## Comprobacion y compilacion

```sh
python pc/tools/audit_install.py --game /ruta/Steam/Fallout\ New\ Vegas --output /ruta/privada/audit.json
```

`pc/tools/build_runtime.sh` se ejecuta dentro de la imagen fijada en
`upstream-lock.json`, montando el repositorio en `/work` y un volumen Linux
de build en `/build`. El script descarga y verifica las fuentes oficiales,
compila el NRO y prepara las fuentes/NLS/licencias del runtime en
`local/runtime-stage`. Las DLL se obtienen del repositorio oficial separado;
su manifiesto fija hashes e interfaces requeridas. No compilar sobre NTFS si
se dispone de un volumen Linux: la exploracion de cabeceras Wine tarda mucho.

```sh
python pc/tests/test_bootstrap.py --cc cc
python pc/tools/audit_runtime.py --game /ruta/Steam/Fallout\ New\ Vegas --wine /ruta/staging/switch/wine --output /ruta/privada/imports.json
```

Las pruebas del adaptador usan archivos sinteticos en un directorio temporal:
detectan ausencias y errores INI, verifican el perfil y el mapeo de botones,
la fusion de INI y la recuperacion de un reemplazo fallido bajo la restriccion
`rename` de Horizon.
No arrancan Fallout. Datos, ejecutables, shaders, logs y capturas del juego
permanecen privados y fuera de los commits.

Referencias: [Autorun](https://github.com/autorunhq/autorun/tree/cbb0e4e6e7fa7f47f328d92441f51bfb507f266f),
[DLL Horizon](https://github.com/autorunhq/autorun-horizon-dlls/tree/5d6eccb82c266f037dd4305037c986b6303cd59a).
