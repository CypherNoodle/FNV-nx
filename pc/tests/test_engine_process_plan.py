"""Test the process blueprint in both CPU state ABIs, with no guest execution."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def run(cc):
    pc=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='engine-plan-',dir=Path.cwd()) as tmp:
        for extended in (False,True):
            exe=Path(tmp)/('extended.exe' if extended else 'legacy.exe')
            subprocess.run([str(cc),'-std=c11','-Wall','-Wextra','-Werror',
                            *(['-DFNV_CPU_EXTENDED_X87=1'] if extended else []),
                            '-I'+str(pc/'native'),'-I'+str(pc/'switch'),
                            str(pc/'switch/engine_layout.c'),str(pc/'switch/engine_process_plan.c'),
                            str(pc/'native/memory_horizon.c'),str(pc/'tests/engine_process_plan_host.c'),
                            '-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',type=Path,required=True)
    run(parser.parse_args().cc)
