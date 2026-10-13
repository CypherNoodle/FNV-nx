# Primer NRO Xbox experimental

El prototipo enlaza código real de Xbox 360, servicios ReXGlue Horizon y el
plugin Xenos Vulkan con NVK Mesa 26.2.4. Se verificaron el ELF AArch64, el punto
de entrada en cero, los segmentos NRO, el nombre FNV-nx, el icono solicitado y
los hooks de arranque. La primera ejecución en consola sigue pendiente.

1. Copia `switch/FNV-nx/` del paquete a la SD. El destino del ejecutable es
   `sdmc:/switch/FNV-nx/fnv-nx.nro`.
2. Coloca en esa misma carpeta los archivos originales extraídos de Xbox 360,
   incluidos `default.xex`, `Data/` y el resto de carpetas, con su estructura
   original. La revisión compatible es la descrita en BASELINE.md.
3. Abre el NRO en modo aplicación, como en las pruebas anteriores con Spaira
   desde su NSP. Déjalo avanzar y registra qué aparece. Para cerrar, usa HOME.
4. Recoge `sd:/switch/FNV-nx/logs/fnv-nx.log`. Si hay un error de sistema,
   conserva también los archivos que aparezcan en `logs/rex/`.

El paquete incluye `fnv-nx.toml` con una barrera experimental activada. Espera
el punto de inicialización observado en Windows durante un máximo de 120 s;
el log indica si el worker esperó, se liberó o agotó el plazo. Este plazo no
es una estimación del tiempo total de arranque. Se puede comparar poniendo
`fnv_startup_barrier = false` en el mismo archivo.

Las rutas internas usan `/switch/FNV-nx` sobre el dispositivo SD de libnx,
porque libstdc++ considera `sdmc:` una ruta relativa. El destino físico sigue
siendo el solicitado. El empaquetado automático de assets queda desactivado:
la carpeta también contiene NRO, configuración, logs y datos de usuario.

La ejecución debe comprobar primero memoria, servicios, shaders y orden de
arranque. El menú y el gameplay en Switch todavía no están validados. Este
backend GPU es el del SDK para el arranque; la adaptación del renderer directo
y la evaluación de deko3d siguen pendientes.
