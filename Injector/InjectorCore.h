#pragma once

#include <cstdint>
#include <cstring>

struct InjectorOptions
{
    bool ForceLaunch = false;
    bool EnableRemoteDebug = false;
};

inline InjectorOptions ParseInjectorOptions(int argc, const char* const* argv)
{
    InjectorOptions options;
    if (!argv) return options;
    for (int index = 1; index < argc; ++index) {
        options.ForceLaunch |= std::strcmp(argv[index], "-l") == 0;
        options.EnableRemoteDebug |= std::strcmp(argv[index], "-d") == 0;
    }
    return options;
}

inline uintptr_t RebaseRemoteExport(uintptr_t localModule, uintptr_t localExport, uintptr_t remoteModule)
{
    if (!localModule || !remoteModule || localExport < localModule) return 0;
    return remoteModule + (localExport - localModule);
}
