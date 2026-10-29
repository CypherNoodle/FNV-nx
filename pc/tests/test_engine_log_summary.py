"""Synthetic counter tests: missing snapshots, session resets and CPU queries."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from summarize_engine_log import summarize

scratch=Path(__file__).resolve().parents[2]/'local';scratch.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='fnv-log-metrics-',dir=scratch) as tmp:
    path=Path(tmp)/'test.log'
    path.write_text('Progress elapsed_ms=1000 draws=1 submissions=2 presents=1 gpu_alloc=1\n'
                    'Heartbeat elapsed_ms=2000 fence_calls=1 fence_ms=0 submit_calls=2 submit_ms=0\n'
                    'Progress elapsed_ms=4000 draws=7 submissions=8 presents=4 gpu_alloc=1\n'
                    'Progress elapsed_ms=1 draws=0 submissions=0 presents=0 gpu_alloc=1\n'
                    'Progress elapsed_ms=1001 draws=2 submissions=3 presents=2 gpu_alloc=1\n')
    r=summarize(path)
    assert len(r['intervals'])==2 and r['intervals'][0]['duration_ms']==3000
    assert r['intervals'][0]['presentations_per_second']==1
    assert r['intervals'][1]['presentations_per_second']==2
    text=('Heartbeat elapsed_ms=1000 fence_calls=1 fence_ms=0 submit_calls=1 submit_ms=0\n'
          'CPU main_ticks_rc=00000000 affinity=7 ticks_hz=1000 core0=10 core1=20 core2=30 core3=0\n'
          'Audio pumps=1 frames=512\n'
          'Heartbeat elapsed_ms=2000 fence_calls=2 fence_ms=1 submit_calls=2 submit_ms=1\n'
          'CPU main_ticks_rc=00000000 affinity=7 ticks_hz=1000 core0=210 core1=320 core2=130 core3=0\n'
          'Audio pumps=2 frames=1024\n')
    path.write_text(text);r=summarize(path)
    assert r['main_thread_cpu_intervals'][0]['main_thread_percent']==60
    assert r['main_thread_cpu_intervals'][0]['main_thread_percent_by_core']['core1']==30
    assert r['metric_samples'][1]['groups']['Audio']['frames']==1024
    for malformed in (text.replace('main_ticks_rc=00000000','main_ticks_rc=0000CE01'),
                      text.replace('core0=210','core0=0'),
                      text.replace('affinity=7 ticks_hz=1000 core0=210','affinity=3 ticks_hz=1000 core0=210'),
                      text.replace('ticks_hz=1000 core0=210','ticks_hz=100 core0=210')):
        path.write_text(malformed);assert not summarize(path)['main_thread_cpu_intervals']
print('PASS: missing snapshots, session resets, per-core rates and four invalid CPU-query cases')
