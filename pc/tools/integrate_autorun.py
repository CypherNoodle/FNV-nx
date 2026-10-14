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

def dedicated_launch(path):
    text=path.read_text(encoding="utf-8")
    marker="    /* FNV dedicated entry: never open the application library or downloader. */"
    if marker in text: return
    begin='    forwarded_game = runtime_forwarded_game( target, sizeof(target) );'
    end='    run_components_first( target, sizeof(target) );'
    if text.count(begin)!=1 or text.count(end)!=1: raise ValueError("Pinned launcher block changed")
    start=text.index(begin); stop=text.index(end,start)
    direct=marker+'''
    {
        char why[160], pending[512];
        snprintf(target, sizeof(target), "%s", argv[1]);
        if (read_first_line(WINE_NX_RUNTIME_ROOT "/run-next.txt", pending, sizeof(pending)) &&
            !strcmp(pending, target)) remove(WINE_NX_RUNTIME_ROOT "/run-next.txt");
        if (!horizon_dlls_ready(RUNTIME_DIR, horizon_dll_runtime_features,
                               sizeof(horizon_dll_runtime_features) / sizeof(horizon_dll_runtime_features[0]),
                               why, sizeof(why)))
        {
            PadState fnv_pad;
            log_line("[FNV-PC] runtime incomplete: %s", why);
            fnv_pc_note_failure(why);
            printf("Copia el paquete PC completo en la SD. Pulsa + para salir.\\n");
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
        }
        autorun = 1;
        log_line("[FNV-PC] dedicated target: %s", target);
    }

'''
    text=text[:start]+direct+text[stop:]
    text=text.replace('runtime_reopen_launcher = config_bool( "reopen-the-launcher-on-exit", 1, "reload-launcher.txt", 0 );',
                      'runtime_reopen_launcher = 0; /* dedicated game: exit to hbmenu */')
    restart='    if (!still_lent && (runtime_reopen_launcher || !access( WINE_NX_RUNTIME_ROOT "/run-next.txt", F_OK )) &&'
    if text.count(restart)!=1: raise ValueError("Pinned restart block changed")
    text=text.replace(restart,'    if (!still_lent && runtime_components_run && (runtime_reopen_launcher || !access( WINE_NX_RUNTIME_ROOT "/run-next.txt", F_OK )) &&')
    path.write_text(text,encoding="utf-8",newline="\n")

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
    log_line( "[FNV-PC] pc-probe02 Autorun ''' + PIN + '''" );
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
    # Upgrade the adapter from the first probe without touching engine code.
    old=(hw / "source/runtime.c").read_text(encoding="utf-8")
    if '[FNV-PC] pc-probe01 Autorun' in old:
        (hw / "source/runtime.c").write_text(old.replace('[FNV-PC] pc-probe01 Autorun','[FNV-PC] pc-probe02 Autorun'),encoding="utf-8",newline="\n")
    replace_once(hw / "source/runtime.c", anchor, hook)
    dedicated_launch(hw / "source/runtime.c")
    replace_once(hw / "source/runtime.c", 'else snprintf( own_nro, sizeof(own_nro), "%s", WINE_NX_RUNTIME_NRO );',
                 'else snprintf( own_nro, sizeof(own_nro), "%s", "sdmc:/switch/FNV-nx/fnv-nx.nro" );')
    replace_once(hw / "CMakeLists.txt", 'set_target_properties(wine-nx-runtime PROPERTIES SUFFIX ".elf")',
                 'target_sources(wine-nx-runtime PRIVATE source/fnv_pc_bootstrap.c)\nset_target_properties(wine-nx-runtime PROPERTIES SUFFIX ".elf")')
    replace_once(hw / "source/xinput_unix.c", '#include "input_profile.h"',
                 '#include "input_profile.h"\n#include "fnv_pc_bootstrap.h"')
    anchor = '        nx_xinput_map( mapped.buttons, mapped.axis[0], mapped.axis[1], mapped.axis[2], mapped.axis[3], &gamepad );'
    replace_once(hw / "source/xinput_unix.c", anchor, anchor + '\n        gamepad.wButtons = fnv_pc_map_buttons(gamepad.wButtons);')
    cmake=hw / "CMakeLists.txt"
    old=cmake.read_text(encoding="utf-8")
    if '"pc01" "${RUNTIME_NACP_FILE}"' in old:
        cmake.write_text(old.replace('"pc01" "${RUNTIME_NACP_FILE}"','"pc02" "${RUNTIME_NACP_FILE}"'),encoding="utf-8",newline="\n")
    replace_once(cmake, '--create "Autorun" "ticoverse.com" "0.0.1" "${RUNTIME_NACP_FILE}"',
                 '--create "FNV-nx PC (experimental)" "CypherNoodle / Autorun" "pc02" "${RUNTIME_NACP_FILE}"')
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
