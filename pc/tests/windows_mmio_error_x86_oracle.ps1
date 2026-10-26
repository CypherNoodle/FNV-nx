$ErrorActionPreference='Stop'
if ([IntPtr]::Size -ne 4) { throw 'Use x86 Windows PowerShell.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FnvMmioErrorOracle {
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate IntPtr IO(IntPtr info,uint msg,IntPtr a,IntPtr b);
    [DllImport("winmm.dll",CharSet=CharSet.Ansi)] static extern IntPtr mmioOpenA(string name,IntPtr info,uint flags);
    [DllImport("winmm.dll")] static extern uint mmioGetInfo(IntPtr handle,IntPtr info,uint flags);
    [DllImport("winmm.dll")] static extern uint mmioClose(IntPtr handle,uint flags);
    static uint observed;
    static IntPtr Callback(IntPtr info,uint msg,IntPtr a,IntPtr b) {
        if(msg==3){observed=(uint)Marshal.ReadInt32(info,12);return IntPtr.Zero;}
        if(msg==4)return IntPtr.Zero;
        return new IntPtr(-1);
    }
    public static void Run() {
        IntPtr input=Marshal.AllocHGlobal(72),output=Marshal.AllocHGlobal(72);
        IO proc=Callback;
        try {
            foreach(uint initial in new uint[]{0,1,257,0xdeadbeef}) {
                Marshal.Copy(new byte[72],0,input,72);
                Marshal.WriteIntPtr(input,8,Marshal.GetFunctionPointerForDelegate(proc));
                Marshal.WriteInt32(input,12,unchecked((int)initial));
                IntPtr h=mmioOpenA("synthetic",input,0x10000);
                if(h==IntPtr.Zero)throw new Exception("Open failed");
                try {
                    if(observed!=0 || Marshal.ReadInt32(input,12)!=0 || mmioGetInfo(h,output,0)!=0 || Marshal.ReadInt32(output,12)!=0)
                        throw new Exception("wErrorRet does not match expected output semantics");
                } finally {if(mmioClose(h,0)!=0)throw new Exception("Close failed");}
            }
            GC.KeepAlive(proc);
        } finally {Marshal.FreeHGlobal(input);Marshal.FreeHGlobal(output);}
    }
}
'@
[FnvMmioErrorOracle]::Run()
'{"status":"PASS","x86_windows_open_error_cases":4}'
