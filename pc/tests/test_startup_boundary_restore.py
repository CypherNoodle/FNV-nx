"""Reject missing/ambiguous diagnostic stops; preserve unrelated dispatch."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from advance_startup_boundary import remove_stop,validate_restored_body

stub='void sub_00401000(void){recomp_unimpl("Unselected startup target",0x401000u);}'
text='void unrelated(void){}\n'+stub+'\nstatic int sentinel=7;\n'
assert remove_stop(text,'sub_00401000',0x401000)=='void unrelated(void){}\n\nstatic int sentinel=7;\n'
checks=1
for value,symbol,address in [(text,'other',0x401000),(text,'sub_00401000',0x401001),
                            (stub+stub,'sub_00401000',0x401000),
                            (text.replace('Unselected startup target','different trap'),'sub_00401000',0x401000)]:
    try:remove_stop(value,symbol,address)
    except ValueError:checks+=1
    else:raise AssertionError('Missing or ambiguous stop accepted')
guard='void sub_00401000(void){if(!operation())RECOMP_UNIMPL("unsupported state",0x401001u);}'
validate_restored_body(guard);checks+=1
for bad in ('','void bad(void){/* translation failed */}','void bad(void){/* opcode: unhandled */}'):
    try:validate_restored_body(bad)
    except ValueError:checks+=1
    else:raise AssertionError('Incomplete translation accepted')
print('PASS',checks,'deliberate-boundary restoration checks')
