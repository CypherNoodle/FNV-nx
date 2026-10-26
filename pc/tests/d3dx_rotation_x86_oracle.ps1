param([Parameter(Mandatory=$true)][string]$InputPath,[Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
if ([IntPtr]::Size -ne 4) { throw 'Use x86 Windows PowerShell.' }
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class FnvRotationOracle {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]
    static extern IntPtr LoadLibraryExW(string name,IntPtr file,uint flags);
    [DllImport("d3dx9_38.dll",CallingConvention=CallingConvention.StdCall)]
    static extern IntPtr D3DXMatrixRotationYawPitchRoll([Out]float[] result,float yaw,float pitch,float roll);
    public static void Run(string input,string output) {
        if(LoadLibraryExW("d3dx9_38.dll",IntPtr.Zero,0x800)==IntPtr.Zero)throw new Exception("Missing system D3DX");
        using(var r=new BinaryReader(File.OpenRead(input))) {
            uint count=r.ReadUInt32();if(count!=5000 || r.BaseStream.Length!=4L+12L*count)throw new Exception("Input shape");
            using(var w=new BinaryWriter(new FileStream(output,FileMode.CreateNew))) {
                w.Write(count);
                for(uint i=0;i<count;i++) {
                    float yaw=r.ReadSingle(),pitch=r.ReadSingle(),roll=r.ReadSingle();var matrix=new float[16];
                    if(D3DXMatrixRotationYawPitchRoll(matrix,yaw,pitch,roll)==IntPtr.Zero)throw new Exception("Rotation failed");
                    w.Write(yaw);w.Write(pitch);w.Write(roll);foreach(float value in matrix)w.Write(value);
                }
            }
        }
    }
}
'@
[FnvRotationOracle]::Run($InputPath,$OutputPath)
