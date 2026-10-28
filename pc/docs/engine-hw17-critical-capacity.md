# HW17 first fault and HW18 synchronization budget

The normal HW17 log identifies the first failure as
`Windows critical-section capacity/SEH pending`. It happens while a worker
initializes a critical section. The engine process plan only provides 256
simultaneously live critical-section slots. Exhausting that fixed table is
fatal for `InitializeCriticalSection`, whose Windows exception path is not
implemented by the runtime. It is not a captured native ARM64 exception.

Live GDB inspection confirms that the white screen is a post-fault state:
`done` is set, the guest main has returned a peer-fault result, and several
workers are waiting/spinning while detaching. The peer-fault message is
secondary. That screen is not evidence of working gameplay or a GPU-only hang.
The unrelated cleanup wait and menu performance still need investigation.

HW18 raises the engine's explicit synchronization budget to 4,096 sections,
within the provider's existing supported range. It leaves the synchronization
algorithm and guest ABI unchanged. Each provider snapshot now reports live
sections and configured capacity, alongside the waiting-thread count. This
will show whether the new budget is sufficient or usage grows over time.
The pure process-plan test passes for both CPU ABIs; actual loading remains
unverified until the console run.

The private artifact builder records the two explicitly declared own entry
source deltas (plan and diagnostic output) with their before/after hashes.
Other compiled baseline inputs remain pinned. The x87 mutex and verified
unchanged-source tail-cycle optimization are retained. No resolution or menu
FPS improvement is claimed for this budget change.
