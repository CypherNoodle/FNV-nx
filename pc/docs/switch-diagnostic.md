# Primer NRO nativo PC: pc-diag01

Este paquete hace una prueba de integracion en Switch. Enlaza el conjunto
ARM64 privado conservado, el runtime propio y libnx en un ELF final y un NRO.
El enlace usa las 354 unidades aceptadas como entrada; el linker elimina
secciones sin referencias. No equivale a arrancar Fallout ni a renderizarlo.

## Instalacion y prueba

Usar modo aplicacion: hbmenu manteniendo R al abrir un juego, o un launcher
con memoria de aplicacion equivalente. El modo Album no realiza la prueba.
El conjunto recompilado hace que el NRO mida aproximadamente 109 MiB.

```text
sd:/switch/FNV-nx/fnv-nx.nro
sd:/switch/FNV-nx/FalloutNV.exe
```

Para esta prueba basta el EXE PC propio. Todavia no necesita copiar Data.
La instalacion futura completa conserva EXE y Data junto al NRO. Los archivos
de Xbox no sustituyen al EXE ni a los datos PC.

La prueba empieza automaticamente. A la repite y + sale normalmente.
No requiere overclock. Mantener los mismos relojes durante la prueba ayuda
a interpretar el tiempo de lectura de SD; no se mide FPS del juego.

El registro se agrega a `sd:/switch/FNV-nx/fnv-pc-diag.log`. Cada ejecucion
empieza con `--- pc-diag01 ---`; no se sobrescribe el registro anterior.
Si no se puede abrir el EXE o escribir el log, la pantalla indica el error.
No se crean carpetas ni se copian DLLs. La aplicacion espera que la carpeta
FNV-nx ya exista. El icono del NRO es el icono del proyecto existente.

## Comprobaciones

* Lectura acotada y SHA256 del EXE, sin modificarlo. La identidad Steam
  conocida se etiqueta como examinada; otras versiones como no perfiladas.
* Carga de encabezados/secciones PE32 y conteo de imports normales. La IAT
  permanece intacta: no se sustituyen imports pendientes por exito ficticio.
* Asignacion de 132 MiB de backing guest, con regiones separadas para imagen,
  PEB, TEB, expansion TLS, pila y un pool VM de 64 KiB.
* Inicializacion del runtime CPU y del contexto Windows de un hilo. Llamada
  a un bridge propio por token, argumentos/retorno stdcall, registros no
  volatiles y LastError en el TEB. No se llama a funciones del juego.
* Permisos Horizon: reserve/commit, lectura readonly, decommit, recommit a
  cero, release y restauracion del backing antes de liberarlo.
* Detach y destruccion ordenada del runtime. Los resultados PASS/FAIL y
  codigos nativos quedan en el registro.

El NRO contiene codigo ARM64 derivado del analisis privado, pero no el EXE,
capturas ni archivos Data. Por eso el artefacto se conserva como salida
privada y no se publica como codigo fuente reutilizable. El proveedor D3D9
Switch, heap guest, sincronizacion, DLLs y entrada original siguen pendientes.
No se intenta ejecutar SteamStub ni una entrada sin perfil compatible.

## Construccion

```sh
python pc/tools/build_switch_diagnostic.py \
  --validation /privado/d3d9-com-switch/validation.json \
  --devkitpro /ruta/devkitPro --cc /ruta/aarch64-none-elf-gcc \
  --nm /ruta/aarch64-none-elf-nm --readelf /ruta/aarch64-none-elf-readelf \
  --elf2nro /ruta/elf2nro --nacptool /ruta/nacptool \
  --icon /ruta/icon.jpg --output /privado/pc-diag01
```

Se necesita el conjunto parcial con su manifiesto e inputs originales
conservados. El script verifica sus hashes antes y despues del build,
compila la entrada, enlaza usando switch.specs y las bibliotecas devkitPro,
y comprueba ELF AArch64/DYN, ausencia de externos fuertes pendientes y
segmentos NRO con cabecera de assets. Guarda comandos de salida y hashes.
La prueba fisica recibida el 15 de octubre de 2026 completo dos ejecuciones
con cero fallos y salida normal. Coincidio el EXE Steam examinado, se recogieron
280 imports y la carga PE tardo 203/207 ms. Pasaron las pruebas de memoria,
bridge stdcall y TEB/TLS. Este resultado valida el diagnostico; el motor no
se ejecuto. Los manifiestos originales de compilacion conservan el valor
hardware_tested=false historico; la evidencia de hardware se guarda aparte.
