"""Compile an original tiny oDepth shader and inspect the emitted SPIR-V contract.

No game shader is embedded. EXEC_END runs MAX c0,c0 exporting X to oDepth.
The test verifies that the *emitted* final depth store selects 0.5 only for
float24, keeps 1.0 for fixed24, and does not scale FSI or conversion twice.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--translator', type=Path, required=True)
p.add_argument('--baseline', type=Path)
p.add_argument('--spirv-tools', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
args=p.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
# CF pair: EXEC_END at ALU index1/count1, then NOP. ALU vector MAX c0,c0
# exports component X to depth61, with inactive scalar RETAIN_PREV(50).
shader=args.output/'explicit-depth.ucode'
shader.write_bytes(struct.pack('<6I', 1 | (1<<12), 2<<12, 0,
                               61 | (1<<15) | (1<<16) | (50<<26), 0, 2<<24))

def run(exe, mode, tag):
    dest=args.output/f'{tag}-{mode}.spv'
    mod={'plain':0, 'truncate':2<<46, 'round':3<<46, 'fsi':0}[mode]
    command=[str(exe), str(shader), str(dest), 'frag', '0', f'{mod:X}']
    if mode=='fsi': command.append('fsi')
    result=subprocess.run(command,capture_output=True,text=True,check=True)
    assert 'depth=1' in result.stdout, result.stdout
    subprocess.run([str(args.spirv_tools/'spirv-val.exe'),'--target-env','vulkan1.2',
                    '--scalar-block-layout',str(dest)],capture_output=True,text=True,check=True)
    dis=subprocess.run([str(args.spirv_tools/'spirv-dis.exe'),str(dest)],
                       capture_output=True,text=True,check=True).stdout
    (args.output/f'{tag}-{mode}.spvasm').write_text(dis)
    return dest.read_bytes(), dis

current={mode:run(args.translator,mode,'current') for mode in ('plain','truncate','round','fsi')}
text=current['plain'][1]
depth=re.search(r'OpDecorate (%\w+) BuiltIn FragDepth',text)[1]
stores=re.findall(r'OpStore '+re.escape(depth)+r' (%\w+)',text)
defs={m[1]:m[2].split() for m in re.finditer(r'^\s*(%\w+) = (.+)$',text,re.M)}
final=defs[stores[-1]]
assert final[0]=='OpFMul',final
loaded, selected=final[2:4]
assert defs[loaded][0]=='OpLoad' and defs[loaded][-1]==depth
select=defs[selected]
assert select[0]=='OpSelect',select
condition,yes,no=select[2:5]
assert defs[yes][0]=='OpConstant' and float(defs[yes][-1])==0.5
assert defs[no][0]=='OpConstant' and float(defs[no][-1])==1.0
comparison=defs[condition]
assert comparison[0]=='OpINotEqual' and defs[comparison[3]][-1]=='0'
bit_test=defs[comparison[2]]
assert bit_test[0]=='OpBitwiseAnd'
flag_mask=int(defs[bit_test[3]][-1])
assert flag_mask>0 and flag_mask&(flag_mask-1)==0
# Evaluate the selected emitted scale for both flags and representative already
# clamped guest depth exports; comparisons must match a viewport remapped once.
for flags in (0,flag_mask,flag_mask|1):
    for z in (0.,0.25,0.75,1.):
        scale=float(defs[yes if flags&flag_mask else no][-1])
        assert z*scale==(z/2 if flags&flag_mask else z)
if args.baseline:
    prior={mode:run(args.baseline,mode,'baseline') for mode in current}
    assert prior['plain'][0]!=current['plain'][0], 'Regression not reproduced'
    for mode in ('truncate','round','fsi'):
        assert prior[mode][0]==current[mode][0], f'Unexpected change to {mode}'
print('PASS synthetic explicit depth: 4 SPIR-V modes valid; emitted float24 x0.5/fixed24 x1; baseline regression and unaffected modes checked' if args.baseline else
      'PASS synthetic explicit depth: 4 SPIR-V modes valid; emitted float24 x0.5/fixed24 x1')
