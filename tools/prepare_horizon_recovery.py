"""Install exact-range watch batching and failed storage-device diagnostics.

No broadened guest permissions, disabled watches, or fake storage successes.
"""
import argparse
from pathlib import Path


def prepare(sdk):
    file = sdk / 'src/core/guest_memory_switch.cpp'
    text = file.read_text()
    marker = '#include "fnv_protect_runs.inc"'
    if marker not in text:
        start = text.index('    for (size_t p = begin; p < end; p += kPageSize) {',
                           text.index('bool RexGmProtect('))
        end = text.index('    return true;\n}', start)
        text = text[:start] + '    ' + marker + '\n' + text[end:]
        anchor = '    uint32_t last_result = 0;'
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '\n    uint64_t fnv_protect_runs = 0;\n'
                            '    uint64_t fnv_protect_pages = 0;\n'
                            '    uint64_t fnv_protect_failures = 0;')
        text = text.replace('#include <malloc.h>', '#include <malloc.h>\n#include <cstdio>')
        old = 'bool ProtectWithPermissions(uint8_t* page, bool read_only) {'
        assert text.count(old) == 1
        text = text.replace(old, 'bool ProtectWithPermissions(uint8_t* page, bool read_only, '
                            'size_t length = kPageSize) {')
        text = text.replace('reinterpret_cast<u64>(page), kPageSize, read_only ? Perm_R : Perm_Rw',
                            'reinterpret_cast<u64>(page), length, read_only ? Perm_R : Perm_Rw')
        file.write_text(text)
    file.with_name('fnv_protect_runs.inc').write_bytes(
        Path(__file__).with_name('horizon_protect_runs.inc').read_bytes())
    text = file.read_text()
    stats = '''extern "C" void RexFnvWatchStats(uint64_t* values) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    values[0] = s.protection.size();
    values[1] = s.chunks.size();
    values[2] = s.view_mapped.size();
    values[3] = s.fnv_protect_runs;
    values[4] = s.fnv_protect_pages;
    values[5] = s.fnv_protect_failures;
}

'''
    if stats not in text:
        text = text.replace('uint32_t RexGmLastResult(void) {', stats + 'uint32_t RexGmLastResult(void) {')
        file.write_text(text)
    file = sdk / 'src/ui/switch_perf.cpp'
    text = file.read_text()
    if 'RexFnvWatchStats' not in text:
        anchor = 'size_t RexGmMappedBytes(void);'
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '\nvoid RexFnvWatchStats(uint64_t* values);')
        anchor = '  // Work the game sends to the GPU, per presented frame.'
        assert text.count(anchor) == 1
        text = text.replace(anchor, '''  uint64_t watch_stats[6]{};
  RexFnvWatchStats(watch_stats);
  std::fprintf(f, "     FNV watches: %llu pages, %llu chunks, %llu mapped views; "
                  "cumulative protect runs %llu, pages %llu, failures %llu\\n",
                  (u64)watch_stats[0], (u64)watch_stats[1], (u64)watch_stats[2],
                  (u64)watch_stats[3], (u64)watch_stats[4], (u64)watch_stats[5]);
''' + anchor)
        file.write_text(text)

    file = sdk / 'src/core/exception_handler_switch.cpp'
    text = file.read_text()
    declaration = '''// FNV-nx: one-fault authorization, scoped to the faulting thread.
static thread_local uint64_t g_fnv_shadow_write = 0;
extern "C" void RexFnvAuthorizeShadowWrite(uint64_t address) {
  g_fnv_shadow_write = address;
}
'''
    if declaration not in text:
        text = text.replace('#include "rex_decode.h"', '#include "rex_decode.h"\n#include "fnv_shadow_write.h"')
        text = text.replace('namespace rex::arch {', declaration + '\nnamespace rex::arch {', 1)
        text = text.replace('  const bool is_write = data_abort && ((esr >> 6) & 1);',
                            '  const bool is_write = data_abort && ((esr >> 6) & 1);\n'
                            '  g_fnv_shadow_write = 0;')
        anchor = '      WriteBack(ex, tc, ctx);\n'
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '''
      if (g_fnv_shadow_write) {
        const uint64_t authorized = std::exchange(g_fnv_shadow_write, uint64_t(0));
        RexAccess acc{};
        const uint32_t insn = *reinterpret_cast<const uint32_t*>(ctx->pc.x);
        if (in_window && is_write && ctx->pc.x == pc && RexDecodeAccess(ctx, insn, &acc) &&
            FnvShadowStoreSpan(acc.kind == REX_ACC_STORE, acc.address,
                size_t(acc.bytes) * (acc.rt2 != 0xFF ? 2 : 1), far, authorized) &&
            EmulateViaShadow(ctx)) {
          RexSwitchPerfCount(3);
          RexResumeFromException(ctx);
        }
        break; // Fail closed; never retry a failed remap or bypass unsupported atomics.
      }
''')
        file.write_text(text)
    file.with_name('fnv_shadow_write.h').write_bytes(
        Path(__file__).with_name('horizon_shadow_write.h').read_bytes())

    file = sdk / 'src/system/xmemory.cpp'
    text = file.read_text()
    marker = 'RexFnvAuthorizeShadowWrite'
    if marker not in text:
        anchor = 'namespace rex::memory {'
        text = text.replace(anchor, '''#if REX_PLATFORM_SWITCH
extern "C" void RexFnvAuthorizeShadowWrite(uint64_t address);
extern "C" void* RexGmShadowFor(uint64_t address);
#endif

''' + anchor, 1)
        anchor = '''        return true;
      }
    }

    uint32_t current_guest_protect = 0;'''
        assert text.count(anchor) == 1
        text = text.replace(anchor, '''        return true;
      }
#if REX_PLATFORM_SWITCH
      // GPU watch callbacks ran first, and guest allocation metadata authorizes
      // this write. A failed host remap does not revoke that guest permission.
      // The exception handler still validates the exact decoded store span.
      if (RexGmShadowFor(reinterpret_cast<uint64_t>(host_address))) {
        static thread_local uint64_t recovered = 0;
        ++recovered;
        if (recovered <= 4 || !(recovered % 4096)) {
          REXSYS_WARN("FNV authorized shadow write after failed remap: guest={:08X} count={}",
                      virtual_address, recovered);
        }
        RexFnvAuthorizeShadowWrite(reinterpret_cast<uint64_t>(host_address));
        return true;
      }
#endif
    }

    uint32_t current_guest_protect = 0;''')
        file.write_text(text)

    file = sdk / 'src/kernel/xam/xam_content_device.cpp'
    text = file.read_text()
    for function in ('Name', 'State', 'Data'):
        warning = f'    REXKRNL_WARN("FNV storage GetDevice{function}: disconnected device {{:08X}}", uint32_t(device_id));'
        if warning in text:
            continue
        start = text.index(f'u32 XamContentGetDevice{function}_entry(')
        pos = text.index('  if (device_info == nullptr) {', start)
        pos += len('  if (device_info == nullptr) {')
        text = text[:pos] + '\n' + warning + text[pos:]
    file.write_text(text)
    print('Prepared exact-range watches, authorized store recovery, failed device diagnostics')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    prepare(parser.parse_args().sdk)
