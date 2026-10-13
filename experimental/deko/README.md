# Prototipo de backend deko3d

Este núcleo experimental está separado del NRO Xbox actual. No es todavía
un renderer del juego ni un NRO jugable deko3d. Se implementaron:

- Adaptación offline de UBOs capturados a un ABI std140 explícito, con planes
  de copia por campo. Los SPIR-V originales permanecen intactos.
- Bindings explícitos para samplers combinados, conservando en un manifiesto
  la imagen y el sampler originales que deberá enlazar el backend.
- Cargador nativo de módulos DKSH y ownership de memoria de código deko3d.
- Registro de draws indexados y no indexados con shaders, buffers, texturas, targets,
  viewport, scissor, blend, rasterización y depth/stencil proporcionados por el caller.

## Verificación inicial

Captura FBO: 69 de 79 variantes compilan con UAM después de la adaptación.
Diez requieren `GL_EXT_demote_to_helper_invocation`, pendiente de una solución
que conserve el comportamiento; no se sustituyó por discard sin comprobarlo.
Captura FSI: 31 de 68 variantes compilan; los fragment shaders pendientes
requieren esa operación y la ruta de interlock. No hay cobertura de todo el juego.

El núcleo compiló como biblioteca ELF AArch64 con devkitA64 y headers deko3d.
La copia de constantes pasó pruebas host de representación (NaN y cero con
signo), strides, límites, overflow, rechazo de alias y fallo sin escritura
parcial. Cada plan generado comprueba que sus campos caben, no se solapan y
conservan los bytes de origen. La compilación no prueba bindings ni imagen
correctos en GPU; tampoco valida todos los casos extremos de aritmética.

## Construcción

```powershell
python tools/build_deko_shader_prototype.py --shaders 'RUTA/CAPTURA' --output 'local/deko-native' --spirv-cross 'RUTA/spirv-cross' --spirv-val 'RUTA/spirv-val' --uam 'RUTA/uam'
cmake -S experimental/deko -B local/deko-core-build -G Ninja -DCMAKE_TOOLCHAIN_FILE='RUTA/REPO/cmake/switch-devkitA64.cmake' -DDEVKITPRO='RUTA/devkitPro' -DCMAKE_BUILD_TYPE=Release
cmake --build local/deko-core-build
```

Los DKSH, GLSL, SPIR-V y manifiestos derivados del juego se guardan en `local/`
y no se versionan. `src/deko/` contiene el código propio del núcleo.

## Pendiente para una prueba gráfica real

La captura alpha05 produjo ocho draws reales, cuyos buffers y constantes se
validaron y exportaron. `build_deko_geometry_bundle.py` crea bundles privados:
reubica los fetches de vértices y la dirección de índices en un SSBO compacto,
conserva los bytes originales y sus controles endian, y requiere shaders de
vértices sin texturas ni restart. Los fragment shaders FSI siguen pendientes.

El build experimental ahora produce `fnv-deko-replay.nro` y `flat.dksh`.
Se ejecuta separado del NRO del juego y lee `sdmc:/switch/FNV-nx/deko-replay/`:
`00/geometry.bin`, `00/vertex.dksh` hasta `07/`, y `flat.dksh`. A inicia;
izquierda/derecha cambia el draw, Y alterna scissor original/completo, X invierte
el eje vertical y + sale. Registra `logs/fnv-deko-replay.log` y espera fences
con tiempo limitado antes de reutilizar/destruir recursos. Conserva constantes
y viewport del draw, usa color plano, desactiva depth/culling y no reproduce
la escena completa. Su imagen, orientación y comportamiento GPU requieren
validación completa; sus tiempos no son un benchmark del juego ni de FSI.
En la primera prueba el usuario vio algunas figuras amarillas sobre fondo oscuro,
lo que confirma salida geométrica parcial, sin texturas ni sombreado originales.
No se obtuvo log: la v0.2 crea y comprueba la carpeta/ruta del registro, muestra
la ruta en pantalla y usa `sdmc:/fnv-deko-replay.log` como alternativa si falla
la ruta habitual. Si ambas fallan, detiene el inicio con un error visible.
Comprueba los 17 recursos antes de iniciar la GPU y muestra cualquier faltante.

El cargador reserva también `DK_SHADER_CODE_UNUSABLE_SIZE`, como exige deko3d,
para que un módulo cuyo código termine en el límite de página no ocupe la
zona final reservada del bloque.

Se añadió una captura optativa de geometría al renderer vigente. Preparar el
SDK con `tools/prepare_horizon_draw_capture.py --sdk RUTA/SDK` y compilar el NRO;
`tools/build_switch.ps1` aplica esta preparación y el dump de shaders automáticamente.
Usar `config/switch-deko-capture.toml` como `fnv-nx.toml`. Captura hasta ocho
pares de shaders de draws indexados, sin tessellation ni memexport, de al menos
96 índices. Guarda registros, constantes de sistema, viewport/scissor, mapas
de constantes e índices/buffers de vértices en `dump_shaders/draw-captures`.
Límites: 16 MiB de geometría por draw y 64 MiB por ejecución. La escritura en
SD puede causar una pausa; no usar esta ejecución para medir rendimiento.

Las páginas se leen desde el alias shadow ya existente, sin cambiar protecciones
ni comprometer memoria. La captura es CPU; no prueba coherencia de datos escritos
por GPU y no contiene texturas/EDRAM. Solo los pares `.json` y `.bin` completos
son válidos. Son datos privados del juego: no publicar en el repositorio.

`tools/inspect_deko_draw_capture.py --capture RUTA/draw.json --output local/draw`
valida el formato (512 bytes para SystemConstants del SDK; 504 bytes reflejados
por estos shaders, más dos constantes de rango de tessellation). Con
`--vertex-abi` y `--pixel-abi` acepta los manifiestos de
la variante exacta y exporta sus UBOs con el ABI nativo. El replay separado usa
solo el vertex stage original; el pixel stage es diagnóstico.

1. Capturar y enlazar texturas, samplers, buffers y constantes de draws reales.
   Los shaders usan vertex pulling y memoria compartida Xenos; compilar un
   shader no proporciona esos datos automáticamente.
2. Crear pools de descriptores, targets, barriers, colas, fences y presentación,
   respetando la vida útil de los módulos y recursos.
3. Verificar llamadas y layout D3D de New Vegas y conectar un draw real al núcleo.
   No están interceptadas las llamadas del juego ni el transporte PM4.
4. Comparar imagen y estado con FSI y resolver las variantes pendientes antes
   de ofrecer un renderer completo o medir una mejora.

FSI ya mostró el interior y permitió cargar una partida, con caras y el icono
de autoguardado distorsionados. El cierre al salir de la casa señaló fallos
`0xCE01` de protección/mapeo y una escritura física no recuperada; ese problema
del runtime sigue pendiente y no se considera resuelto por este prototipo.
