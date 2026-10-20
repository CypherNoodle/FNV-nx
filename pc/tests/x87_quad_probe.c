/* Research harness only. No game translation or Horizon runtime integration. */
#include <stdint.h>
#include <float.h>
#include <math.h>
_Static_assert(LDBL_MANT_DIG==113 && LDBL_MAX_EXP==16384,"Require binary128 long double");
_Static_assert(sizeof(long double)==16,"Require 16-byte binary128 storage");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Require a little-endian research target
#endif
union quad {long double value;uint64_t words[2];};
static int probe_errno;
int *__errno(void){return &probe_errno;}
static union quad load(const unsigned char *p){uint64_t sig=0;for(unsigned i=0;i<8;i++)sig|=(uint64_t)p[i]<<(8*i);uint16_t se=p[8]|(uint16_t)p[9]<<8;union quad q;q.words[0]=sig<<49;q.words[1]=((uint64_t)se<<48)|((sig>>15)&UINT64_C(0xffffffffffff));return q;}
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out){
union quad a=load(request),b=load(request+10),z;probe_errno=0;__asm__ volatile("msr fpsr,xzr");
switch(request[20]){case 0:z.value=sinl(a.value);break;case 1:z.value=cosl(a.value);break;case 2:z.value=tanl(a.value);break;case 3:z.value=atan2l(a.value,b.value);break;case 4:z.value=log2l(a.value)*b.value;break;default:z.value=expm1l(a.value*0x1.62e42fefa39ef35793c7673007e6p-1L);break;}
for(unsigned j=0;j<2;j++)for(unsigned i=0;i<8;i++)out[j*8+i]=(unsigned char)(z.words[j]>>(i*8));
uint64_t fpsr;__asm__ volatile("mrs %0,fpsr":"=r"(fpsr));for(unsigned i=0;i<4;i++){out[16+i]=(unsigned char)((unsigned)probe_errno>>(i*8));out[20+i]=(unsigned char)(fpsr>>(i*8));}
}
