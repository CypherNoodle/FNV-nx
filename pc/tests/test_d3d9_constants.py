"""Windows NULLREF oracle plus preserved PC constants/shader inputs, no game run."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from shader_packages import parse_package

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def test(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh private directory')
    paths=[Path(__file__).resolve(),pc/'tests/d3d9_constants_host.c',pc/'native/d3d9_constants.c',
        pc/'native/d3d9_constants.h',pc/'native/host_mutex.h',pc/'tools/shader_packages.py',a.capture.resolve(),
        *sorted(a.shaders.resolve().glob('*.sdp'))]
    frozen={p:sha(p) for p in paths};events=[];draw_calls=[]
    for line in a.capture.read_text().splitlines():
        match=re.match(r'(\d+) IDirect3DDevice9::Set(Vertex|Pixel)ShaderConstantF\(.*StartRegister = (\d+), pConstantData = \{([^}]+)\}, Vector4fCount = (\d+)\) = D3D_OK',line)
        if match:
            call,stage,first,data,count=match.groups();count=int(count);values=[float(x) for x in data.split(',')]
            if len(values)!=count*4:raise ValueError('Invalid captured constants')
            events.append(struct.pack('<4I',0,int(stage=='Pixel'),int(first),count)+struct.pack('<'+'f'*len(values),*values))
        elif re.match(r'\d+ IDirect3DDevice9::Draw(?:Indexed)?Primitive\(',line):
            call=int(line.split()[0]);draw_calls.append(call);events.append(struct.pack('<4I',1,call,0,0))
        elif re.search(r'::(?:Reset|Apply|Capture|BeginStateBlock|EndStateBlock)\(',line):
            raise ValueError('State-block/reset capture needs full reconstruction')
    if not draw_calls or not events:raise ValueError('No constant/draw events')
    unique={}
    for p in sorted(a.shaders.glob('*.sdp')):
        for record in parse_package(p.read_bytes()):unique.setdefault(record['sha256'],record)
    if not unique:raise ValueError('No shaders')
    out.mkdir(parents=True)
    capture=out/'constant-events.bin';capture.write_bytes(b'FNV9CST1'+struct.pack('<I',len(events))+b''.join(events))
    catalog=out/'shader-events.bin'
    with catalog.open('wb') as f:
        f.write(b'FNV9SHD1'+struct.pack('<I',len(unique)))
        for h in sorted(unique):
            r=unique[h];f.write(struct.pack('<2I',int(r['stage']=='pixel'),r['bytes'])+r['code'])
    exe=out/'constants-test.exe'
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),
        str(pc/'native/d3d9_constants.c'),str(pc/'tests/d3d9_constants_host.c'),'-ld3d9','-luser32','-o',str(exe)]
    built=subprocess.run(command,capture_output=True,text=True);(out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    run=subprocess.run([str(exe),str(capture),str(catalog)],capture_output=True,text=True,timeout=60)
    (out/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(f'Oracle exit {run.returncode}: '+run.stdout+run.stderr)
    result=json.loads(run.stdout.strip().splitlines()[-1])
    assert result['native_shader_roundtrips']==len(unique) and result['captured_draw_boundaries']==len(draw_calls)
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Inputs changed')
    result.update(source_sha256={str(p):h for p,h in frozen.items()},exe_sha256=sha(exe),
        capture_draw_calls=draw_calls,capture_constants_are_text_rounded=True,
        gpu_shader_translation_validated=False,full_draw_state_replayed=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('source_sha256','capture_draw_calls')},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('output','capture','shaders'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())
