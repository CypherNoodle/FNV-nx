param([Parameter(Mandatory=$true)][string]$DllPath)
$ErrorActionPreference='Stop'
if([IntPtr]::Size -ne 4){throw 'Use x86 PowerShell'}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BinkSoundOracle {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryExW(string p,IntPtr f,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr d,string n);
 [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr d);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int SetSound(IntPtr callback,uint param);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate IntPtr Factory(uint param);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int SoundOpen(IntPtr snd,uint rate,int bits,int channels,uint flags,IntPtr bink);
 static uint last;static int calls;static bool accept;
 static int Open(IntPtr snd,uint rate,int bits,int channels,uint flags,IntPtr bink){throw new Exception("Unexpected sound open");}
 static SoundOpen sound=Open;
 static IntPtr Make(uint param){calls++;last=param;return accept?Marshal.GetFunctionPointerForDelegate(sound):IntPtr.Zero;}
 public static void Run(string path){
  IntPtr dll=LoadLibraryExW(path,IntPtr.Zero,8);if(dll==IntPtr.Zero)throw new Exception("Load failed "+Marshal.GetLastWin32Error());
  SetSound set=(SetSound)Marshal.GetDelegateForFunctionPointer(GetProcAddress(dll,"_BinkSetSoundSystem@8"),typeof(SetSound));
  Factory factory=Make;
  foreach(bool ok in new[]{false,true,false,true}){
   accept=ok;calls=0;last=0;uint p=ok?0x12345678u:0xabcdef12u;int r=set(Marshal.GetFunctionPointerForDelegate(factory),p);
   Console.WriteLine("accept="+ok+" return="+r+" calls="+calls+" param="+last);
  }
  calls=0;Console.WriteLine("null return="+set(IntPtr.Zero,0)+" calls="+calls);
  uint seed=0x781204u;bool selected=false;
  for(int i=0;i<2000;i++){
   seed=unchecked(seed*1664525u+1013904223u);accept=(seed&16)!=0;calls=0;last=0;
   int r=set(Marshal.GetFunctionPointerForDelegate(factory),seed);selected=selected||accept;
   if(r!=(selected?1:0)||calls!=1||last!=seed)throw new Exception("Native sound-selection mismatch");
  }
  if(set(IntPtr.Zero,0)!=0)throw new Exception("Native reset mismatch");
  Console.WriteLine("{\"status\":\"PASS\",\"native_sound_selection_calls\":2004,\"decoded\":false}");
  GC.KeepAlive(factory);GC.KeepAlive(sound);FreeLibrary(dll);
 }
}
'@
[BinkSoundOracle]::Run($DllPath)
