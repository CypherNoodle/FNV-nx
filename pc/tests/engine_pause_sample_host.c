#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "engine_pause_sample.h"
struct fixture {unsigned trace,paused;uint32_t pause_error,capture_error,resume_error;};
static uint32_t pause_call(void *v){struct fixture *f=v;assert(!f->trace && !f->paused);f->trace=1;if(!f->pause_error)f->paused=1;return f->pause_error;}
static uint32_t capture_call(void *v){struct fixture *f=v;assert(f->trace==1 && f->paused);f->trace=12;return f->capture_error;}
static uint32_t resume_call(void *v){struct fixture *f=v;assert(f->trace==12 && f->paused);f->trace=123;f->paused=0;return f->resume_error;}
int main(void){
 unsigned cases=0;
 for(unsigned p=0;p<2;p++)for(unsigned c=0;c<2;c++)for(unsigned r=0;r<2;r++){
    struct fixture f={.pause_error=p?0xce01:0,.capture_error=c?0xc001:0,.resume_error=r?0xb001:0};
    struct fnv_pause_ops ops={&f,pause_call,capture_call,resume_call};struct fnv_pause_result got=fnv_pause_sample(&ops);
    assert(got.pause==f.pause_error && !f.paused);
    if(p)assert(f.trace==1 && got.capture==UINT32_MAX && got.resume==UINT32_MAX);
    else assert(f.trace==123 && got.capture==f.capture_error && got.resume==f.resume_error);
    cases++;
 }
 printf("{\"status\":\"PASS\",\"cases\":%u,\"resume_after_capture_failure\":true,\"capture_skipped_after_pause_failure\":true}\n",cases);return 0;
}
