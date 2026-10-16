# Revision del PR 1: A8 y seleccion de frame

Revision de [PR 1](https://github.com/CypherNoodle/FNV-nx/pull/1), head
bf3204c226c5543012a72c7cf10b98a68f25d3f3. Tres cambios de una linea; los blobs
se verificaron exactamente antes de probarlos en una copia privada del trabajo
actual de pc-port. La revision inicial no modifico el PR. Posteriormente, por
peticion del usuario, se agrego el fix del fixture: head actual
02a31d3d5901e5c48fea1eb39a8d595ef4cb9939. El PR se fusiono por orden del usuario
en pc-port: e58401a684a4c9964f85f84d76ca4495273e0b04. Los cuatro cambios ya
estan incluidos; no hay que aplicar el parche otra vez.

## Hallazgo P2 corregido en la rama del PR

El cambio de produccion A8 a RGB=0 es correcto. Sin embargo,
`pc/tests/vk_backend_textures_fixture.h:34` seguia asignando RGB=255 para A8.
Con los tres cambios originales del PR, test_d3d9_vk_backend.py abortaba en
check_texture, assertion memcmp(data,expected,bytes). Hacia falta cambiar alli
`format==3?255:v` por `format==3?0:v`, conservando alpha y el caso L8.
Cambiar solamente el modelo de test_texture_upload.py no cubria esta regresion.
El commit adicional del PR ya corrige esa expectativa. Su base exacta mas las
cuatro lineas cambiadas pasa 81 callers, 26 draws, 141 submits/completions y los
controles negativos. ARM64 -Werror y los 3000 casos/23 negativos tambien pasan.
La descripcion del PR se actualizo con esta evidencia y los limites de cada test.

La documentacion primaria [D3DFORMAT](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dformat)
explica la excepcion A8: sus tres canales de color se inicializan a cero.

## Comprobacion independiente

Nuevo oraculo propio: test_d3d9_texture_channels.py y
d3d9_texture_channels_host.c. Crea un device D3D9 HAL de Windows, textura 16x16
con los 256 valores posibles de alpha/luminancia, dibuja con pipeline fijo y
con un pixel shader ps_2_0 de muestreo directo, y lee el target por
GetRenderTargetData. Compara los pixels reales con el convertidor C indicado.
ARGB8 y L8 son controles adicionales; no usa el modelo Python como referencia.

Resultado en NVIDIA GeForce GTX 1660 SUPER: 1536 pixels (3 formatos x 2 rutas
x 256), PASS para el convertidor del PR. A8 da (0,0,0,A) en ambas rutas. El
convertidor anterior con RGB blanco falla; el control negativo detecta ese
fallo expresamente. Esto es evidencia de Windows HAL, no de Switch/NVK.

Uso: `python pc/tests/test_d3d9_texture_channels.py --native <checkout-con-fix>/pc/native
--output <carpeta-nueva> --cc <compilador-C>`. En pc-port antes de fusionar el
fix A8, usar `--expect-mismatch` para verificar que detecta el defecto conocido.
No se debe presentar ese control negativo como una prueba positiva del renderer.

## Draw y regresiones

El guard de draw compila para ARM64 con -Werror. Hoy la preseleccion y
begin_frame recorren los mismos slots bajo el mismo mutex, asi que no hay
una diferencia observable en la ruta normal. No es una mejora de rendimiento.
La condicion de frame distinto no fue inducida: pasar estos tests no demuestra
que esa ruta defensiva completa todos los pasos de rollback que una futura
politica de seleccion pudiera necesitar.

Con el PR mas la expectativa A8 corregida: la regresion Vulkan pasa 82 callers,
57 comprobaciones de fabrica, 26 draws y los tres controles negativos. Headless
conserva 141 submits/completions; WSI pasa cuatro Present COM y 146 submits/
completions. El modelo CPU tambien pasa 3000 casos y 23 negativos. No se
realizo una prueba Switch de A8 ni se dibujaron shaders del juego.

Evidencia privada: local/pr1-a8-hal02, pr1-a8-old-negative01, pr1-upload01,
pr1-vk-exact01 (fallo reproducido), pr1-vk-reviewed01 (corregido) y
local/pr1-review/backend-arm64.o. El parche complementario de una linea queda
en la entrega local pc-renderer-hw01/pr1-additional-a8-test.patch; git apply
--check paso en una copia limpia del fixture. Esa linea ya esta incluida en el PR;
no hace falta aplicarla de nuevo a su head actualizado. Los tres blobs originales
del autor se conservan.
