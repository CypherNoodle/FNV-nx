param([Parameter(Mandatory=$true)][string]$InputPath,[Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
if ([IntPtr]::Size -ne 4) { throw 'Use x86 Windows PowerShell.' }
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class FnvMathOracle {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern IntPtr LoadLibraryExW(string path,IntPtr file,uint flags);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXMatrixTranspose([Out]float[] o,float[] m);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXMatrixInverse([Out]float[] o,out float determinant,float[] m);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXVec4Transform([Out]float[] o,float[] v,float[] m);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXVec3TransformCoord([Out]float[] o,float[] v,float[] m);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXVec3TransformNormal([Out]float[] o,float[] v,float[] m);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXPlaneTransform([Out]float[] o,float[] v,float[] m);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXVec3Normalize([Out]float[] o,float[] v);
    [DllImport("d3dx9_38.dll")]static extern IntPtr D3DXPlaneNormalize([Out]float[] o,float[] v);
    public static void Run(string input,string output) {
        if(LoadLibraryExW("d3dx9_38.dll",IntPtr.Zero,0x800)==IntPtr.Zero)throw new Exception("Missing D3DX");
        using(var r=new BinaryReader(File.OpenRead(input))) {
            uint count=r.ReadUInt32();if(count!=1000 || r.BaseStream.Length!=4L+80L*count)throw new Exception("Input shape");
            using(var w=new BinaryWriter(new FileStream(output,FileMode.CreateNew))) {
                w.Write(count);
                for(uint j=0;j<count;j++) {
                    var m=new float[16];var v=new float[4];for(int i=0;i<16;i++){m[i]=r.ReadSingle();w.Write(m[i]);}for(int i=0;i<4;i++){v[i]=r.ReadSingle();w.Write(v[i]);}
                    for(int op=0;op<8;op++) {
                        var o=new float[16];float det=99;IntPtr p;
                        switch(op) {
                        case 0:p=D3DXMatrixTranspose(o,m);break;
                        case 1:p=D3DXMatrixInverse(o,out det,j%97==0?new float[16]:m);break;
                        case 2:p=D3DXVec4Transform(o,v,m);break;
                        case 3:p=D3DXVec3TransformCoord(o,v,m);break;
                        case 4:p=D3DXVec3TransformNormal(o,v,m);break;
                        case 5:p=D3DXPlaneTransform(o,v,m);break;
                        case 6:p=D3DXVec3Normalize(o,v);break;
                        default:p=D3DXPlaneNormalize(o,v);break;
                        }
                        foreach(float value in o)w.Write(value);w.Write(det);w.Write(p==IntPtr.Zero?0u:1u);
                    }
                }
            }
        }
    }
}
'@
[FnvMathOracle]::Run($InputPath,$OutputPath)
