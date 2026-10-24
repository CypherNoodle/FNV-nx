"""Translated x86 Bink movie imports backed by real decoding, PCM and frame copy."""
import argparse
from pathlib import Path
from test_bink_mixer import run

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','ffmpeg-source','ffmpeg-build','video'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--reference',type=Path);p.add_argument('--cc',nargs='+',required=True);p.add_argument('--x87-build',type=Path);p.add_argument('--frames',type=int,default=120)
    a=p.parse_args()
    if not 1<=a.frames<=3000:p.error('frames must be1..3000')
    run(a,movie=True)
