#include "pch.h"
#include <thread>
#include "StateManager.h"
#include "Utils/Log.h"
#include "Utils/Hooks.h"
#include "Utils/Utils.h"
#include "CefUtils.h"
#include "SpotifyOggHook.h"

HMODULE _selfModule;
std::shared_ptr<StateManager> _stateMgr;

HMODULE WaitForModule(const wchar_t* name, DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    for (;;) {
        if (auto module = GetModuleHandleW(name)) return module;
        if (GetTickCount() - start >= timeoutMs) return nullptr;
        Sleep(10);
    }
}

void SignalInjectorReady()
{
    const auto name = std::format(L"Local\\SoggfyInitReady-{}", GetCurrentProcessId());
    if (HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str())) {
        SetEvent(event);
        CloseHandle(event);
    }
}

void InstallHooks()
{
    SignalInjectorReady();

    HMODULE spotify = WaitForModule(L"Spotify.dll", 30000);
    HMODULE cef = WaitForModule(L"libcef.dll", 30000);
    if (!spotify) throw std::runtime_error("Spotify.dll did not load");
    if (!cef) throw std::runtime_error("libcef.dll did not load");

    InstallSpotifyOggHook(spotify, [](const OggPageView& page) {
        auto state = _stateMgr;
        if (state) state->ReceiveOggPage(page);
    });
    CefUtils::InitUrlBlocker([&](auto url) { return _stateMgr && _stateMgr->IsUrlBlocked(url); });
    Hooks::EnableAll();
}

std::filesystem::path GetModulePath(HMODULE module)
{
    //https://stackoverflow.com/a/33613252
    std::vector<wchar_t> pathBuf;
    DWORD copied = 0;
    do {
        pathBuf.resize(pathBuf.size() + MAX_PATH);
        copied = GetModuleFileName(module, &pathBuf.at(0), pathBuf.size());
    } while (copied >= pathBuf.size());
    pathBuf.resize(copied);

    return std::filesystem::path(pathBuf.begin(), pathBuf.end());
}

std::string GetFileVersion(const std::wstring& fn)
{
    int versionDataLen = GetFileVersionInfoSize(fn.c_str(), NULL);
    auto versionData = std::make_unique<uint8_t[]>(versionDataLen);
    GetFileVersionInfo(fn.c_str(), 0, versionDataLen, versionData.get());

    VS_FIXEDFILEINFO* info;
    UINT infoLen;
    VerQueryValue(versionData.get(), L"\\", (LPVOID*)&info, &infoLen);

    return std::format(
        "{}.{}.{}.{}",
        HIWORD(info->dwFileVersionMS),
        LOWORD(info->dwFileVersionMS),
        HIWORD(info->dwFileVersionLS),
        LOWORD(info->dwFileVersionLS)
    );
}

void Exit()
{
    LogInfo("Uninstalling...");

    Hooks::DisableAll();
    StopSpotifyOggHook();

    if (_stateMgr) {
        _stateMgr->Shutdown();
        _stateMgr = nullptr;
    }

    CloseLogger();
    FreeLibraryAndExitThread(_selfModule, 0);
}

DWORD WINAPI Init(LPVOID param)
{
    fs::path moduleDir = GetModulePath(_selfModule).parent_path();
    fs::path dataDir = Utils::GetLocalAppDataFolder() / "Soggfy";

    // Portable config heuristic: config.json placed next to DLL or injected launch with no persistent config 
    if (fs::exists(moduleDir / "config.json") || (!fs::exists(moduleDir / "Spotify.exe") && !fs::exists(dataDir / "config.json"))) {
        dataDir = moduleDir;
    }
    else if (!fs::exists(dataDir)) {
        fs::create_directories(dataDir);
    }
    
    bool logToCon = true;
    fs::path logFile = dataDir / "log.txt";
#if NDEBUG
    logToCon = fs::exists(dataDir / "_debug.txt");
    LogMinLevel = logToCon ? LOG_TRACE : LOG_DEBUG;
#endif

    std::string spotifyVersion = "<unknown>";

    try {
        InitLogger(logToCon, logFile);
        
        _stateMgr = StateManager::New(dataDir, moduleDir);
    
        spotifyVersion = GetFileVersion(L"Spotify.exe");
        LogInfo("Spotify version: {}", spotifyVersion);

        InstallHooks();

        std::thread(&StateManager::RunControlServer, _stateMgr).detach();
    } catch (std::exception& ex) {
        auto msg = std::format(
            "Failed to initialize Soggfy: {}\n\n"
            "This likely means that the Spotify version you are using ({}) is not supported.\n"
            "Try updating Soggfy, or downgrading Spotify to the supported version.",
            ex.what(), spotifyVersion
        );
        LogError("{}", msg);
        MessageBoxA(NULL, msg.c_str(), "Soggfy", MB_ICONERROR);
        Exit();
    }
    LogInfo("Hooks were successfully installed.");

    if (logToCon) {
        while (true) {
            auto ch = std::tolower(std::cin.get());

            if (ch == 'l') {
                LogMinLevel = (LogLevel)(LogMinLevel == LOG_TRACE ? LOG_INFO : LogMinLevel - 1); // cycle through [INFO, DEBUG, TRACE]
                LogInfo("Min log level set to {}", (int)LogMinLevel);
            }
            if (ch == 'u') break;
        }
        Exit();
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD  ul_reason_for_call,
                      LPVOID lpReserved
)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        _selfModule = hModule;
        CreateThread(NULL, 0, Init, NULL, 0, NULL);
    }
    return TRUE;
}
