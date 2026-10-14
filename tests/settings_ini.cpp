#include "settings_ini.h"
#include <cassert>
#include <iostream>
int main() {
    const auto good = fnv::ParseSettingsIni("\xEF\xBB\xBFversion=1\r\n[graficos]\nresolucion=960x540\nvsync=relaxed\n[cpu]\nprincipal=2\naudio=auto\n[pantalla]\nmostrar_fps=true ; comentario\n");
    assert(good.valid_version && good.errors.empty() && good.flags.size() == 7);
    assert(good.main_core == 2 && good.audio_core == -1);
    for (const auto& [name, value] : good.flags) { (void)value; assert(name != "vsync" && name != "vblank_per_swap"); }
    const auto bad = fnv::ParseSettingsIni("version=9\n[cpu]\nprincipal=3\ncomandos_gpu=-1\n[graficos]\nresolucion=1000x720\nvsync=off\nvsync=on\n[pantalla]\nmostrar_fps=yes\ninventado=true\n");
    assert(!bad.valid_version && bad.errors.size() == 7 && !bad.main_core && !bad.gpu_core);
    assert(bad.flags.size() == 3);
    const auto fsi = fnv::ParseSettingsIni("version=1\n[optimizacion]\nfsi_interlock_tardio=true\n");
    assert(fsi.errors.empty() && fsi.flags.size() == 1);
    assert(fsi.flags[0].first == "fnv_fsi_late_opaque" && fsi.flags[0].second == "true");
    std::cout << "PASS INI: UTF8/CRLF, comments, mapping, invalid values, duplicates, version, core range, vblank unchanged\n";
}
