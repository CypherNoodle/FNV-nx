"""Explicit identities for optional x87 numerical backends."""
QUAD_MODEL='arm64-quad-bounded-math-v1'
QUAD_FLAGS=['-DFNV_CPU_X87_QUAD_MATH=1','-frounding-math','-fno-builtin','-ffp-contract=off']
QUAD_PLATFORM_SYMBOLS={'__multf3','atan2l','expm1l','log2l'}
TRIG_MODEL='arm64-quad-true-pi-trig-v2'
MODELS=(QUAD_MODEL,TRIG_MODEL)
def model_flags(model):
    if model is None:return []
    if model==QUAD_MODEL:return list(QUAD_FLAGS)
    if model==TRIG_MODEL:return [*QUAD_FLAGS,'-DFNV_CPU_X87_QUAD_TRIG=1']
    raise ValueError('Unknown x87 numerical model')
def model_symbols(model):
    model_flags(model)
    return set() if model is None else QUAD_PLATFORM_SYMBOLS|({'sinl','cosl','tanl','__divtf3'} if model==TRIG_MODEL else set())
