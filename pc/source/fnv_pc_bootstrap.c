/* FNV-specific entry adapter. Wine, CPU, graphics and input remain Autorun's. */
#include "fnv_pc_bootstrap.h"
#include "fnv_pc_pe.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef FNV_PC_ROOT
#define FNV_PC_ROOT "sdmc:/switch/FNV-nx"
#endif
#define GAME FNV_PC_ROOT "/pc/FalloutNV.exe"
#ifndef FNV_PC_WINE_ROOT
#define FNV_PC_WINE_ROOT "sdmc:/switch/wine"
#endif
#define USER_DOCUMENTS FNV_PC_WINE_ROOT "/drive_c/users/steamuser/Documents"
#define GAME_SETTINGS USER_DOCUMENTS "/My Games/FalloutNV"

struct settings { int cpu, renderer, profile, verbose, cores, fps, vsync, cap, compositor, labels, width, height; };
static char *direct_argv[] = {FNV_PC_ROOT "/fnv-nx.nro", GAME, NULL};
static int map_labels = 1;

void fnv_pc_note_failure(const char *reason) {
    FILE *log=fopen(FNV_PC_ROOT "/logs/fnv-pc-bootstrap.log","a");
    if (log) { fprintf(log,"runtime_failure=%s\n",reason); fclose(log); }
    printf("FNV-nx PC: %s\n",reason);
    printf("Registro: " FNV_PC_ROOT "/logs/fnv-pc-bootstrap.log\n");
}

unsigned short fnv_pc_map_buttons(unsigned short b) {
    if (!map_labels) return b;
    return (b & 0x0fff) | ((b & 0x5000) << 1) | ((b & 0xa000) >> 1);
}

static char *trim(char *s) {
    char *end;
    while (isspace((unsigned char)*s)) ++s;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return s;
}
static int boolean(const char *v, int *out) {
    if (!strcmp(v, "0") || !strcmp(v, "1")) { *out = v[0] - '0'; return 1; }
    return 0;
}
static int read_settings(struct settings *s, FILE *log) {
    FILE *f = fopen(FNV_PC_ROOT "/fnv-pc.ini", "rb");
    char line[256], section[32] = "";
    unsigned seen = 0, count = 0;
    if (!f) { fprintf(log, "ini=missing; using documented defaults\n"); return 1; }
    while (fgets(line, sizeof(line), f)) {
        char *key, *v, *end; unsigned bit = 0; int ok = 0;
        ++count;
        if (!strchr(line, '\n') && !feof(f)) goto bad;
        key = trim(line);
        if (!*key || *key == ';' || *key == '#') continue;
        if (*key == '[') {
            end = strchr(key, ']');
            if (!end || *trim(end + 1)) goto bad;
            *end = 0;
            if (strcmp(key + 1, "runtime") && strcmp(key + 1, "graphics")) goto bad;
            snprintf(section, sizeof(section), "%s", key + 1); continue;
        }
        v = strchr(key, '='); if (!v) goto bad;
        *v++ = 0; key = trim(key); v = trim(v);
        if (!strcmp(section, "runtime")) {
            if (!strcmp(key, "cpu")) { bit = 1; ok = !strcmp(v,"fex") || !strcmp(v,"box64"); s->cpu = !strcmp(v,"fex"); }
            else if (!strcmp(key,"renderer")) { bit = 2; ok = !strcmp(v,"dxvk") || !strcmp(v,"wine"); s->renderer = !strcmp(v,"dxvk"); }
            else if (!strcmp(key,"profile")) { bit = 4; ok = boolean(v,&s->profile); }
            else if (!strcmp(key,"verbose")) { bit = 8; ok = boolean(v,&s->verbose); }
            else if (!strcmp(key,"four_cores")) { bit = 16; ok = boolean(v,&s->cores); }
            else if (!strcmp(key,"button_layout")) { bit = 512; ok = !strcmp(v,"labels") || !strcmp(v,"positions"); s->labels = !strcmp(v,"labels"); }
        } else if (!strcmp(section, "graphics")) {
            if (!strcmp(key,"fps")) { bit = 32; ok = boolean(v,&s->fps); }
            else if (!strcmp(key,"vsync")) { bit = 64; ok = boolean(v,&s->vsync); }
            else if (!strcmp(key,"frame_limit")) { bit = 128; ok = !strcmp(v,"0") || !strcmp(v,"30") || !strcmp(v,"60"); s->cap = ok ? atoi(v) : 0; }
            else if (!strcmp(key,"presentation")) { bit = 256; ok = !strcmp(v,"framebuffer") || !strcmp(v,"compositor"); s->compositor = !strcmp(v,"compositor"); }
            else if (!strcmp(key,"width")) { bit = 1024; ok = !strcmp(v,"640") || !strcmp(v,"960") || !strcmp(v,"1280"); s->width = ok ? atoi(v) : 0; }
            else if (!strcmp(key,"height")) { bit = 2048; ok = !strcmp(v,"360") || !strcmp(v,"540") || !strcmp(v,"720"); s->height = ok ? atoi(v) : 0; }
        }
        if (!ok || (seen & bit)) goto bad;
        seen |= bit;
    }
    if (ferror(f)) goto bad;
    if (s->width * 9 != s->height * 16) goto bad;
    fclose(f); return 1;
bad:
    fprintf(log,"ini=invalid at line %u; launch cancelled\n",count);
    fclose(f); return 0;
}
static int nonempty(const char *path) { struct stat st; return !stat(path,&st) && S_ISREG(st.st_mode) && st.st_size > 0; }

/* Horizon fsdev rename does not replace an existing destination. Retain and
 * recover the previous version instead of deleting an INI before committing. */
static int recover_file(const char *dest) {
    char backup[560]; struct stat st;
    snprintf(backup,sizeof(backup),"%s.fnv-previous",dest);
    if (!stat(backup,&st)) {
        if (nonempty(dest)) return remove(backup)==0;
        return rename(backup,dest)==0;
    }
    return 1;
}
static int commit_file(const char *temp, const char *dest) {
    char backup[560]; struct stat st;
    if (!rename(temp,dest)) return 1;
    if (stat(dest,&st) || !S_ISREG(st.st_mode)) return 0;
    snprintf(backup,sizeof(backup),"%s.fnv-previous",dest);
    if (!stat(backup,&st) || rename(dest,backup)) return 0;
    if (rename(temp,dest)) { rename(backup,dest); return 0; }
    return remove(backup)==0;
}

struct pref { const char *section, *key; int value, seen; };
static int append_section(FILE *f, struct pref *p, size_t n, const char *section) {
    for (size_t i=0; i<n; ++i) if (!p[i].seen && !strcmp(p[i].section,section)) {
        if (fprintf(f,"%s=%d\n",p[i].key,p[i].value)<0) return 0;
        p[i].seen=1;
    }
    return 1;
}
/* Merge only our display/controller keys. Retain other settings and save paths. */
static int game_ini(const char *name, struct settings *s) {
    struct pref p[]={{"Display","iSize W",s->width,0},{"Display","iSize H",s->height,0},
        {"Display","bFull Screen",0,0},{"Display","iMultiSample",0,0},
        {"Display","iPresentInterval",s->vsync,0},{"Display","bTransparencyMultisampling",0,0},
        {"Controls","bDisable360Controller",0,0}};
    char dest[512],temp[520],line[2048],copy[2048],section[128]="";
    FILE *src,*out; size_t total=0, n=sizeof(p)/sizeof(p[0]); int ok=1;
    snprintf(dest,sizeof(dest),GAME_SETTINGS "/%s",name);
    snprintf(temp,sizeof(temp),"%s.tmp",dest);
    if (!recover_file(dest)) return 0;
    src=fopen(dest,"rb");
    if (!src) src=fopen(FNV_PC_ROOT "/pc/Fallout_default.ini","rb");
    if (!src) return 0;
    out=fopen(temp,"wb"); if (!out) { fclose(src); return 0; }
    while (ok && fgets(line,sizeof(line),src)) {
        char *text,*end,*eq; int changed=0;
        total+=strlen(line); if (total>1024*1024 || (!strchr(line,'\n') && !feof(src))) { ok=0; break; }
        strcpy(copy,line); text=trim(copy);
        if (*text=='[' && (end=strchr(text,']'))) {
            ok=append_section(out,p,n,section); *end=0;
            if (strlen(text+1)>=sizeof(section)) { ok=0; break; }
            strcpy(section,text+1);
        } else if ((eq=strchr(text,'='))) {
            *eq=0; text=trim(text);
            for (size_t i=0; i<n; ++i) if (!strcmp(section,p[i].section) && !strcmp(text,p[i].key)) {
                ok=fprintf(out,"%s=%d\n",p[i].key,p[i].value)>=0; p[i].seen=changed=1; break;
            }
        }
        if (!changed && fputs(line,out)==EOF) ok=0;
        if (*line && line[strlen(line)-1]!='\n' && fputc('\n',out)==EOF) ok=0;
    }
    if (ferror(src)) ok=0;
    if (ok) ok=append_section(out,p,n,section);
    for (size_t i=0; ok && i<n; ++i) if (!p[i].seen) {
        ok=fprintf(out,"\n[%s]\n",p[i].section)>=0 && append_section(out,p,n,p[i].section);
    }
    fclose(src); if (fclose(out)) ok=0;
    if (!ok) { remove(temp); return 0; }
    return commit_file(temp,dest);
}

int fnv_pc_bootstrap(int *argc, char ***argv) {
    struct settings s = {1,1,1,0,0,1,0,30,0,1,960,540};
    struct fnv_pc_pe pe;
    FILE *log, *profile; char path[512];
    const char *required[] = {"FalloutNV.exe", "Fallout_default.ini", "Data/FalloutNV.esm", "Data/Fallout - Meshes.bsa", "Data/Fallout - Misc.bsa", "Data/Fallout - Sound.bsa", "Data/Fallout - Textures.bsa", "Data/Fallout - Textures2.bsa", "Data/Fallout - Voices1.bsa"};
    mkdir(FNV_PC_ROOT "/logs",0777);
    log = fopen(FNV_PC_ROOT "/logs/fnv-pc-bootstrap.log","w");
    if (!log) { printf("FNV PC: no se pudo abrir el registro.\n"); return 0; }
    fprintf(log,"FNV-nx PC bootstrap 03\nengine_started=unverified\n");
    if (!read_settings(&s,log)) goto fail;
    for (size_t i=0; i<sizeof(required)/sizeof(required[0]); ++i) {
        snprintf(path,sizeof(path),FNV_PC_ROOT "/pc/%s",required[i]);
        if (!nonempty(path)) { fprintf(log,"missing=%s\n",path); goto fail; }
    }
    if (!fnv_pc_read_pe(GAME,&pe,log)) goto fail;
    fprintf(log,"pe=i386 base=%08x entry_rva=%08x image_bytes=%u imports=%u\n",pe.image_base,pe.entry_rva,pe.image_bytes,pe.import_count);
    fprintf(log,"large_address_aware=%d\n",!!(pe.file_characteristics & 0x20));
    fprintf(log,"entry_in_bind=%d steam_api_import=%d galaxy_wrapper_import=%d\n",pe.entry_in_bind,pe.steam_api,pe.galaxy_wrapper);
    fprintf(log,"drm_free=unverified; import/section indicators are not proof of absence of DRM\n");
    if (pe.entry_in_bind) fprintf(log,"bind_entry_indicator=present; executable protection requires analysis\n");
    if (pe.steam_api) fprintf(log,"steam_api_provider_behavior=unverified; this runtime does not supply a Steam client\n");
    for (unsigned i=0;i<pe.import_count;++i) {
        const char *dll=pe.imports[i];
        const char *helpers[]={"steam_api.dll","galaxywrp.dll","galaxy.dll","binkw32.dll","libvorbisfile.dll","libvorbis.dll"};
        fprintf(log,"import=%s\n",dll);
        for (size_t j=0;j<sizeof(helpers)/sizeof(helpers[0]);++j) if (!strcmp(dll,helpers[j])) {
            snprintf(path,sizeof(path),FNV_PC_ROOT "/pc/%s",dll);
            if (!nonempty(path)) { fprintf(log,"missing_imported_game_dll=%s\n",path); goto fail; }
        }
    }
    mkdir(USER_DOCUMENTS "/My Games",0777); mkdir(GAME_SETTINGS,0777);
    if (!game_ini("Fallout.ini",&s) || !game_ini("FalloutPrefs.ini",&s)) { fprintf(log,"game_ini=failed\n"); goto fail; }
    fprintf(log,"display=%dx%d windowed MSAA=0; game INI initialized/merged\n",s.width,s.height);
    snprintf(path,sizeof(path),FNV_PC_ROOT "/pc/FalloutNV.wine-nx.txt.tmp");
    profile = fopen(path,"w");
    if (!profile) { fprintf(log,"profile=write failed\n"); goto fail; }
    if (fprintf(profile,"# Generated by fnv-pc.ini; edit the INI.\ntitle=Fallout New Vegas PC\ncpu=%s\nd3d=%s\nprofile=%d\nverbose=%d\nfour-cores=%d\nwindows=%s\ndxvk-hud=%s\nvsync=%d\nframe-limit=%d\nlsfg=0\n",
        s.cpu ? "fex":"box64",s.renderer ? "dxvk":"wine",s.profile,s.verbose,s.cores,s.compositor ? "compositor":"framebuffer",s.fps ? "fps":"0",s.vsync,s.cap) < 0) {
        fclose(profile); fprintf(log,"profile=write failed\n"); goto fail;
    }
    if (fclose(profile)) { fprintf(log,"profile=close failed\n"); goto fail; }
    if (!recover_file(FNV_PC_ROOT "/pc/FalloutNV.wine-nx.txt") || !commit_file(path,FNV_PC_ROOT "/pc/FalloutNV.wine-nx.txt")) { fprintf(log,"profile=commit failed\n"); goto fail; }
    map_labels = s.labels;
    fprintf(log,"button_layout=%s; xinput_rumble=not implemented by upstream\n",s.labels ? "labels":"positions");
    fprintf(log,"target=%s\ncpu=%s renderer=%s fps=%d vsync=%d cap=%d four_cores=%d\nexecutable_bootstrap=original; authentication not bypassed\npreflight=PASS; handing execution to Wine\n",GAME,s.cpu ? "fex":"box64",s.renderer ? "dxvk":"wine",s.fps,s.vsync,s.cap,s.cores);
    fclose(log);
    /* A dedicated FNV application must ignore host launcher arguments. */
    *argc = 2; *argv = direct_argv;
    return 1;
fail:
    fclose(log); printf("FNV PC: revisa " FNV_PC_ROOT "/logs/fnv-pc-bootstrap.log\n"); return 0;
}
