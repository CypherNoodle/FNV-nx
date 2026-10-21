"""Exercise the bounded Shell32 LocalAppData bridge and port-root mapping."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['SHGetFolderPathA','CreateFileA','WriteFile','FlushFileBuffers','SetFilePointerEx','SetEndOfFile','CreateDirectoryA','ReadFile','CloseHandle']
    shared.ARGS=[5,7,5,1,5,1,2,5,1];shared.DLLS=['SHELL32.dll','KERNEL32.dll','KERNEL32.dll','KERNEL32.dll','KERNEL32.dll','KERNEL32.dll','KERNEL32.dll','KERNEL32.dll','KERNEL32.dll']
    shared.test(p.parse_args(),host_source='windows_shell_host.c',
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_shell.h',
                      Path(__file__).resolve().parents[1]/'native/windows_shell.inc'),
        strict_modes=('--strict-csidl','--strict-flags','--strict-hwnd','--strict-token','--strict-output'),wrapper_stride=64)
