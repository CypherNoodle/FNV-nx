/* Native Windows oracle only: no Play calls or audible output. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dsound.h>
#include <assert.h>
#include <stdio.h>
int main(void){
 IDirectSound8 *d=NULL;assert(SUCCEEDED(DirectSoundCreate8(NULL,&d,NULL)));
 assert(SUCCEEDED(IDirectSound8_SetCooperativeLevel(d,GetDesktopWindow(),DSSCL_NORMAL)));
 WAVEFORMATEX wave={WAVE_FORMAT_PCM,1,48000,96000,2,16,0};DSBUFFERDESC desc={0};desc.dwSize=sizeof(desc);desc.dwFlags=DSBCAPS_CTRL3D|DSBCAPS_CTRLVOLUME|DSBCAPS_GETCURRENTPOSITION2;desc.dwBufferBytes=512;desc.lpwfxFormat=&wave;
 IDirectSoundBuffer *b=NULL;IDirectSoundBuffer8 *b8=NULL;IDirectSound3DBuffer *sp=NULL;
 assert(SUCCEEDED(IDirectSound8_CreateSoundBuffer(d,&desc,&b,NULL)));
 assert(SUCCEEDED(IDirectSoundBuffer_QueryInterface(b,&IID_IDirectSoundBuffer8,(void**)&b8)));
 assert(SUCCEEDED(IDirectSoundBuffer_QueryInterface(b,&IID_IDirectSound3DBuffer,(void**)&sp)));
 ULONG device_add=IDirectSound8_AddRef(d),device_release=IDirectSound8_Release(d);
 ULONG base_add=IDirectSoundBuffer_AddRef(b),base_release=IDirectSoundBuffer_Release(b);
 ULONG alias_add=IDirectSound3DBuffer_AddRef(sp),alias_release=IDirectSound3DBuffer_Release(sp);
 ULONG base_release1=IDirectSoundBuffer8_Release(b8),base_release2=IDirectSoundBuffer_Release(b);DWORD mode=99;HRESULT live=IDirectSound3DBuffer_GetMode(sp,&mode);ULONG alias_final=IDirectSound3DBuffer_Release(sp);
 assert(device_add==2 && device_release==1 && base_add==3 && base_release==2 && alias_add==2 && alias_release==1 && base_release1==1 && base_release2==0 && SUCCEEDED(live) && !mode && alias_final==0);
 IDirectSound8_Release(d);
 printf("{\"status\":\"PASS\",\"independent_device_refs\":true,\"independent_buffer_interface_refs\":true,\"spatial_alive_after_base_zero\":true,\"played_audio\":false}\n");return 0;
}
