# FNV-nx: recompilacion estatica PC

Esta rama trabaja en un port nativo de la version PC. Xbox permanece pausado.
El usuario aporta su EXE y datos; el NRO contendra el codigo ARM64 generado
previamente y los servicios nativos de Windows que usa el juego. Es el modelo
de [SH2-nx](https://github.com/cutarev/sh2-nx) y
[SH3-nx](https://github.com/cutarev/sh3-nx), cuyos generadores procesan una
version concreta del EXE antes de construir el NRO.

Leer el EXE en la consola aporta secciones, datos y direcciones del programa.
No traduce todo el motor en ese momento. Cada version soportada necesitara
un perfil de identidad que coincida con el codigo ARM64 compilado. Los bridges
D3D8 de Silent Hill no sustituyen directamente el D3D9 de New Vegas.

## Estado

Existe una biblioteca C independiente en `native/pe_image.c` que:

* Carga encabezados y secciones PE32 en memoria ordinaria suministrada por el
  caller, con inicializacion a cero de las zonas BSS.
* Representa las direcciones x86 como offsets, sin exigir que el proceso
  Switch pueda mapear memoria host en la direccion Windows `0x00400000`.
* Recoge imports por nombre y ordinal y sus slots IAT.
* Enlaza todos los slots mediante un resolver nativo; si falta un servicio,
  deja la IAT completa intacta y devuelve un error.
* Comprueba rangos, huecos, capacidad de memoria, tablas truncadas, secciones
  solapadas, nombres invalidos y slots duplicados.

La prueba host compara el mapeo completo y los imports con un parser
independiente. Paso con fixtures y con el EXE Steam examinado: 280 slots,
17 DLL. La biblioteca tambien compilo como objeto ARM64 con devkitA64.
Los tokens del test son sinteticos; no implementan funciones Windows.
No se ejecuto codigo del juego ni se verifico este cargador en consola.

El laboratorio nativo previo conserva 412 constructores CRT observados y
una frontera en `0x44F770`, antes del motor. Usa codigo capturado de una
sesion PC y no es una recompilacion completa ni un arranque desde el EXE
original. No hay un perfil AOT del motor listo, un launcher nativo del juego
ni un NRO nuevo para probarlo todavia.

La version Steam examinada coincide con SteamStub 2.1 y depende tambien de
Steamworks. Su entrada original esta en `.bind`. El archivo permanece
intacto y no se sustituye su autenticacion. Consultar
[el estudio de la dependencia](docs/steam-dependency.md). No se presupone
compatibilidad con otra distribucion hasta auditar su archivo real.

## Carpeta de instalacion objetivo

Este es el contrato del futuro port, no un paquete ya disponible:

```text
sd:/switch/FNV-nx/fnv-nx.nro
sd:/switch/FNV-nx/FalloutNV.exe
sd:/switch/FNV-nx/Data/...
sd:/switch/FNV-nx/fnv-pc.ini     # configuracion nativa futura, opcional
```

EXE y datos junto al NRO, sin `pc/` ni `switch/wine/`. El cargador actual
no crea ni copia carpetas. El INI `pc/fnv-pc.ini` conservado en este repo
pertenece al experimento Wine y no configura la biblioteca nativa.
Las rutas de guardados y cache nativos se definiran al integrar esos servicios.

## Proxima integracion

Primero se necesita un perfil verificable del ejecutable y una generacion
AOT que cubra el arranque original, callbacks y entradas indirectas. Cada
import requiere su ABI x86, retorno, efectos sobre memoria y comportamiento
de error correctos, no una funcion vacia que diga que tuvo exito. La carga
dinamica de bibliotecas, TLS, excepciones y las tablas virtuales necesitan
auditorias adicionales.

Los servicios abarcan Windows/CRT, D3D9/D3DX, input, sonido y video. Se
pueden reutilizar los conocimientos y componentes host ya probados para
Switch; los shaders, las llamadas D3D9 y el motor PC aun requieren adaptacion.
El renderer de Xbox no es un reemplazo directo del renderer PC.

La siguiente prueba de consola del motor requiere una entrada ARM64 real
que alcance inicializacion del juego con esos servicios. Una lectura PE
correcta, un menu capturado o un triangulo no cumplen ese criterio.

## Pruebas y herramientas

```sh
python pc/tools/audit_native_imports.py --exe /ruta/FalloutNV.exe --output /ruta/privada/native-imports.json
python pc/tests/test_native_pe.py --cc cc --game /ruta/Fallout-New-Vegas
cmake -S pc/native -B /ruta/privada/native-build
cmake --build /ruta/privada/native-build
```

`--game` es opcional para ejecutar solo fixtures. `--cc` admite un comando
con argumentos, por ejemplo `--cc /ruta/zig cc`. CMake construye una
biblioteca de carga de datos; no genera un NRO ni recompila el motor.
`audit_native_imports.py` produce una lista de requisitos, sin afirmar que
los bridges esten implementados ni que el ejecutable carezca de DRM.

Se conservan las herramientas de auditoria PE/BSA y el
[experimento Wine PC01–PC03](docs/wine-experiment.md) por trazabilidad.
`tools/build_runtime.sh` requiere ahora `FNV_BUILD_HISTORICAL_WINE=1` para
reproducir explicitamente ese experimento pausado. No construye el port
nativo. Los informes de ejecutables propios permanecen fuera de los commits.
