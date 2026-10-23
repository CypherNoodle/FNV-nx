"""Verify the pinned public stb_dxt header, or fetch it only when missing."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import urllib.request


def verify(fetch=False):
    root=Path(__file__).resolve().parents[1]/'third_party/stb_dxt'
    metadata=json.loads((root/'provenance.json').read_text())
    commit=metadata['commit'];digest=metadata['sha256']
    if not re.fullmatch('[0-9a-f]{40}',commit) or not re.fullmatch('[0-9a-f]{64}',digest):
        raise ValueError('Invalid pinned identity')
    url='https://raw.githubusercontent.com/nothings/stb/'+commit+'/stb_dxt.h'
    if metadata['url']!=url or metadata['license']!='MIT OR Unlicense' or metadata['modified'] is not False:
        raise ValueError('Unexpected dependency provenance')
    target=root/'stb_dxt.h'
    if target.exists():
        data=target.read_bytes()
    elif fetch:
        request=urllib.request.Request(url,headers={'User-Agent':'FNV-nx-dependency-verification'})
        with urllib.request.urlopen(request,timeout=30) as response:data=response.read(100001)
        if len(data)>100000 or hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Downloaded source identity differs')
        with target.open('xb') as stream:stream.write(data)
    else:
        raise FileNotFoundError('Pinned header missing; rerun with --fetch')
    if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Pinned header has changed; refusing to overwrite it')
    print('PASS stb_dxt '+commit+' '+digest)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fetch',action='store_true',help='Download the pinned public header only if absent')
    verify(p.parse_args().fetch)
