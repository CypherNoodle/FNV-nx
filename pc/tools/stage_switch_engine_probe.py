"""Stage a private SD package from an accepted engine NRO, with no DLL copying.

The user's original default INI is private data, not published project source.
Use a fresh directory; staging never modifies an installation or an existing SD.
"""
import argparse
import json
import re
from pathlib import Path
import shutil
import struct
from link_switch_engine_platform import sha

def verify_embedded_files(nro,expected):
    raw=nro.read_bytes()
    if len(raw)<64 or raw[16:20]!=b'NRO0':raise ValueError('Invalid NRO header')
    asset=struct.unpack_from('<I',raw,24)[0]
    if asset+56>len(raw) or raw[asset:asset+4]!=b'ASET':raise ValueError('Invalid asset header')
    relative,size=struct.unpack_from('<QQ',raw,asset+40);base=asset+relative
    if size<80 or base+size>len(raw):raise ValueError('Invalid ROMFS bounds')
    header=struct.unpack_from('<10Q',raw,base)
    _,_,_,dirs,dir_bytes,_,_,files,file_bytes,data=header
    if header[0]!=80 or dirs+dir_bytes>size or dir_bytes<24 or files+file_bytes>size or data>size:
        raise ValueError('Invalid ROMFS metadata bounds')
    root=struct.unpack_from('<6I',raw,base+dirs)
    if root[2]!=0xffffffff or root[5]:raise ValueError('Require the flat diagnostic ROMFS')
    offset=root[3];seen=set();actual={}
    while offset!=0xffffffff:
        if offset in seen or offset+32>file_bytes:raise ValueError('Invalid ROMFS file chain')
        seen.add(offset);at=base+files+offset
        parent,next_file,start,length,_,name_bytes=struct.unpack_from('<IIQQII',raw,at)
        if parent or not name_bytes or offset+32+name_bytes>file_bytes or start+length>size-data:
            raise ValueError('Invalid ROMFS file entry')
        name=raw[at+32:at+32+name_bytes].decode('ascii')
        if name in actual or '/' in name or '\\' in name or '\0' in name:raise ValueError('Invalid ROMFS file name')
        import hashlib
        actual[name]=hashlib.sha256(raw[base+data+start:base+data+start+length]).hexdigest()
        offset=next_file
    if actual!=expected:raise ValueError('Embedded private files differ from accepted build inputs')
    return actual

def stage(validation,default_ini,output):
    output=output.resolve()
    if output.exists():raise ValueError('Use a fresh delivery directory')
    proof=json.loads(validation.read_text())
    if not re.fullmatch(r'pc-engine-hw\d{2}',proof.get('build','')):raise ValueError('Invalid diagnostic build name')
    if proof.get('status')!='BUILT' or not proof.get('guest_engine_execution_enabled') or not proof.get('private_output'):
        raise ValueError('Require an accepted private engine diagnostic')
    nro=Path(proof['nro'])
    if sha(nro)!=proof['nro_sha256']:raise ValueError('Changed NRO')
    expected={}
    for filename in ('captured-text-data.bin','catalog.sdp'):
        hashes={digest for name,digest in proof['source_sha256'].items() if Path(name).name==filename}
        if len(hashes)!=1:raise ValueError('Ambiguous private input identity: '+filename)
        expected[filename]=hashes.pop()
    embedded=verify_embedded_files(nro,expected)
    default=default_ini.read_bytes()
    if not 1<=len(default)<=1<<20 or b'\0' in default:raise ValueError('Invalid original default INI')
    root=output/'FNV-nx';profile=root/'UserData/FNV/Documents/My Games/FalloutNV'
    profile.mkdir(parents=True)
    shutil.copyfile(nro,root/'fnv-nx.nro')
    (profile/'FALLOUT.INI').write_bytes(default)
    (profile/'FalloutPrefs.ini').write_text('[Display]\niSize W=1280\niSize H=720\nbFull Screen=1\n[Controls]\nbUse Joystick=1\n',newline='\n')
    text='''Prueba privada FNV PC engine hw01 — todavía sin validar en consola.

1. Respalda el NRO y UserData anteriores de sd:/switch/FNV-nx/.
2. Copia el contenido de FNV-nx de este paquete a sd:/switch/FNV-nx/.
3. En esa misma carpeta coloca FalloutNV.exe ORIGINAL y Data de tu edición PC
   de Steam compatible. Son archivos de PC, no los assets de Xbox 360.
   No uses el EXE parcheado con 4 GB/anticrash. No copies DLL de Autorun.
4. Abre fnv-nx.nro desde hbmenu en modo aplicación (R al abrir un juego).
   El Álbum no tiene la memoria necesaria. La salida de audio está a volumen 0.
5. Esta prueba ya intenta ejecutar el motor. Puede fallar antes del menú y no
   constituye un arranque del EXE desde cero: incorpora código capturado privado.
6. Para detenerla mantén L + R + Minus un segundo. Plus queda disponible para
   el juego. El fin de sesión es diagnóstico; no es un cierre normal del motor.
7. Devuelve sd:/switch/FNV-nx/fnv-pc-engine-hw01.log y describe qué se vio.
   Empieza por el menú; no uses partidas importantes ni midas rendimiento aún.

El paquete contiene código/datos privados del juego. No lo subas al repositorio.
Los ajustes de adaptador y tamaño se obtienen de la Switch. El perfil de caps
es experimental, limitado a los programas gráficos preparados.
'''
    text=text.replace('hw01',proof['build'].removeprefix('pc-engine-'))
    if proof.get('native_crash_capture'):
        text+='\nSi se cierra, envia tambien fnv-pc-engine-'+proof['build'].removeprefix('pc-engine-')+'-crash.log de la misma carpeta. Esta revision recoge el fallo nativo; no presupone corregida su causa.\n'
    (output/'LEEME.txt').write_text(text,encoding='utf-8')
    result=dict(status='STAGED',nro=str(root/'fnv-nx.nro'),nro_sha256=sha(root/'fnv-nx.nro'),
                default_ini_sha256=sha(profile/'FALLOUT.INI'),private_output=True,
                switch_hardware_tested=False,bulk_game_assets_copied=False,dlls_copied=False)
    # default INI is user game data, while bulk executable/Data assets are not
    # staged by this helper. Record that distinction explicitly.
    result['user_default_ini_copied']=True
    result['verified_embedded_files']=embedded
    (output/'delivery.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('validation','default-ini','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(stage(a.validation,a.default_ini,a.output)))
