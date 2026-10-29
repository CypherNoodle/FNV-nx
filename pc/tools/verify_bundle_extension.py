"""Admit only a reconstructed private shader bundle preserving its base rows."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from emit_prepared_bundle import emit_bundle,sha

def verify(base_path,extension_path):
    base_path=Path(base_path).resolve();extension_path=Path(extension_path).resolve()
    if base_path==extension_path:raise ValueError('Require a separate extension')
    proofs=[json.loads(p.read_text()) for p in (base_path,extension_path)]
    rows=[]
    for path,proof in zip((base_path,extension_path),proofs):
        manifest=path.parent/'manifest.json';header=path.parent/'startup_program.h'
        if proof.get('status')!='PASS' or proof.get('schema')!='fnv-prepared-bundle-v1':raise ValueError('Invalid bundle receipt')
        for name,digest in proof['input_sha256'].items():
            if sha(Path(name))!=digest:raise ValueError('Changed bundle input')
        if proof['input_sha256'].get(str(manifest))!=sha(manifest) or sha(header)!=proof['header_sha256']:
            raise ValueError('Unbound bundle manifest/header')
        data=json.loads(manifest.read_text())
        rows.append([{**r,'fixture':str((manifest.parent/r['fixture']).resolve())} for r in data['programs']])
        # Reconstruct both headers, including interfaces, uniforms and samplers.
        with TemporaryDirectory(prefix='verify-bundle-',dir=extension_path.parent) as temporary:
            actual=emit_bundle(manifest,Path(temporary)/'program.h')
        for key in ('header_sha256','programs','input_sha256','tool_sha256','emitter_sha256'):
            if actual[key]!=proof[key]:raise ValueError('Bundle reconstruction differs: '+key)
    if len(rows[1])<=len(rows[0]) or len(rows[1])-len(rows[0])>64 or rows[1][:len(rows[0])]!=rows[0]:
        raise ValueError('Extension replaced or reordered baseline programs')
    if proofs[1]['programs']<=proofs[0]['programs']:raise ValueError('Extension adds no distinct programs')
    return proofs[1]
