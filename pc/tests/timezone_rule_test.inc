/* Synthetic transition tables and the independent Windows timezone oracle. */
#include "timezone_rule.h"
static int64_t tz_seconds(unsigned year,unsigned month,unsigned day,unsigned hour){
    SYSTEMTIME s={(WORD)year,(WORD)month,0,(WORD)day,(WORD)hour,0,0,0};FILETIME f;
    assert(SystemTimeToFileTime(&s,&f));return (int64_t)(((uint64_t)f.dwLowDateTime|((uint64_t)f.dwHighDateTime<<32))/10000000)-INT64_C(11644473600);
}
static void tz_transition(unsigned char *r,unsigned index,int64_t seconds,unsigned type){
    uint64_t bits=(uint64_t)seconds;fnv_tz_put32(r+16+index*8,(uint32_t)bits);fnv_tz_put32(r+20+index*8,(uint32_t)(bits>>32));r[0x1f50+index]=(unsigned char)type;
}
static unsigned timezone_rule_checks(void){
    unsigned checks=0;unsigned char rule[0x4000],out[172];uint32_t id;
    const int offsets[][2]={{3600,7200},{-18000,-14400},{36000,37800},{3600,0},{20700,22500}};
    for(unsigned scenario=0;scenario<5;++scenario)for(unsigned south=0;south<2;++south){
        memset(rule,0,sizeof(rule));fnv_tz_put32(rule,8);fnv_tz_put32(rule+4,2);fnv_tz_put32(rule+8,8);
        fnv_tz_put32(rule+0x2338,(uint32_t)offsets[scenario][0]);fnv_tz_put32(rule+0x2348,(uint32_t)offsets[scenario][1]);rule[0x234c]=1;fnv_tz_put32(rule+0x2350,4);
        memcpy(rule+0x2b38,"STD\0DST",8);fnv_tz_put32(rule+0x2d38,south);
        unsigned n=0;tz_transition(rule,n++,tz_seconds(2024,1,1,0),south);
        for(unsigned y=2025;y<=2027;++y){
            tz_transition(rule,n++,tz_seconds(y,south?4:3,15,2)-offsets[scenario][south?1:0],south?0:1);
            tz_transition(rule,n++,tz_seconds(y,south?10:11,15,2)-offsets[scenario][south?0:1],south?1:0);
        }
        tz_transition(rule,n++,tz_seconds(2028,1,1,0),south);assert(n==8);
        for(unsigned month=1;month<=12;++month)for(unsigned day=1;day<=28;day+=3){
            int64_t now=tz_seconds(2026,month,day,12);memset(out,0xa5,sizeof(out));id=77;
            assert(fnv_timezone_rule(rule,sizeof(rule),now,out,&id));TIME_ZONE_INFORMATION zone;memcpy(&zone,out,sizeof(zone));
            unsigned active=south;for(unsigned j=0;j<n;++j)if(fnv_tz_i64(rule+16+j*8)<=now)active=rule[0x1f50+j];
            assert(id==(active?2u:1u) && zone.Bias==-offsets[scenario][0]/60 && zone.StandardBias==0 && zone.DaylightBias==(offsets[scenario][0]-offsets[scenario][1])/60);
            assert(!wcscmp(zone.StandardName,L"STD") && !wcscmp(zone.DaylightName,L"DST"));
            uint16_t utc_fields[8],expected_fields[8];assert(fnv_tz_date(now,utc_fields) && fnv_tz_date(now+offsets[scenario][active],expected_fields));SYSTEMTIME utc,local;
            memcpy(&utc,utc_fields,16);assert(SystemTimeToTzSpecificLocalTime(&zone,&utc,&local) && !memcmp(&local,expected_fields,16));checks++;
        }
        /* Exact transition edges: output must use the offset before the jump
         * for the transition's wall clock, including negative and half-hour DST. */
        for(unsigned j=3;j<=4;++j)for(int delta=-1;delta<=1;++delta){int64_t now=fnv_tz_i64(rule+16+j*8)+delta;
            assert(fnv_timezone_rule(rule,sizeof(rule),now,out,&id));unsigned active=delta<0?rule[0x1f50+j-1]:rule[0x1f50+j];assert(id==(active?2u:1u));checks++;}
        /* Corruption and unsupported shapes never modify output or id. */
        for(unsigned bad=0;bad<9;++bad){unsigned char copy[0x4000];memcpy(copy,rule,sizeof(copy));
            if(bad==0)fnv_tz_put32(copy,1001);if(bad==1)copy[0x1f50]=2;if(bad==2)memcpy(copy+24,copy+16,8);
            if(bad==3)copy[0x234c]=2;if(bad==4)fnv_tz_put32(copy+0x2338,3601);if(bad==5)fnv_tz_put32(copy+0x2350,999);
            if(bad==6)fnv_tz_put32(copy,4);if(bad==7)copy[0x2b38]=0xff;if(bad==8)fnv_tz_put32(copy+0x2d38,2);
            memset(out,0xa5,sizeof(out));id=77;assert(!fnv_timezone_rule(copy,sizeof(copy),tz_seconds(2026,6,1,0),out,&id));
            assert(id==77);for(unsigned k=0;k<sizeof(out);++k)assert(out[k]==0xa5);checks++;
        }
    }
    memset(rule,0,sizeof(rule));fnv_tz_put32(rule+4,1);fnv_tz_put32(rule+8,4);memcpy(rule+0x2b38,"UTC",4);
    assert(fnv_timezone_rule(rule,sizeof(rule),tz_seconds(2026,6,1,0),out,&id) && id==0 && fnv_tz_i32(out)==0);checks++;
    return checks;
}
