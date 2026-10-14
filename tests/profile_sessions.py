"""Regression: appended old logs must not contaminate the current profile."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_switch_profile import analyze

old = ('[old] FNV INI applied: fnv_fixed_render_mode=true\n[FATAL] old crash\n'
       'FNV GPU sample: submission=16 groups=2 draw_ms=999.0 resolve_ms=1.0 other_ms=0.0 total_ms=1000.0\n')
current = ('[new] FNV INI applied: fnv_fixed_render_mode=true\n'
           'FNV GPU sample: submission=32 groups=2 draw_ms=9.0 resolve_ms=1.0 other_ms=0.0 total_ms=10.0\n')
profile = ('image 0x0001, code up to 0x0002\n'
           '==== 10.0 s | mode: normal | game 99.0 fps | CPU total 50%\n'
           'image 0x0003, code up to 0x0004\n'
           '==== 10.0 s | mode: normal | game 3.0 fps | CPU total 100%\n'
           '     clocks: CPU 1020.0 MHz, GPU 307.2 MHz, memory 1331.2 MHz\n'
           '     FNV depth exports: fbo_float24=5 fbo_fixed24=2 fsi=0 half_scale_fix=3\n'
           '     FNV render topology: legacy=1 dynamic=2 barriers=5 broad=0 memory_items=0 buffer_items=3 image_items=2 state_updates=10 target_state_changes=4 fsi=0 fbo=10 | passes/frame=1.00 barriers/frame=1.67 changes/frame=1.33\n')
data=analyze(profile,old+current)
assert data['application_sessions_detected']==data['profile_sessions_detected']==2
assert data['fatal_lines']==[] and len(data['gpu_samples'])==1
assert data['gpu_samples'][0]['draw_ms']==9 and len(data['intervals'])==1
assert data['intervals'][0]['fps']==3 and data['intervals'][0]['clocks_mhz']['gpu']==307.2
assert data['intervals'][0]['render_topology']['fbo']==10
assert data['intervals'][0]['render_topology']['barriers_per_frame']==1.67
assert data['intervals'][0]['depth_exports']==dict(fbo_float24=5,fbo_fixed24=2,fsi=0,half_scale_fix=3)
assert analyze('', '[FATAL] unmarked crash')['fatal_lines']==['[FATAL] unmarked crash']
print('PASS latest marked sessions, no old crash/GPU contamination, clock metadata, unmarked compatibility')
