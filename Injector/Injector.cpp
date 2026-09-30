#include <iostream>
#include <filesystem>
#include <format>
#include <functional>
#include <unordered_set>

#include <Windows.h>
#include <winternl.h>
#include <TlHelp32.h>
#include <psapi.h>
#include <shlobj_core.h>
#include "InjectorCore.h"

namespace fs = std::filesystem;

bool ReadRemote(HANDLE process, uintptr_t address, void* output, SIZE_T size)
{
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(process, reinterpret_cast<const void*>(address), output, size, &bytesRead) &&
        bytesRead == size;
}

uintptr_t FindRemoteModuleFromPeb(HANDLE process, const wchar_t* moduleName)
{
    using QueryProcess = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    auto queryProcess = reinterpret_cast<QueryProcess>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    PROCESS_BASIC_INFORMATION processInfo{};
    if (!queryProcess || !NT_SUCCESS(queryProcess(process, ProcessBasicInformation,
        &processInfo, sizeof(processInfo), nullptr))) return 0;

    struct PebPrefix { uint8_t Reserved[0x18]; uintptr_t Loader; } peb{};
    struct LoaderPrefix { uint8_t Reserved[0x10]; LIST_ENTRY Modules; } loader{};
    struct LoaderEntry {
        LIST_ENTRY LoadOrder;
        LIST_ENTRY MemoryOrder;
        LIST_ENTRY InitializationOrder;
        uintptr_t ModuleBase;
        uintptr_t EntryPoint;
        ULONG ImageSize;
        ULONG Padding;
        UNICODE_STRING FullName;
        UNICODE_STRING BaseName;
    } entry{};

    if (!ReadRemote(process, reinterpret_cast<uintptr_t>(processInfo.PebBaseAddress), &peb, sizeof(peb)) ||
        !peb.Loader || !ReadRemote(process, peb.Loader, &loader, sizeof(loader))) return 0;

    const uintptr_t listHead = peb.Loader + offsetof(LoaderPrefix, Modules);
    uintptr_t current = reinterpret_cast<uintptr_t>(loader.Modules.Flink);
    for (size_t count = 0; current && current != listHead && count < 128; ++count) {
        if (!ReadRemote(process, current, &entry, sizeof(entry))) return 0;
        if (entry.BaseName.Buffer && entry.BaseName.Length && entry.BaseName.Length < 1024) {
            std::wstring name(entry.BaseName.Length / sizeof(wchar_t), L'\0');
            if (ReadRemote(process, reinterpret_cast<uintptr_t>(entry.BaseName.Buffer),
                name.data(), entry.BaseName.Length) && _wcsicmp(name.c_str(), moduleName) == 0) {
                return entry.ModuleBase;
            }
        }
        current = reinterpret_cast<uintptr_t>(entry.LoadOrder.Flink);
    }
    return 0;
}

uintptr_t FindRemoteModule(HANDLE process, DWORD processId, const wchar_t* moduleName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W module = {};
        module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot, &module)) {
            do {
                if (_wcsicmp(module.szModule, moduleName) == 0) {
                    const auto result = reinterpret_cast<uintptr_t>(module.modBaseAddr);
                    CloseHandle(snapshot);
                    return result;
                }
            } while (Module32NextW(snapshot, &module));
        }
        CloseHandle(snapshot);
    }

    if (auto result = FindRemoteModuleFromPeb(process, moduleName)) return result;
    throw std::runtime_error("kernel32.dll was not available in the target process");
}

void InjectDll(HANDLE hProc, DWORD processId, const fs::path& dllPath)
{
    fs::path path = fs::absolute(dllPath);
    if (!fs::exists(path)) throw std::runtime_error("SpotifyOggDumper.dll was not found beside the injector");
    std::wstring pathW = path.wstring();
    SIZE_T pathLenBytes = (pathW.size() + 1) * sizeof(wchar_t);

    auto localKernel32 = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll"));
    auto localLoadLibrary = reinterpret_cast<uintptr_t>(GetProcAddress(
        reinterpret_cast<HMODULE>(localKernel32), "LoadLibraryW"));
    auto remoteKernel32 = FindRemoteModule(hProc, processId, L"kernel32.dll");
    auto remoteLoadLibrary = RebaseRemoteExport(localKernel32, localLoadLibrary, remoteKernel32);
    if (!remoteLoadLibrary) throw std::runtime_error("Could not resolve remote LoadLibraryW");

    LPVOID pathArgAddr = VirtualAllocEx(hProc, NULL, pathLenBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pathArgAddr) {
        throw std::runtime_error("Could not allocate memory in the target process");
    }
    HANDLE thread = nullptr;
    try {
        SIZE_T bytesWritten = 0;
        if (!WriteProcessMemory(hProc, pathArgAddr, pathW.c_str(), pathLenBytes, &bytesWritten) ||
            bytesWritten != pathLenBytes) {
            throw std::runtime_error("Could not write the DLL path to the target process");
        }

        thread = CreateRemoteThread(hProc, NULL, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoadLibrary), pathArgAddr, 0, NULL);
        if (!thread) throw std::runtime_error("Could not create the remote loader thread");

        DWORD waitResult = WaitForSingleObject(thread, 10000);
        if (waitResult == WAIT_TIMEOUT) throw std::runtime_error("Remote loader thread timed out");
        if (waitResult != WAIT_OBJECT_0) throw std::runtime_error("Could not wait for the remote loader thread");

        DWORD exitCode = 0;
        if (!GetExitCodeThread(thread, &exitCode) || exitCode == 0)
            throw std::runtime_error("Spotify rejected SpotifyOggDumper.dll");
    } catch (...) {
        if (thread) CloseHandle(thread);
        VirtualFreeEx(hProc, pathArgAddr, 0, MEM_RELEASE);
        throw;
    }
    CloseHandle(thread);
    VirtualFreeEx(hProc, pathArgAddr, 0, MEM_RELEASE);
}

void EnumProcessesEx(std::function<void(PROCESSENTRY32&)> visitor)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 proc = {};
    proc.dwSize = sizeof(proc);

    if (Process32First(snapshot, &proc)) {
        do {
            visitor(proc);
        } while (Process32Next(snapshot, &proc));
    }
    CloseHandle(snapshot);
}
bool HasOpenWindow(DWORD procId)
{
    struct Data { DWORD ProcId; HWND Win; };
    Data data = { procId, 0 };

    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto data = (Data*)param;
        DWORD parentProcId;
        GetWindowThreadProcessId(hwnd, &parentProcId);

        if (parentProcId == data->ProcId && GetWindow(hwnd, GW_OWNER) == 0 && IsWindowVisible(hwnd)) {
            data->Win = hwnd;
            return false;
        }
        return true;
    }, (LPARAM)&data);

    return data.Win != 0;
}
//Note: this will only work if both processes have the same word size (64 vs 32)
std::wstring GetProcessCommandLine(HANDLE hProc)
{
    typedef NTSTATUS(NTAPI* NtQueryInformationProcess_FuncType)(
        IN HANDLE ProcessHandle,
        ULONG ProcessInformationClass,
        OUT PVOID ProcessInformation,
        IN ULONG ProcessInformationLength,
        OUT PULONG ReturnLength OPTIONAL
    );
    static const auto _QueryProcInfo = (NtQueryInformationProcess_FuncType)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "NtQueryInformationProcess");

    PROCESS_BASIC_INFORMATION info;
    if (!NT_SUCCESS(_QueryProcInfo(hProc, ProcessBasicInformation, &info, sizeof(info), NULL))) {
        throw std::exception("Failed to get process information");
    }
    PEB peb;
    RTL_USER_PROCESS_PARAMETERS procParams;
    ReadProcessMemory(hProc, info.PebBaseAddress, &peb, sizeof(peb), NULL);
    ReadProcessMemory(hProc, peb.ProcessParameters, &procParams, sizeof(procParams), NULL);

    std::wstring cmdLine(procParams.CommandLine.Length, '\0');
    ReadProcessMemory(hProc, procParams.CommandLine.Buffer, cmdLine.data(), cmdLine.size(), NULL);
    return cmdLine;
}

#define COL_RED     "\033[1;91m"
#define COL_GREEN   "\033[1;92m"
#define COL_YELLOW  "\033[1;93m"
#define COL_BLUE    "\033[1;94m"
#define COL_RESET   "\033[0m"

HANDLE FindSpotifyProcess()
{
    DWORD procId = 0;
    
    EnumProcessesEx([&](auto proc) {
        if (_wcsicmp(proc.szExeFile, L"Spotify.exe") == 0 && HasOpenWindow(proc.th32ProcessID)) {
            procId = proc.th32ProcessID;
        }
    });
    if (procId == 0) {
        return INVALID_HANDLE_VALUE;
    }
    return OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION |  PROCESS_VM_READ |  PROCESS_VM_WRITE, 
        false, procId
    );
}
struct ProcessTarget
{
    HANDLE Process = INVALID_HANDLE_VALUE;
    HANDLE MainThread = nullptr;
    DWORD ProcessId = 0;
    bool LaunchedByInjector = false;
    bool MainThreadSuspended = false;
};

ProcessTarget LaunchSpotifyProcess(bool enableRemoteDebug)
{
    fs::path exePath = fs::absolute("Spotify/Spotify.exe");
    if (!fs::exists(exePath)) {
        PWSTR appData;
        SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &appData);
        exePath = fs::path(appData) / "Spotify/Spotify.exe";
    }
    if (!fs::exists(exePath)) {
        throw std::exception("Spotify installation not found.");
    }
    fs::path workDir = exePath.parent_path();
    std::cout << "Launching Spotify...\n";

    PROCESS_INFORMATION proc = {};
    STARTUPINFO startInfo = {};
    startInfo.cb = sizeof(STARTUPINFO);

    std::wstring cmdLine = L"\"" + exePath.wstring() + L"\" ";
    if (enableRemoteDebug) {
        cmdLine += L" --remote-debugging-port=9222";
    }

    if (!CreateProcess(exePath.c_str(), cmdLine.data(), NULL, NULL, false, DEBUG_ONLY_THIS_PROCESS,
        NULL, workDir.c_str(), &startInfo, &proc)) {
        throw std::runtime_error("Could not start Spotify process");
    }

    uintptr_t applicationEntry = 0;
    uint8_t entryByte = 0;
    bool entryBreakpointInstalled = false;
    bool stoppedAtEntry = false;
    DEBUG_EVENT event{};
    while (WaitForDebugEvent(&event, 10000)) {
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            auto imageBase = reinterpret_cast<uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage);
            IMAGE_DOS_HEADER dos{};
            IMAGE_NT_HEADERS64 nt{};
            if (ReadRemote(proc.hProcess, imageBase, &dos, sizeof(dos)) &&
                dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
                ReadRemote(proc.hProcess, imageBase + dos.e_lfanew, &nt, sizeof(nt)) &&
                nt.Signature == IMAGE_NT_SIGNATURE) {
                applicationEntry = imageBase + nt.OptionalHeader.AddressOfEntryPoint;
            }
        }
        if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)
            CloseHandle(event.u.LoadDll.hFile);

        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
            const auto exceptionAddress = reinterpret_cast<uintptr_t>(
                event.u.Exception.ExceptionRecord.ExceptionAddress);
            if (!entryBreakpointInstalled) {
                uint8_t breakpoint = 0xcc;
                SIZE_T transferred = 0;
                if (!applicationEntry ||
                    !ReadRemote(proc.hProcess, applicationEntry, &entryByte, sizeof(entryByte)) ||
                    !WriteProcessMemory(proc.hProcess, reinterpret_cast<void*>(applicationEntry),
                        &breakpoint, sizeof(breakpoint), &transferred) || transferred != sizeof(breakpoint)) {
                    ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
                    break;
                }
                FlushInstructionCache(proc.hProcess, reinterpret_cast<void*>(applicationEntry), 1);
                entryBreakpointInstalled = true;
            } else if (exceptionAddress == applicationEntry) {
                SIZE_T transferred = 0;
                WriteProcessMemory(proc.hProcess, reinterpret_cast<void*>(applicationEntry),
                    &entryByte, sizeof(entryByte), &transferred);
                FlushInstructionCache(proc.hProcess, reinterpret_cast<void*>(applicationEntry), 1);

                CONTEXT context{};
                context.ContextFlags = CONTEXT_CONTROL;
                if (transferred != sizeof(entryByte) || !GetThreadContext(proc.hThread, &context) ||
                    SuspendThread(proc.hThread) == DWORD(-1)) {
                    ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
                    break;
                }
                context.Rip = applicationEntry;
                if (!SetThreadContext(proc.hThread, &context)) {
                    ResumeThread(proc.hThread);
                    ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE);
                    break;
                }
                stoppedAtEntry = true;
            }
        }

        const bool breakpointEvent = event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT &&
            event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT;
        const DWORD continueStatus = event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT && !breakpointEvent
            ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId,
            stoppedAtEntry ? DBG_CONTINUE : continueStatus);
        if (stoppedAtEntry) break;
        if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) break;
    }

    if (!stoppedAtEntry || !DebugActiveProcessStop(proc.dwProcessId)) {
        TerminateProcess(proc.hProcess, 1);
        CloseHandle(proc.hThread);
        CloseHandle(proc.hProcess);
        throw std::runtime_error("Could not stop Spotify before its application entry point");
    }
    return { proc.hProcess, proc.hThread, proc.dwProcessId, true, true };
}

void KillSpotifyProcesses()
{
    EnumProcessesEx([&](auto proc) {
        if (_wcsicmp(proc.szExeFile, L"Spotify.exe") != 0) return;

        auto handle = OpenProcess(PROCESS_TERMINATE, false, proc.th32ProcessID);
        TerminateProcess(handle, 0);
        WaitForSingleObject(handle, 3000);
        CloseHandle(handle);
    });
}
//Delete `%localappdata%/Spotify/Update` to prevent Spotify from auto updating
void DeleteSpotifyUpdate()
{
    PWSTR localAppData;
    SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData);

    auto updateDir = fs::path(localAppData) / "Spotify/Update";
    std::error_code errCode;
    //remove_all() returns 0 if the path doesn't exist, -1 on error.
    if (fs::remove_all(updateDir, errCode) == static_cast<uintmax_t>(-1)) {
        std::cout << COL_YELLOW "Warn: Failed to delete Spotify update (%localappdata%/Spotify/Update). You may need to re-run Install.ps1 to downgrade.\n" COL_RESET;
    }
    CoTaskMemFree(localAppData);
}

void EnableAnsiColoring()
{
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD currMode;
    GetConsoleMode(handle, &currMode);
    SetConsoleMode(handle, currMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

HANDLE CreateInitializationEvent(DWORD processId)
{
    const auto name = std::format(L"Local\\SoggfyInitReady-{}", processId);
    return CreateEventW(nullptr, TRUE, FALSE, name.c_str());
}

int main(int argc, char* argv[])
{
    auto options = ParseInjectorOptions(argc, argv);
    EnableAnsiColoring();

    ProcessTarget target;
    try {
        HANDLE existingProcess = FindSpotifyProcess();
        if (options.ForceLaunch || existingProcess == INVALID_HANDLE_VALUE) {
            KillSpotifyProcesses();
            DeleteSpotifyUpdate();
            target = LaunchSpotifyProcess(options.EnableRemoteDebug);
        } else {
            target.Process = existingProcess;
            target.ProcessId = GetProcessId(existingProcess);
            std::cout << COL_YELLOW
                << "Attaching to an already running Spotify process. Use -l for early browser integration.\n"
                << COL_RESET;
        }
        std::cout << "Injecting dumper dll into Spotify process (" << target.ProcessId << ")...\n";

        HANDLE initializationEvent = target.MainThreadSuspended
            ? CreateInitializationEvent(target.ProcessId) : nullptr;
        try {
            InjectDll(target.Process, target.ProcessId, L"SpotifyOggDumper.dll");
        } catch (...) {
            if (initializationEvent) CloseHandle(initializationEvent);
            throw;
        }
        if (target.MainThreadSuspended) {
            if (initializationEvent) {
                WaitForSingleObject(initializationEvent, 3000);
                CloseHandle(initializationEvent);
            }
            if (ResumeThread(target.MainThread) == DWORD(-1))
                throw std::runtime_error("Could not start Spotify after injection");
            target.MainThreadSuspended = false;
        }
        if (target.MainThread) CloseHandle(target.MainThread);
        CloseHandle(target.Process);
        target = {};
        
        std::cout << COL_GREEN "Injection succeeded!\n" COL_RESET;
    } catch (std::exception& ex) {
        if (target.LaunchedByInjector && target.Process != INVALID_HANDLE_VALUE) {
            TerminateProcess(target.Process, 1);
            WaitForSingleObject(target.Process, 3000);
        }
        if (target.MainThread) CloseHandle(target.MainThread);
        if (target.Process != INVALID_HANDLE_VALUE) CloseHandle(target.Process);
        std::cout << COL_RED "Error: " << ex.what() << "\n" COL_RESET;
    }
    
    for (int i = 5; i > 0; i--) {
        std::cout << "Exiting in " << i << "s...\r";
        Sleep(1000);
    }
    return 0;
}
