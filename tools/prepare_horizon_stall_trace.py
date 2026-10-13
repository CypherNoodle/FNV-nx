"""Capture blocked-thread stacks and original save I/O entry/exit, without waking waiters."""
import argparse
from pathlib import Path


def prepare(sdk):
    path = sdk / 'src/ui/switch_perf.cpp'
    text = path.read_text()
    marker = '// FNV alpha11: separate bounded sampling for a stalled main and empty GPU ring.'
    if marker not in text:
        anchor = '  ApplyExclusiveCore(f, order);'
        assert text.count(anchor) == 1
        block = '''  // FNV alpha11: separate bounded sampling for a stalled main and empty GPU ring.
  static bool fnv_seen_frames = false, fnv_stall_sampled = false;
  static unsigned fnv_stall_windows = 0;
  if (counters_now[0] != counters_last[0]) fnv_seen_frames = true;
  bool fnv_main_idle = false, fnv_ring_idle = false;
  for (size_t i : order) {
    const Slot& slot = g_slots[i];
    if (std::strncmp(slot.name, "Main XThread", 12) == 0) fnv_main_idle = slot.cpu < 1.0;
    if (std::strncmp(slot.name, "GPU Commands", 12) == 0) fnv_ring_idle = slot.cpu < 1.0;
  }
  const bool fnv_stall_enabled = rex::cvar::GetFlagByName("fnv_save_trace") == "true";
  const bool fnv_stall_candidate = fnv_stall_enabled && fnv_seen_frames && fnv_fps == 0.0 &&
                                  fnv_main_idle && fnv_ring_idle;
  fnv_stall_windows = fnv_stall_candidate ? fnv_stall_windows + 1 : 0;
  if (!fnv_stall_sampled && fnv_stall_windows >= 2) {
    fnv_stall_sampled = true;
    g_fnv_sample_started = true;
    g_fnv_sample_until = armGetSystemTick() + 30 * tick_freq;
    std::fprintf(f, "FNV stalled wait sampling: begin 30 seconds after two zero-frame intervals; "
                    "observing stacks, NOT releasing waits\\n");
  }
'''
        text = text.replace(anchor, block + anchor)
        path.write_text(text)
    path = sdk / 'src/system/xmemory.cpp'
    text = path.read_text()
    old = '''      REXSYS_ERROR(
          "BaseHeap::AllocFixed attempting to reserve an already reserved "
          "range");'''
    new = '''      auto* fnv_state = rex::runtime::ThreadState::Get();
      REXSYS_ERROR("BaseHeap::AllocFixed attempting to reserve an already reserved range: "
                   "base={:08X} size={:08X} heap={:08X} page_size={:X} page={} state={:X} caller={:08X}",
                   base_address, size, heap_base_, page_size_, page_number, state,
                   fnv_state ? uint32_t(fnv_state->context()->lr) : 0);'''
    if old in text:
        assert text.count(old) == 1
        path.write_text(text.replace(old, new))
    else: assert new in text, 'Unsupported AllocFixed diagnostic source'
    path = sdk / 'src/kernel/xboxkrnl/xboxkrnl_io.cpp'
    text = path.read_text()
    if 'FNV save read enter:' not in text:
        anchor = '      result = file->Read(buffer.guest_address(), buffer_length,'
        assert text.count(anchor) == 1
        text = text.replace(anchor, '''      const bool fnv_save_read = rex::cvar::GetFlagByName("fnv_save_trace") == "true" &&
                                 file->path().find("Savegame.dat") != std::string::npos;
      if (fnv_save_read) REXKRNL_INFO("FNV save read enter: handle={:08X} bytes={} offset={} "
                                     "event={:08X} apc={:08X} sync={}",
                                     uint32_t(file_handle), uint32_t(buffer_length), displacement,
                                     uint32_t(event_handle), apc_routine_ptr.guest_address(), file->is_synchronous());
''' + anchor)
        anchor = '      const uint64_t io_us_end = NowUs();'
        pos = text.index(anchor, text.index('FNV save read enter:'))
        text = text[:pos] + '''      if (fnv_save_read) REXKRNL_INFO("FNV save read return: handle={:08X} result={:08X} bytes={}",
                                      uint32_t(file_handle), uint32_t(result), bytes_read);
''' + text[pos:]
        path.write_text(text)
    print('PASS: stall sampling and save/reservation diagnostics; original wait/read results unchanged')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    prepare(p.parse_args().sdk)
