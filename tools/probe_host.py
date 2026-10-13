"""Bounded boot probe of the Xbox guest code, with writable data in workspace."""
from pathlib import Path
import subprocess,time,json,hashlib,datetime,argparse,ctypes,os
parser=argparse.ArgumentParser()
parser.add_argument('--single-core',action='store_true',help='Diagnostic affinity for this owned process only')
parser.add_argument('--startup-barrier',action='store_true',help='Experimental ordering of the guest worker and object constructor')
parser.add_argument('--barrier-phase',choices=['constructor','bootstrap','pre-loop'],default='constructor')
parser.add_argument('--visible',action='store_true',help='Enable SDK presentation for guest-only frame capture')
parser.add_argument('--test-pad',action='store_true',help='Opt-in synthetic guest pad: START at 8 s and A at 14 s')
parser.add_argument('--seconds',type=int,default=45,help='Owned process deadline, 20 to 120 seconds')
parser.add_argument("--game-root",type=Path,required=True)
parser.add_argument("--exe",type=Path,default=Path(__file__).resolve().parents[1]/"out/host/fnv_xbox.exe")
args=parser.parse_args()
assert 20<=args.seconds<=120,'Probe deadline must be between 20 and 120 seconds'
HERE=Path(__file__).resolve().parents[1]/'local'
HERE.mkdir(exist_ok=True);ROOT=HERE
exe=args.exe.resolve();assert exe.is_file(),'Build the Windows reference first'
build={'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}
game=args.game_root.resolve();assert (game/'default.xex').is_file()
for name in ['user-data','cache','metadata','logs','reports']:(HERE/name).mkdir(exist_ok=True)
probe_id=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
runtime_log=HERE/('logs/host-boot-'+probe_id+'.log');console_log=ROOT/'reports/fnv-xbox-host-boot-console.log'
command=[str(exe),'--game_data_root='+str(game),'--user_data_root='+str(HERE/'user-data'),
 '--cache_root='+str(HERE/'cache'),'--metadata_root='+str(HERE/'metadata'),
 '--log_file='+str(runtime_log),'--headless='+('false' if args.visible else 'true'),'--allow_game_relative_writes=false']
start=time.monotonic();timed_out=False
affinity={'requested':args.single_core,'applied':False}
observations={name:(exe.parent/name).stat().st_size if (exe.parent/name).exists() else 0
              for name in ['fnv-host-boot-trace.txt','fnv-host-crash.txt','fnv-host-frame-watch.txt','fnv-host-test-pad.txt']}
with console_log.open('w',encoding='utf-8') as output:
 environment=os.environ.copy()
 environment['FNV_XBOX_STARTUP_BARRIER']={'constructor':'1','bootstrap':'2','pre-loop':'3'}[args.barrier_phase] if args.startup_barrier else '0'
 environment['FNV_XBOX_TEST_PAD']='1' if args.test_pad else '0'
 process=subprocess.Popen(command,cwd=exe.parent,stdout=output,stderr=subprocess.STDOUT,env=environment)
 if args.single_core:
  kernel=ctypes.WinDLL('kernel32',use_last_error=True)
  kernel.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32];kernel.OpenProcess.restype=ctypes.c_void_p
  kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
  kernel.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
  kernel.CloseHandle.argtypes=[ctypes.c_void_p]
  handle=kernel.OpenProcess(0x0200|0x0400,False,process.pid)
  allowed=ctypes.c_size_t();system=ctypes.c_size_t()
  if handle:
   if kernel.GetProcessAffinityMask(handle,ctypes.byref(allowed),ctypes.byref(system)) and allowed.value:
    mask=allowed.value & -allowed.value
    affinity.update(applied=bool(kernel.SetProcessAffinityMask(handle,mask)),mask=hex(mask))
   kernel.CloseHandle(handle)
  if not affinity['applied']:affinity['error']=ctypes.get_last_error()
 try:code=process.wait(timeout=args.seconds)
 except subprocess.TimeoutExpired:
  timed_out=True;process.terminate();code=process.wait(timeout=15)
snapshots={}
for name,offset in observations.items():
 source=exe.parent/name
 if source.exists():
  destination=HERE/'logs'/(probe_id+'-'+name)
  destination.write_bytes(source.read_bytes()[offset:])
  snapshots[name]=str(destination)
report={'result':'OBSERVATION_ONLY','command':command,'exit_code':code,
 'exit_hex':hex(code&0xffffffff),'elapsed_seconds':round(time.monotonic()-start,3),
 'terminated_owned_process_after_timeout':timed_out,'runtime_log':str(runtime_log),
 'console_log':str(console_log),'exe_sha256':build['exe_sha256'],
 'guest_boot_verified':False,'menu_verified':False,'switch_executed':False,
 'diagnostic_snapshots':snapshots,'process_affinity_experiment':affinity,
 'experimental_startup_barrier':args.startup_barrier,
 'barrier_phase':args.barrier_phase if args.startup_barrier else None,
 'presentation_requested':args.visible,'synthetic_test_pad':args.test_pad,'deadline_seconds':args.seconds,
 'guest_capture_files':[str(path) for path in exe.parent.glob(f'fnv-host-guest-{process.pid}-*.ppm')]}
(ROOT/'reports/fnv-xbox-host-boot.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
