# Revision de los forks Mesa para Switch

Revision del 16 de octubre de 2026, con refs explicitos para poder repetirla.

| Repositorio / copia | Commit examinado |
| --- | --- |
| danfromtico/mesa-switch main | 0ff52079ca9e9e837ed27eb95a8d239f439a1864 |
| NaGaa95/mesa-switch main | 2330d007712acbd32e396574203339df39bde153 |
| Checkout local / identificador del build conservado | 606441c90e459028a50fca352daebfc20211f890 |

Ambos repositorios describen Mesa 26.2.4 con NVK Vulkan para Horizon, enlazado
estaticamente como libvulkan.a sin loader/ICD externo. Eso encaja con incluir
el backend en el NRO. No implica usar Wine o instalar DLLs en la SD.
Fuentes: [danfromtico](https://github.com/danfromtico/mesa-switch/blob/0ff52079ca9e9e837ed27eb95a8d239f439a1864/README.rst)
y [NaGaa95](https://github.com/NaGaa95/mesa-switch/blob/2330d007712acbd32e396574203339df39bde153/README.rst).

La [comparacion de refs](https://github.com/danfromtico/mesa-switch/compare/2330d007712acbd32e396574203339df39bde153...0ff52079ca9e9e837ed27eb95a8d239f439a1864)
marca danfromtico dos commits por delante y ninguno por detras: la fusion
606441c9 de la rama NaGaa95, y un cambio posterior para cerrar los buffers
cacheados de los dispositivos al terminar el runtime Horizon. El checkout local
ya contiene esa fusion. No son dos alternativas independientes en esos refs.
El commit adicional describe limpieza de recursos al cerrar; no demuestra una
mejora de FPS ni se ha medido aqui contra el otro build.

La copia local tiene siete archivos de wrappers/configuracion de compilacion
modificados, sin cambios tracked en el renderer. Sus bibliotecas conservadas
tienen version 26.2.4 e identificador git-606441c90e. Se guardaron hashes de esos
wrappers y de libvulkan.a en evidencia privada. No se actualizo el driver en
este lote, no se mezclaron archivos de SDK entre builds y no se recompilo Mesa.

La ruta sigue siendo D3D9 propio -> Vulkan -> Mesa/NVK. Las pruebas COM/Vulkan
actuales usan el driver desktop de Windows; aun no enlazan el NVK conservado
al NRO PC. La siguiente revision del SDK debe fijar un commit y un conjunto
coherente de bibliotecas/headers, comprobar la construccion y despues comparar
la misma prueba en Switch. No se atribuye rendimiento de NVK a estas pruebas
desktop ni al antiguo renderer Xenos de Xbox.
