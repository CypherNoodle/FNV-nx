#ifndef FNV_TIMEZONE_RULE_H
#define FNV_TIMEZONE_RULE_H
#include "host_calendar.h"
#include <string.h>
/* Own bounded decoder for the Horizon TimeZoneRule layout documented by
 * Switchbrew. Output is the 172-byte little-endian Win32 TIME_ZONE_INFORMATION.
 * No extrapolation outside explicit transition coverage, no historical promise. */
static inline uint32_t fnv_tz_u32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline int32_t fnv_tz_i32(const unsigned char *p){uint32_t n=fnv_tz_u32(p);return n>>31?-1-(int32_t)~n:(int32_t)n;}
static inline int64_t fnv_tz_i64(const unsigned char *p){uint64_t n=fnv_tz_u32(p)|((uint64_t)fnv_tz_u32(p+4)<<32);return n>>63?-1-(int64_t)~n:(int64_t)n;}
static inline void fnv_tz_put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(i*8));}
static inline void fnv_tz_put16(unsigned char *p,uint16_t n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static inline int fnv_tz_date(int64_t seconds,uint16_t out[8]){
    uint64_t filetime;return fnv_host_utc_from_unix(seconds,0,&filetime) && fnv_calendar_systemtime(filetime,out);
}
static inline int fnv_tz_name(const unsigned char *rule,uint32_t chars,unsigned type,unsigned char *out){
    uint32_t start=fnv_tz_u32(rule+0x2338+16*type+8);if(start>=chars)return 0;
    for(unsigned i=0;i<32;++i){if(start+i>=chars)return 0;unsigned c=rule[0x2b38+start+i];
        if(!c)return 1;
        if(c<32 || c>126 || i==31)return 0;
        fnv_tz_put16(out+2*i,(uint16_t)c);}
    return 0;
}
static inline int fnv_timezone_rule(const unsigned char *rule,size_t bytes,int64_t now,unsigned char output[172],uint32_t *id){
    if(!rule || bytes!=0x4000 || !output || !id)return 0;
    uint32_t count=fnv_tz_u32(rule),types=fnv_tz_u32(rule+4),chars=fnv_tz_u32(rule+8),initial=fnv_tz_u32(rule+0x2d38);
    if(count>1000 || !types || types>128 || !chars || chars>512 || initial>=types || rule[12]>1 || rule[13]>1)return 0;
    for(unsigned i=0;i<types;++i){const unsigned char *t=rule+0x2338+16*i;int32_t offset=fnv_tz_i32(t);
        if(t[4]>1 || offset < -86400 || offset>86400 || fnv_tz_u32(t+8)>=chars)return 0;}
    uint16_t current_date[8];if(!fnv_tz_date(now,current_date))return 0;unsigned year=current_date[0];
    /* Use the local year at now, not the UTC year at a New Year boundary. */
    unsigned active=initial;int64_t previous=INT64_MIN;
    for(unsigned i=0;i<count;++i){int64_t at=fnv_tz_i64(rule+16+8*i);unsigned next=rule[0x1f50+i];
        if(next>=types || at<=previous)return 0;
        previous=at;if(at<=now)active=next;}
    int32_t current_offset=fnv_tz_i32(rule+0x2338+16*active);
    if((current_offset>0 && now>INT64_MAX-current_offset) || (current_offset<0 && now<INT64_MIN-current_offset) || !fnv_tz_date(now+current_offset,current_date))return 0;
    year=current_date[0];int64_t first_day=0;
    if(year<1602 || year>30826)return 0;
    for(unsigned y=1970;y<year;++y)first_day+=365+fnv_calendar_leap(y);
    for(unsigned y=year;y<1970;++y)first_day-=365+fnv_calendar_leap(y);
    int64_t begin=first_day*86400,end=begin+(365+fnv_calendar_leap(year))*INT64_C(86400);
    /* Nonempty transition tables must cover both edges of the local year. */
    if(count && (fnv_tz_i64(rule+16)>begin-86400 || fnv_tz_i64(rule+16+8*(count-1))<end+86400))return 0;
    unsigned char result[172]={0};unsigned before=initial,standard=128,daylight=128,transitions=0;uint16_t dates[2][8];
    for(unsigned i=0;i<count;++i){int64_t at=fnv_tz_i64(rule+16+8*i);unsigned after=rule[0x1f50+i];
        int32_t old_offset=fnv_tz_i32(rule+0x2338+16*before),new_offset=fnv_tz_i32(rule+0x2338+16*after);
        if((old_offset>0 && at>INT64_MAX-old_offset) || (old_offset<0 && at<INT64_MIN-old_offset))return 0;
        uint16_t date[8];
        if(at>=begin-86400 && at<=end+86400){
            if(!fnv_tz_date(at+old_offset,date))return 0;
            if(date[0]==year && (old_offset!=new_offset || rule[0x2338+16*before+4]!=rule[0x2338+16*after+4])){
                if(++transitions>2 || rule[0x2338+16*before+4]==rule[0x2338+16*after+4])return 0;
                unsigned s=rule[0x2338+16*after+4]?before:after,d=rule[0x2338+16*after+4]?after:before;
                if((standard!=128 && (fnv_tz_i32(rule+0x2338+16*standard)!=fnv_tz_i32(rule+0x2338+16*s))) ||
                   (daylight!=128 && (fnv_tz_i32(rule+0x2338+16*daylight)!=fnv_tz_i32(rule+0x2338+16*d))))return 0;
                standard=s;daylight=d;memcpy(dates[rule[0x2338+16*after+4]?1:0],date,sizeof(date));
            }
        }
        before=after;
    }
    uint32_t state;
    if(!transitions){if(rule[0x2338+16*active+4])return 0;standard=active;state=0;}
    else {if(transitions!=2 || standard==128 || daylight==128)return 0;state=rule[0x2338+16*active+4]?2:1;}
    int32_t standard_offset=fnv_tz_i32(rule+0x2338+16*standard);
    if(standard_offset%60 || !fnv_tz_name(rule,chars,standard,result+4))return 0;
    fnv_tz_put32(result,(uint32_t)(-standard_offset/60));
    if(transitions){int32_t daylight_offset=fnv_tz_i32(rule+0x2338+16*daylight);
        if(daylight_offset%60 || !fnv_tz_name(rule,chars,daylight,result+88))return 0;
        fnv_tz_put32(result+168,(uint32_t)((standard_offset-daylight_offset)/60));
        for(unsigned i=0;i<8;++i){fnv_tz_put16(result+68+2*i,dates[0][i]);fnv_tz_put16(result+152+2*i,dates[1][i]);}
    }
    memcpy(output,result,sizeof(result));*id=state;return 1;
}
#endif
