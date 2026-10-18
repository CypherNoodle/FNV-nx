"""Fetch pinned upstream CC0 headers for an external build dependency, never SD assets."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

REVISION='ea99364f61c14656440e8d77e9c233ccf3124633'
FILES={'minimp3.h':'57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad',
       'minimp3_ex.h':'8437f3fc1d4d8ab2269a1624f5380a08df1967a048f8887789bae6e25db7db79',
       'LICENSE':'6a1ee543e5282cd9061881edf462e6fdab181f328da71fc2c9a6950a80e94d01'}


def verify(root):
    for name,digest in FILES.items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=digest:raise ValueError('Dependency mismatch: '+name)
    return dict(repository='https://github.com/lieff/minimp3',revision=REVISION,license='CC0-1.0',sha256=FILES)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():print(json.dumps(verify(a.output),indent=2))
    else:
        data={name:urllib.request.urlopen('https://raw.githubusercontent.com/lieff/minimp3/'+REVISION+'/'+name,timeout=30).read() for name in FILES}
        if any(hashlib.sha256(data[n]).hexdigest()!=h for n,h in FILES.items()):raise ValueError('Download identity mismatch')
        a.output.mkdir(parents=True)
        for name,value in data.items():(a.output/name).write_bytes(value)
        print(json.dumps(verify(a.output),indent=2))
