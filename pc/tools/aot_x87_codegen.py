"""Opt-in extended-x87 codegen driver; no SDK edits and no engine boot claim."""
import argparse
import json
from pathlib import Path
import runpy
import sys

PROFILE_KEYS={'schema','contract_status','code_selector','data_selector','stack_selector','opcode_compatibility','data_pointer_on_exception_only'}

def load_profile(path):
    def unique(pairs):
        result={}
        for key,value in pairs:
            if key in result:raise ValueError('Duplicate x87 profile key: '+key)
            result[key]=value
        return result
    p=json.loads(path.read_text(),object_pairs_hook=unique)
    if set(p)!=PROFILE_KEYS or p['schema']!='fnv-x87-protected32-v1' or p['contract_status']!='experimental':
        raise ValueError('Require an explicit experimental protected32 x87 profile')
    for key in ('code_selector','data_selector','stack_selector'):
        if type(p[key]) is not int or not 0<=p[key]<=0xffff:raise ValueError('Invalid guest selector: '+key)
    for key in ('opcode_compatibility','data_pointer_on_exception_only'):
        if type(p[key]) is not bool:raise ValueError('Explicit boolean guest policy required: '+key)
    return p


def install_profile(profile):
    from aot_x87_extended import install,install_environment
    install();install_environment(**{key:profile[key] for key in ('code_selector','data_selector','stack_selector','opcode_compatibility','data_pointer_on_exception_only')})
    from aot_sahf_flags import install as sahf
    sahf()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('sdk','analysis','x87-profile'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--eh3-helpers',type=Path);p.add_argument('--simd-bits',action='store_true');p.add_argument('--mul-flags',action='store_true')
    a,remaining=p.parse_known_args();profile=load_profile(a.x87_profile);sys.path.insert(0,str(a.sdk.resolve()))
    if a.simd_bits:
        from aot_simd_bits import install
        install()
    if a.mul_flags:
        from aot_mul_flags import install
        install()
    from aot_frame_restore import install as frame
    frame()
    if a.eh3_helpers:
        from audit_eh3_helpers import verify
        from aot_eh3_frames import install as eh3
        eh3(verify(a.analysis.resolve(),json.loads(a.eh3_helpers.read_text())))
    install_profile(profile)
    sys.argv=['tools.recomp',*remaining];runpy.run_module('tools.recomp',run_name='__main__')
