# Helpers de frames normales con cookie

audit_seh_helpers.py verifica un par de candidatos explicitos contra una forma
MSVC de registro SEH con cookie. Decodifica el cuerpo completo hasta RET y exige
la secuencia de operandos, no solo marcadores de bytes. Solo permite variables
la direccion de handler y cookie, validadas en text y data respectivamente.
Registra hashes del contenedor, perfil, herramienta y cuerpos, sin publicar
instrucciones ni datos del juego. El resultado no implementa excepciones.

El detector del SDK espera otra variante y no identifica los dos helpers de
la captura FNV auditada. Las direcciones concretas quedan en un reporte privado
ligado a esa identidad; no se incorporan como constantes para otras ediciones.
run_aot_analysis.py acepta --seh-helpers REPORTE junto con --frame-restore y
--generation-name NUEVO. Revalida el reporte antes/despues de la generacion y
entrega los overrides de helpers al traductor. build_cpu_runtime.py vuelve a
validarlo si aparece en el reporte de generacion. No modifica el SDK.

test_seh_helpers.py --python-deps DEPS --sdk SDK ejecuta 78 controles negativos
y genera cuatro funciones de un PE propio por el pipeline completo (sin gaps).
test_windows_seh_cookie.py --sdk SDK --python-deps DEPS --output NUEVO
--arm-cc GCC --cc ZIG cc ejecuta 1000 ciclos anidados en cuatro contextos, compara
FS:[0], cookie/scope, ESP/EBP, EBX/ESI/EDI y LastError; compila el mismo fixture
para ARM64. --without-overlay es control negativo y debe fallar restaurando EBP.

La captura real confirma el patron y que un caller de CRT auditado llama al par;
no se ejecuto ese CRT. Las pruebas ejecutables usan codigo propio. La generacion
grande de 346 objetos sigue siendo la anterior, sin estos overrides. No hay
dispatch de excepciones, filtros, unwind, arranque frio ni nuevo NRO por esto.
