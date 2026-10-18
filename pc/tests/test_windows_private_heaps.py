"""Private heaps through SDK-translated x86 IAT callers and real VM permissions."""
import argparse
from pathlib import Path
import test_windows_heap as shared


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--cc', nargs='+', required=True)
    shared.NAMES += ['HeapCreate', 'HeapDestroy']
    shared.ARGS += [3, 1]
    shared.test(parser.parse_args(), host_source='windows_private_heaps_host.c',
                extra_inputs=(Path(__file__).resolve(),),
                strict_modes=('--strict-create', '--broken-create'))
