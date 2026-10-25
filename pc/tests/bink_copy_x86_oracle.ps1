param([Parameter(Mandatory=$true)][string]$DllPath,[Parameter(Mandatory=$true)][string]$VideoPath)
$ErrorActionPreference='Stop'
if([IntPtr]::Size -ne 4){throw 'Use x86 PowerShell'}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BinkCopyOracle {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryExW(string p,IntPtr f,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr d,string n);
 [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr d);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int SetSound(IntPtr p,uint n);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate IntPtr Open([MarshalAs(UnmanagedType.LPStr)] string p,uint n);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate void Close(IntPtr p);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate void Next(IntPtr p);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int Decode(IntPtr p);
 [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int Copy(IntPtr h,IntPtr d,int pitch,uint height,uint dx,uint dy,uint sx,uint sy,uint w,uint rows,uint flags);
 static T Get<T>(IntPtr d,string n){IntPtr p=GetProcAddress(d,n);if(p==IntPtr.Zero)throw new Exception(n);return (T)(object)Marshal.GetDelegateForFunctionPointer(p,typeof(T));}
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 static byte[] Pixels(Copy copy,IntPtr h,int width,int height,uint sx,uint sy,uint sw,uint sh,uint dx,uint dy,uint flags,out int result){
  int pitch=width*4+32,bytes=pitch*height;byte[] data=new byte[bytes+128];for(int i=0;i<data.Length;i++)data[i]=0xa5;
  IntPtr p=Marshal.AllocHGlobal(data.Length);
  try{Marshal.Copy(data,0,p,data.Length);result=copy(h,IntPtr.Add(p,64),pitch,(uint)height,dx,dy,sx,sy,sw,sh,flags);Marshal.Copy(p,data,0,data.Length);
   for(int i=0;i<data.Length;i++){
    int at=i-64,row=at<0?-1:at/pitch,col=at<0?-1:at%pitch;
    bool written=row>=(int)dy&&row<(int)(dy+sh)&&col>=(int)dx*4&&col<(int)(dx+sw)*4;
    if(!written)Check(data[i]==0xa5,"Copy touched padding/guard at "+i);
   }
   byte[] pixels=new byte[sw*sh*4];for(int y=0;y<(int)sh;y++)Array.Copy(data,64+((int)dy+y)*pitch+(int)dx*4,pixels,y*(int)sw*4,(int)sw*4);return pixels;
  }finally{Marshal.FreeHGlobal(p);}
 }
 public static void Run(string dllPath,string video){
  IntPtr dll=LoadLibraryExW(dllPath,IntPtr.Zero,8);Check(dll!=IntPtr.Zero,"Load failed");IntPtr h=IntPtr.Zero;
  try{
   Get<SetSound>(dll,"_BinkSetSoundSystem@8")(IntPtr.Zero,0);h=Get<Open>(dll,"_BinkOpen@8")(video,0);Check(h!=IntPtr.Zero,"Open failed");
   int width=Marshal.ReadInt32(h),height=Marshal.ReadInt32(h,4);Check(width>=64&&height>=64&&width<=1920&&height<=1080&&Marshal.ReadInt32(h,8)>=90,"Dimensions/frame count");
   Next next=Get<Next>(dll,"_BinkNextFrame@4");Decode decode=Get<Decode>(dll,"_BinkDoFrame@4");Copy copy=Get<Copy>(dll,"_BinkCopyToBufferRect@44");
   for(int i=0;i<90;i++){Check(decode(h)==0,"Decode failed");if(i!=89)next(h);}
   byte[][] all=new byte[4][];int comparisons=0;string[] alpha=new string[4];
   for(uint f=3;f<=6;f++){
    int r;all[f-3]=Pixels(copy,h,width,height,0,0,(uint)width,(uint)height,0,0,0xf0000000u|f,out r);Check(r==0,"Copy return");
    byte[] rect=Pixels(copy,h,64,48,32,16,16,16,16,8,0xf0000000u|f,out r);Check(r==0,"Rect return");
    for(int y=0;y<16;y++)for(int x=0;x<16*4;x++){Check(rect[y*64+x]==all[f-3][((y+16)*width+32)*4+x],"Rectangle mismatch");comparisons++;}
    bool[] values=new bool[256];for(int i=3;i<all[f-3].Length;i+=4)values[all[f-3][i]]=true;string s="";for(int i=0;i<256;i++)if(values[i])s+=(s.Length>0?",":"")+i;alpha[f-3]=s;
   }
   int edgeComparisons=0;
   for(uint format=3;format<=4;format++)for(int which=0;which<2;which++){
    uint sx=which==0?0:(uint)width-128,sy=which==0?(uint)height-208:16;
    uint cw=Math.Min(256u,(uint)width-sx),ch=Math.Min(256u,(uint)height-sy);int er;
    byte[] edge=Pixels(copy,h,256,height,sx,sy,256,256,0,0,format,out er);Check(er==0,"Edge copy return");
    for(int y=0;y<256;y++)for(int x=0;x<256;x++)for(int c=0;c<4;c++){
     byte expected=x<cw&&y<ch?all[format-3][((y+(int)sy)*width+x+(int)sx)*4+c]:(byte)0xa5;
     Check(edge[(y*256+x)*4+c]==expected,"Edge tile clipping/guard mismatch");edgeComparisons++;
    }
   }
   long[] sums=new long[3];int nonGray=0;for(int i=0;i<all[0].Length;i+=4){for(int c=0;c<3;c++){Check(all[0][i+c]==all[1][i+2-c],"32 versus 32R mismatch");comparisons++;sums[c]+=all[0][i+c];}if(all[0][i]!=all[0][i+2])nonGray++;}
   Console.WriteLine("{\"status\":\"PASS\",\"pixel_bytes_compared\":"+comparisons+",\"edge_pixel_bytes_compared\":"+edgeComparisons+",\"non_gray_pixels\":"+nonGray+",\"channel_sums_32\":["+String.Join(",",sums)+"],\"alpha_32\":\""+alpha[0]+"\",\"alpha_32R\":\""+alpha[1]+"\",\"alpha_32A\":\""+alpha[2]+"\",\"alpha_32RA\":\""+alpha[3]+"\",\"played_audio\":false,\"game_started\":false}");
  }finally{if(h!=IntPtr.Zero)Get<Close>(dll,"_BinkClose@4")(h);FreeLibrary(dll);}
 }
}
'@
[BinkCopyOracle]::Run($DllPath,$VideoPath)
