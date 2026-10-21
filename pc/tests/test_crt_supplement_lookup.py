"""Supplement dispatch ordering without overriding any frozen target."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from probe_crt_table_entries import extend_lookup

original='static const struct {unsigned at;void (*function)(void);} probe_functions[]={{0x10u,sub_A},{0x30u,sub_C},};\n'
new=extend_lookup(original,{0x20:'sub_B',0x40:'sub_D'})
assert new.startswith('void sub_B(void);\nvoid sub_D(void);\n')
assert 'probe_functions[]={{0x10u,sub_A},{0x20u,sub_B},{0x30u,sub_C},{0x40u,sub_D},};' in new
checks=1
for text,entries in [(original,{}),(original,{0x10:'override'}),('',{0x20:'sub_B'}),
        (original.replace('0x30u','0x10u'),{0x20:'sub_B'}),
        (original.replace('sub_A},','sub_A},garbage'),{0x20:'sub_B'})]:
    try:extend_lookup(text,entries)
    except ValueError:checks+=1
    else:raise AssertionError('Malformed/overriding lookup accepted')
assert original.endswith('sub_C},};\n')
print('PASS',checks,'supplemental dispatch checks; original text unchanged')
