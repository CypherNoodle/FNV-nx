# Horizon worker affinity

The Horizon host-thread adapter originally passed core ID -2 to threadCreate
and started the thread without changing affinity. In the installed libnx API,
-2 chooses the process default core. It does not select the process's complete
allowed core mask.

Before starting each worker, the adapter now queries InfoType_CoreMask for the
current process and the new thread's preferred core/current mask. It preserves
a legal preferred core, otherwise chooses a permitted core, and applies the
allowed mask when needed. No hard-coded mask or assumption about application
access to core 3 is used. Existing priority and stack policies are unchanged.

An empty/unsupported mask or failed query/update closes the stopped thread and
reports creation failure. A worker never starts after failed affinity setup.
This allows Horizon to migrate runnable workers among permitted cores; it does
not force every core to stay busy or remove game synchronization dependencies.

`test_host_thread_switch.py` compiles the actual adapter against synthetic
Horizon syscall stubs. Twelve cases check restricted/already-correct masks,
preferred-core handling and rollback at every failing operation. The original
adapter fails the new migration assertion. Strict compilation against installed
libnx headers and Windows managed-thread regressions are separate checks.

This has not yet been measured on a Switch running the game. Successful compile
and syscall-order tests establish implementation behavior, not an FPS increase.
