# Primer NRO Xbox experimental

El prototipo enlaza código real de Xbox 360, servicios ReXGlue Horizon y el
plugin Xenos Vulkan con NVK Mesa 26.2.4. Se verificaron el ELF AArch64, el punto
de entrada en cero, los segmentos NRO, el nombre FNV-nx, el icono solicitado y
los hooks de arranque. La primera ejecución en consola llegó al menú con
imagen, sonido y controles, según la prueba del usuario. La carga de una
partida falló por una función indirecta sin registrar (`0x82D1E938`).

La alpha02 añadió ese límite y otros 21 saltos, y llegó a la cinemática inicial.
La prueba del usuario observó reproducción lenta y cierre durante la carga
tras saltarla, por otra entrada sin registrar (`0x82C17A70`). La alpha03 añade
once funciones originales verificadas mediante revisión estática y de su
flujo de control. En consola llegó al tutorial y al creador de personaje;
el mundo quedó negro y el autoguardado cerró por `0x82814F90`. La alpha04
añade ese forwarder y otros cinco de grupos verificados.

1. Copia `switch/FNV-nx/` del paquete a la SD. El destino del ejecutable es
   `sdmc:/switch/FNV-nx/fnv-nx.nro`.
2. Coloca en esa misma carpeta los archivos originales extraídos de Xbox 360,
   incluidos `default.xex`, `Data/` y el resto de carpetas, con su estructura
   original. La revisión compatible es la descrita en BASELINE.md.
3. Abre el NRO en modo aplicación, como en las pruebas anteriores con Spaira
   desde su NSP. Espera el menú, crea una partida y registra hasta dónde llega
   (cinemática, carga o escena jugable). Para cerrar, usa HOME.
4. Recoge `sd:/switch/FNV-nx/logs/fnv-nx.log`. Si hay un error de sistema,
   conserva también los archivos que aparezcan en `logs/rex/`.

El paquete incluye `fnv-nx.toml` con una barrera experimental activada. Espera
el punto de inicialización observado en Windows durante un máximo de 120 s;
el log indica si el worker esperó, se liberó o agotó el plazo. Este plazo no
es una estimación del tiempo total de arranque. Se puede comparar poniendo
`fnv_startup_barrier = false` en el mismo archivo.

La alpha04 vuelve al perfil pasivo (`switch_profile_stacks = false`) y desactiva
consultas nativas de oclusión como prueba del mundo negro. Copia también el
nuevo `fnv-nx.toml`; aún no se ha validado una mejora de imagen o rendimiento.
Conserva `logs/rex/rex_profile.log`, el log principal y el registro del cierre.
El preset captura shaders observados en
`sd:/switch/FNV-nx/cache/shader-diagnostics-alpha04/`; conserva esa carpeta
para investigar materiales y preparar una prueba de conversión a deko3d.

Las rutas internas usan `/switch/FNV-nx` sobre el dispositivo SD de libnx,
porque libstdc++ considera `sdmc:` una ruta relativa. El destino físico sigue
siendo el solicitado. El empaquetado automático de assets queda desactivado:
la carpeta también contiene NRO, configuración, logs y datos de usuario.

La siguiente ejecución debe comprobar autoguardado, movimiento y si aparece
el mundo. Esta compilación sigue usando NVK; deko3d aún no está integrado.

La alpha04 mostró HUD pero conservó el mundo negro. Para una sola comparación
de corrección visual, `config/switch-world-fsi.toml` se copia con el nombre
`fnv-nx.toml` y se reinicia el mismo NRO. Esta ruta puede ser más lenta: el
objetivo es comprobar si aparece la escena, no medir una mejora. Conserva
los logs y `cache/shader-diagnostics-fsi/`, y restaura luego el preset FBO.
El gameplay, la vibración y el rendimiento todavía no están validados. Este
backend GPU es el del SDK para el arranque; la adaptación del renderer directo
y la evaluación de deko3d siguen pendientes.
