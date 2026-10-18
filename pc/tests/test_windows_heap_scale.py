"""Large guest heap workloads and first-fit equivalence through translated IAT."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    shared.test(p.parse_args(), host_source='windows_heap_scale_host.c',
                extra_inputs=(Path(__file__).resolve(),), strict_modes=())
