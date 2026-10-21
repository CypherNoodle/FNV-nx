"""Explicit backing budgets for bounded private PC startup diagnostics."""
PROFILES = {
    'bounded64': dict(arena_bytes=64<<20, vm_bytes=24<<20, commit_limit=16<<20),
    'extended808': dict(arena_bytes=808<<20, vm_bytes=768<<20, commit_limit=768<<20),
}

def configuration(name):
    if name not in PROFILES:raise ValueError('Unknown diagnostic memory profile')
    return dict(PROFILES[name])

def validate_observation(host,name):
    expected=configuration(name)
    observed=host.get('guest_memory',{})
    if any(observed.get(k)!=v for k,v in expected.items()):
        raise ValueError('Host did not allocate the selected diagnostic memory profile')
    if observed.get('committed_bytes',-1)<0 or observed['committed_bytes']>expected['commit_limit']:
        raise ValueError('Invalid diagnostic committed-byte accounting')

def require_switch_profile(desktop):
    # Older bounded observations predate the explicit selector. New profiles
    # cannot be used as evidence for the fixed 64 MiB Switch CRT probe.
    name=desktop.get('memory_profile','bounded64')
    if name!='bounded64':raise ValueError('Host memory profile differs from the fixed Switch CRT probe')
    if 'memory_profile' in desktop:validate_observation(desktop['host'],name)
