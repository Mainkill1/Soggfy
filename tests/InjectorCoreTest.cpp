#include "../Injector/InjectorCore.h"

#include <cassert>
#include <cstdint>

int main()
{
    auto defaults = ParseInjectorOptions(1, nullptr);
    assert(!defaults.ForceLaunch && !defaults.EnableRemoteDebug);

    const char* arguments[] = { "Injector.exe", "-l", "-d" };
    auto selected = ParseInjectorOptions(3, arguments);
    assert(selected.ForceLaunch && selected.EnableRemoteDebug);

    assert(RebaseRemoteExport(0x10000000, 0x10001234, 0x70000000) == 0x70001234);
    assert(RebaseRemoteExport(0x180000000, 0x180001000, 0x7ffb00000000) == 0x7ffb00001000);
    assert(RebaseRemoteExport(0, 0x1000, 0x2000) == 0);
    assert(RebaseRemoteExport(0x2000, 0x1000, 0x3000) == 0);
    assert(RebaseRemoteExport(0x1000, 0x2000, 0) == 0);
}
