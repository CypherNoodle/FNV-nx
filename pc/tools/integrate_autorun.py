#!/usr/bin/env python3
"""Apply the FNV entry adapter to the pinned, local Autorun source tree."""
import argparse
import shutil
from pathlib import Path

PIN = "cbb0e4e6e7fa7f47f328d92441f51bfb507f266f"

def replace_once(path, anchor, replacement):
    text = path.read_text(encoding="utf-8")
    if replacement in text:
        return
    if text.count(anchor) != 1:
        raise ValueError(f"Pinned source anchor changed: {path.name}")
    path.write_text(text.replace(anchor, replacement), encoding="utf-8", newline="\n")

def integrate(root):
    pc = Path(__file__).resolve().parents[1]
    hw = root / "horizon-wine"
    if not (hw / "source/runtime.c").is_file():
        raise ValueError("Not an Autorun source directory")
    for name in ("fnv_pc_bootstrap.c", "fnv_pc_bootstrap.h"):
        shutil.copyfile(pc / "source" / name, hw / "source" / name)
    replace_once(hw / "source/runtime.c", '#include "fex_options.h"',
                 '#include "fnv_pc_bootstrap.h"\n#include "fex_options.h"')
    anchor = '    log_line( "[LOWVA] forwarder title %016llx", runtime_title_id() );'
    hook = anchor + '''
    log_line( "[FNV-PC] pc-probe01 Autorun ''' + PIN + '''" );
    if (!fnv_pc_bootstrap(&argc, &argv))
    {
        PadState fnv_pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&fnv_pad);
        while (appletMainLoop())
        {
            padUpdate(&fnv_pad);
            if (padGetButtonsDown(&fnv_pad) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
            svcSleepThread(16000000);
        }
        consoleExit(NULL);
        leave_cleanly();
        return 0;
    }'''
    replace_once(hw / "source/runtime.c", anchor, hook)
    replace_once(hw / "CMakeLists.txt", 'set_target_properties(wine-nx-runtime PROPERTIES SUFFIX ".elf")',
                 'target_sources(wine-nx-runtime PRIVATE source/fnv_pc_bootstrap.c)\nset_target_properties(wine-nx-runtime PROPERTIES SUFFIX ".elf")')
    replace_once(hw / "source/xinput_unix.c", '#include "input_profile.h"',
                 '#include "input_profile.h"\n#include "fnv_pc_bootstrap.h"')
    anchor = '        nx_xinput_map( mapped.buttons, mapped.axis[0], mapped.axis[1], mapped.axis[2], mapped.axis[3], &gamepad );'
    replace_once(hw / "source/xinput_unix.c", anchor, anchor + '\n        gamepad.wButtons = fnv_pc_map_buttons(gamepad.wButtons);')
    replace_once(hw / "CMakeLists.txt", '--create "Autorun" "ticoverse.com" "0.0.1" "${RUNTIME_NACP_FILE}"',
                 '--create "FNV-nx PC (experimental)" "CypherNoodle / Autorun" "pc01" "${RUNTIME_NACP_FILE}"')
    icon = pc.parent / "icon.jpg"
    if icon.is_file():
        shutil.copyfile(icon,hw / "assets/fnv-pc.jpg")
        replace_once(hw / "CMakeLists.txt", '"--icon=${CMAKE_CURRENT_SOURCE_DIR}/assets/autorun.jpg" ${RUNTIME_ROMFS_ARG}',
                     '"--icon=${CMAKE_CURRENT_SOURCE_DIR}/assets/fnv-pc.jpg" ${RUNTIME_ROMFS_ARG}')
        replace_once(hw / "CMakeLists.txt", 'DEPENDS wine-nx-runtime "${RUNTIME_NACP_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/autorun.jpg"',
                     'DEPENDS wine-nx-runtime "${RUNTIME_NACP_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/fnv-pc.jpg"')
    # The verified codeload archive has no .git. Keep the upstream base identity
    # explicit; FNV adapter hashes are recorded separately in our package.
    anchor = "'wine': subprocess.check_output(['git', '-C', str(wine_source), 'rev-parse', 'HEAD'], text=True).strip(),"
    replace_once(hw / "tools/package-amd64.py", anchor,
                 "'wine': os.environ.get('WINE_NX_SOURCE_COMMIT') or subprocess.check_output(['git', '-C', str(wine_source), 'rev-parse', 'HEAD'], text=True).strip(),")
    print("FNV adapter integrated; upstream boot payloads and Steam files untouched.")

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--autorun", required=True, type=Path)
    integrate(ap.parse_args().autorun.resolve())
