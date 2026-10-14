"""Read-only PC installation audit. Loading files is not proof of engine startup."""
import argparse,hashlib,json,struct
from pathlib import Path

STEAM_SHA='3a87f92f011e5dc9179ddf733cf08be2b39ea6e5b7a8a9e3a9a72dafcc1b104d'
def pe_info(path):
    raw=path.read_bytes()
    def read(offset,n):
        if offset<0 or n<0 or offset+n>len(raw): raise ValueError('PE file range')
        return raw[offset:offset+n]
    def u16(at): return struct.unpack('<H',read(at,2))[0]
    def u32(at): return struct.unpack('<I',read(at,4))[0]
    if read(0,2)!=b'MZ': raise ValueError('Missing MZ')
    nt=u32(60)
    if read(nt,4)!=b'PE\0\0': raise ValueError('Missing PE signature')
    machine,number=u16(nt+4),u16(nt+6)
    optional_size=u16(nt+20);optional=nt+24
    if u16(optional)!=0x10b or machine!=0x14c: raise ValueError('Expected x86 PE32')
    read(optional,optional_size)
    if optional_size<112 or not 0<number<=96: raise ValueError('Invalid header dimensions')
    base,entry,image_size=u32(optional+28),u32(optional+16),u32(optional+56)
    header_size=u32(optional+60)
    sections=[]
    for i in range(number):
        at=optional+optional_size+i*40;data=read(at,40)
        name=data[:8].split(b'\0')[0].decode('ascii')
        virtual,rva,count,offset=struct.unpack_from('<4I',data,8)
        if rva+virtual>image_size: raise ValueError('Section outside image')
        if count: read(offset,count)
        sections.append(dict(name=name,rva=rva,virtual_bytes=virtual,raw_bytes=count,raw_offset=offset))
    def rva_offset(rva,n=1):
        if 0<=rva<header_size and rva+n<=header_size: read(rva,n);return rva
        for s in sections:
            delta=rva-s['rva']
            if 0<=delta and delta+n<=s['raw_bytes']: return s['raw_offset']+delta
        raise ValueError('RVA has no file backing')
    def string(rva):
        result=bytearray()
        for i in range(1024):
            byte=read(rva_offset(rva+i),1)[0]
            if not byte: return result.decode('ascii')
            result.append(byte)
        raise ValueError('Unterminated PE string')
    imports=[]
    if u32(optional+92)>1:
        import_rva=u32(optional+104);import_size=u32(optional+108)
        if import_rva:
            for index in range(min(import_size//20,4096)):
                row=struct.unpack('<5I',read(rva_offset(import_rva+index*20,20),20))
                if not any(row): break
                original,_,_,dll,iat=row
                symbols=[];lookup=original or iat
                for j in range(4096):
                    value=u32(rva_offset(lookup+j*4,4))
                    if not value: break
                    symbols.append({'ordinal':value&65535} if value&0x80000000 else {'name':string(value+2)})
                else: raise ValueError('Import table exceeds bound')
                imports.append({'dll':string(dll),'symbols':symbols})
            else: raise ValueError('Unterminated import descriptors')
    return dict(sha256=hashlib.sha256(raw).hexdigest(),bytes=len(raw),machine='i386',
        image_base=base,image_bytes=image_size,entry_va=base+entry,
        large_address_aware=bool(u16(nt+22)&0x20),
        entry_section=next((s['name'] for s in sections if s['rva']<=entry<s['rva']+s['virtual_bytes']),None),
        sections=sections,imports=imports)

def bsa_info(path):
    size=path.stat().st_size
    with path.open('rb') as f: header=f.read(36)
    if len(header)!=36 or header[:4]!=b'BSA\0': raise ValueError('Invalid BSA header: '+path.name)
    version,offset,flags,folders,files,folder_names,file_names,file_flags=struct.unpack_from('<8I',header,4)
    if version not in (103,104) or folders>100000 or files>2000000 or offset<36:
        raise ValueError('Unsupported BSA layout: '+path.name)
    if offset+folders*16+folder_names+file_names>size: raise ValueError('BSA index exceeds file: '+path.name)
    return dict(name=path.name,bytes=size,version=version,folders=folders,files=files,flags=flags)

def audit(game):
    exe=game/'FalloutNV.exe';data=game/'Data'
    required=['FalloutNV.esm','Fallout - Meshes.bsa','Fallout - Misc.bsa','Fallout - Sound.bsa',
              'Fallout - Textures.bsa','Fallout - Textures2.bsa','Fallout - Voices1.bsa']
    missing=[name for name in required if not (data/name).is_file() or not (data/name).stat().st_size]
    pe=pe_info(exe)
    dlls={p.name.lower():p for p in game.iterdir() if p.is_file() and p.suffix.lower()=='.dll'}
    helpers={'binkw32.dll','steam_api.dll','libvorbis.dll','libvorbisfile.dll','galaxywrp.dll','galaxy.dll'}
    imported={i['dll'].lower() for i in pe['imports']}
    # Edition-specific libraries are required by actual imports, not store name.
    root_required=tuple(sorted(helpers & imported))+('Fallout_default.ini',)
    for name in root_required:
        provider=dlls.get(name) if name.endswith('.dll') else game/name
        if not provider or not provider.is_file() or not provider.stat().st_size: missing.append(name)
    bsas=[bsa_info(p) for p in sorted(data.glob('*.bsa'))]
    local=[]
    for dll in sorted(dlls.values()):
        try: local.append({'name':dll.name,**pe_info(dll)})
        except ValueError as error: local.append({'name':dll.name,'error':str(error)})
    missing += [row['name']+' (invalid PE)' for row in local if row['name'].lower() in root_required and 'error' in row]
    indicators=dict(entry_in_bind=pe['entry_section']=='.bind',steam_api_import='steam_api.dll' in imported,
        galaxy_wrapper_import=bool(imported & {'galaxywrp.dll','galaxy.dll'}),
        known_steamstub21=pe['sha256']==STEAM_SHA,drm_free=None)
    return dict(result='PASS' if not missing else 'INCOMPLETE',executable=pe,
        known_steam_executable=pe['sha256']==STEAM_SHA,missing=missing,archives=bsas,
        edition_indicators=indicators,required_game_dlls=list(root_required[:-1]),
        local_dlls=local,asset_bytes=sum(p.stat().st_size for p in data.rglob('*') if p.is_file()),
        foreign_graphics_dlls=[name for name in ('d3d9.dll','dxgi.dll','d3d11.dll') if name in dlls],
        engine_started=False,switch_execution_verified=False,graphics_integrated=False,
        limits=['read-only file/header audit, not gameplay','BSA payload checksums are not verified',
                'static imports do not prove provider behavior or absence of DRM',
                'Executable bootstrap and Windows DLL execution need the actual runtime'])
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();report=audit(args.game)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(report['result'],'PE',report['executable']['machine'],'entry section',report['executable']['entry_section'],
          'archives',len(report['archives']),'import DLLs',len(report['executable']['imports']),
          'asset GiB',round(report['asset_bytes']/2**30,2),'engine_started=False')
