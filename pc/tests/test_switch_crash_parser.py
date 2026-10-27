"""Own synthetic report: do not confuse another thread PC with crashed PC."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from symbolize_switch_crash import parse_report
text='PC: 100020 (fnv-nx + 0x20)\nLR: 100010 (fnv-nx + 0x10)\nModule Id: '+'ab'*32+'\nThread Report:\nPC: 100099 (fnv-nx + 0x99)'
identity,rows=parse_report(text)
assert identity=='ab'*32 and [r['offset'] for r in rows]==[32,16]
for bad in ('',text.replace('Module Id:','Other:'),text.replace('PC:','Other:')):
    try:parse_report(bad)
    except ValueError:pass
    else:raise AssertionError('Malformed report accepted')
print('PASS crash module identity and first-thread offset parsing')
