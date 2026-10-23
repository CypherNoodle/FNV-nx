param([Parameter(Mandatory=$true)][string]$InputPath,[Parameter(Mandatory=$true)][string]$OutputPath,[switch]$Transpose)
$ErrorActionPreference='Stop'
if ([IntPtr]::Size -ne 4) { throw 'Run with SysWOW64 Windows PowerShell (x86).' }
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class FnvD3dxMatrixOracle {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryExW(string name, IntPtr file, uint flags);
    [DllImport("d3dx9_38.dll", CallingConvention=CallingConvention.StdCall)]
    static extern IntPtr D3DXMatrixMultiply([Out] float[] result, [In] float[] a, [In] float[] b);
    [DllImport("d3dx9_38.dll", CallingConvention=CallingConvention.StdCall)]
    static extern IntPtr D3DXMatrixMultiplyTranspose([Out] float[] result, [In] float[] a, [In] float[] b);
    public static void Run(string input,string output,bool transpose) {
        if(LoadLibraryExW("d3dx9_38.dll",IntPtr.Zero,0x800)==IntPtr.Zero) throw new Exception("System D3DX unavailable");
        using(var reader=new BinaryReader(File.OpenRead(input))) {
            uint count=reader.ReadUInt32();
            if(count==0 || count>100000 || reader.BaseStream.Length!=4L+128L*count) throw new Exception("Invalid input shape");
            using(var writer=new BinaryWriter(new FileStream(output,FileMode.CreateNew))) {
                writer.Write(count);
                for(uint n=0;n<count;n++) {
                    var a=new float[16];var b=new float[16];var result=new float[16];
                    for(int i=0;i<16;i++) a[i]=reader.ReadSingle();
                    for(int i=0;i<16;i++) b[i]=reader.ReadSingle();
                    if((transpose?D3DXMatrixMultiplyTranspose(result,a,b):D3DXMatrixMultiply(result,a,b))==IntPtr.Zero) throw new Exception("Matrix multiply failed");
                    foreach(float value in a) writer.Write(value);
                    foreach(float value in b) writer.Write(value);
                    foreach(float value in result) writer.Write(value);
                }
            }
        }
    }
}
'@
[FnvD3dxMatrixOracle]::Run($InputPath,$OutputPath,$Transpose.IsPresent)
