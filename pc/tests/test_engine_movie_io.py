"""Exercise own synthetic movie file I/O; no codec or sound playback."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def run(cc):
    pc=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='movie-io-',dir=Path.cwd()) as tmp:
        root=Path(tmp);video=root/'Data/Video';video.mkdir(parents=True)
        (video/'probe.bik').write_bytes(b'ABCDEFGH')
        exe=root/'movie.exe'
        subprocess.run([str(cc),'-std=gnu11','-Wall','-Wextra','-Werror',
                        '-I'+str(pc/'native'),'-I'+str(pc/'switch'),
                        str(pc/'switch/engine_movie_io.c'),str(pc/'tests/engine_movie_io_host.c'),
                        '-o',str(exe)],check=True)
        subprocess.run([str(exe),str(root)],check=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',type=Path,required=True)
    run(parser.parse_args().cc)
