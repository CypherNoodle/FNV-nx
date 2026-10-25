# DirectSound public references and object lifetime

The buffer and its 3D interface have independent public reference counters on
the tested Windows DirectSound implementation. A device's reported public count
also excludes the internal holds owned by its buffers. Returning a shared count
made a guest release-to-zero loop consume another interface's references. Its
subsequent 3D Release then encountered a retired identity.

The bridge now separates base-interface references, 3D/listener references and
total object lifetime. QueryInterface increments the selected public count and
the total. Release returns the selected count; storage is released only when
the total reaches zero. A child drops its internal device hold only at that
point. Primary-buffer reuse acquires a new base reference. A live 3D interface
can reacquire the controlling buffer with QueryInterface after its base count
has reached zero. A repeated Release on a zero public count fails explicitly
without holding the provider mutex.

`test_dsound8.py` compares these transitions through actual translated x86 COM
callers against native Windows DirectSound. It checks device counts while a
child exists, Buffer8/base sharing, independent 3D counts, a usable 3D interface
after base zero, reacquisition through IUnknown, and listener lifetime. Native
buffers are never played. Bridge mixer output is compared in memory only.

The standalone `test_dsound_interface_refs.py` records the small native oracle
independently. Run it on Windows with `--cc` and a fresh `--output` directory.
It creates an original synthetic PCM buffer, never calls Play, and records
source and executable hashes with its result.

These tests establish reference semantics for the implemented interfaces. They
do not establish Nintendo Switch performance or complete game startup. Peer
fault propagation is documented separately in `cpu-peer-dispatch.md`.
