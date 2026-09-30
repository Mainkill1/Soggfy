#include "pch.h"
#include "SpotifyOggHook.h"
#include "SpotifyHookSupport.h"
#include "Utils/Hooks.h"
#include "Utils/Log.h"

#include <array>
#include <atomic>
#include <memory>

namespace {

struct NativeOggPage
{
    uint8_t* Header;
    int32_t HeaderLength;
    uint8_t* Body;
    int32_t BodyLength;
};
static_assert(sizeof(NativeOggPage) == 32 && offsetof(NativeOggPage, Body) == 16,
    "Spotify 1.3.1.234 uses the Windows x64 libogg page layout");

using PageSeek = int32_t(*)(void*, NativeOggPage*);
PageSeek OriginalPageSeek = nullptr;

constexpr size_t MaximumPageBytes = 65307;
constexpr size_t QueueCapacity = 512;

struct PageSlot
{
    uintptr_t Context = 0;
    uint16_t HeaderLength = 0;
    uint32_t BodyLength = 0;
    std::array<uint8_t, MaximumPageBytes> Bytes{};
};

std::unique_ptr<PageSlot[]> Queue;
size_t QueueHead = 0;
size_t QueueTail = 0;
size_t QueueCount = 0;
SRWLOCK QueueLock = SRWLOCK_INIT;
HANDLE QueueEvent = nullptr;
HANDLE WorkerThread = nullptr;
std::atomic<bool> Running = false;
std::function<void(const OggPageView&)> PageSink;
std::atomic<uint64_t> DroppedPages = 0;
std::atomic<bool> CaptureConfirmed = false;

uint8_t* ValidateSpotifyModule(HMODULE module)
{
    auto base = reinterpret_cast<uint8_t*>(module);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) {
        throw std::runtime_error("Spotify.dll has an invalid PE header");
    }
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < SpotifyOggParserSignature.size()) {
        throw std::runtime_error("Spotify.dll is not the supported x64 image");
    }

    const auto imageSize = size_t(nt->OptionalHeader.SizeOfImage);
    auto parser = FindUniqueSpotifyParser(base, imageSize);
    if (!parser) {
        throw std::runtime_error("Spotify Ogg parser signature was not found exactly once");
    }

    const auto parserRva = size_t(parser - base);
    bool executable = false;
    auto section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        const size_t sectionStart = section->VirtualAddress;
        const size_t sectionSize = std::max<size_t>(section->Misc.VirtualSize, section->SizeOfRawData);
        if (parserRva >= sectionStart && parserRva < sectionStart + sectionSize) {
            executable = (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            break;
        }
    }
    if (!executable) throw std::runtime_error("Spotify Ogg parser signature is outside executable code");
    return const_cast<uint8_t*>(parser);
}

int32_t PageSeekHook(void* sync, NativeOggPage* page)
{
    const int32_t result = OriginalPageSeek(sync, page);
    if (!Running || result <= 0 || !sync || !page || !page->Header || !page->Body ||
        page->HeaderLength < 27 || page->HeaderLength > 282 ||
        page->BodyLength < 0 || page->BodyLength > 65025 ||
        page->HeaderLength + page->BodyLength != result || result > MaximumPageBytes) {
        return result;
    }

    if (!TryAcquireSRWLockExclusive(&QueueLock)) {
        ++DroppedPages;
        return result;
    }
    if (QueueCount == QueueCapacity) {
        ++DroppedPages;
        ReleaseSRWLockExclusive(&QueueLock);
        return result;
    }

    auto& slot = Queue[QueueTail];
    slot.Context = reinterpret_cast<uintptr_t>(sync);
    slot.HeaderLength = uint16_t(page->HeaderLength);
    slot.BodyLength = uint32_t(page->BodyLength);
    std::memcpy(slot.Bytes.data(), page->Header, page->HeaderLength);
    std::memcpy(slot.Bytes.data() + page->HeaderLength, page->Body, page->BodyLength);
    QueueTail = (QueueTail + 1) % QueueCapacity;
    ++QueueCount;
    ReleaseSRWLockExclusive(&QueueLock);
    SetEvent(QueueEvent);
    return result;
}

DWORD WINAPI PageWorker(LPVOID)
{
    PageSlot local;
    while (Running) {
        WaitForSingleObject(QueueEvent, 250);
        for (;;) {
            AcquireSRWLockExclusive(&QueueLock);
            if (!QueueCount) {
                ReleaseSRWLockExclusive(&QueueLock);
                break;
            }
            local = Queue[QueueHead];
            QueueHead = (QueueHead + 1) % QueueCapacity;
            --QueueCount;
            ReleaseSRWLockExclusive(&QueueLock);

            if (PageSink) {
                if (!CaptureConfirmed.exchange(true)) LogDebug("Spotify Ogg page capture is active");
                PageSink({ local.Context, local.Bytes.data(), local.HeaderLength,
                    local.Bytes.data() + local.HeaderLength, local.BodyLength });
            }
        }
    }
    return 0;
}

}

void InstallSpotifyOggHook(HMODULE spotifyModule, std::function<void(const OggPageView&)> pageSink)
{
    if (Running) return;
    uint8_t* target = ValidateSpotifyModule(spotifyModule);

    Queue = std::make_unique<PageSlot[]>(QueueCapacity);
    QueueHead = QueueTail = QueueCount = 0;
    DroppedPages = 0;
    CaptureConfirmed = false;
    PageSink = std::move(pageSink);
    QueueEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!QueueEvent) throw std::runtime_error("Failed to create the Ogg page queue event");

    Running = true;
    WorkerThread = CreateThread(nullptr, 0, PageWorker, nullptr, 0, nullptr);
    if (!WorkerThread) {
        Running = false;
        CloseHandle(QueueEvent);
        QueueEvent = nullptr;
        Queue.reset();
        throw std::runtime_error("Failed to create the Ogg page worker");
    }

    try {
        Hooks::Create(target, reinterpret_cast<void*>(&PageSeekHook),
            reinterpret_cast<void**>(&OriginalPageSeek), "SpotifyOggPageSeek");
    } catch (...) {
        StopSpotifyOggHook();
        throw;
    }
    LogInfo("Spotify Ogg parser hook prepared at RVA 0x{:X}", target - reinterpret_cast<uint8_t*>(spotifyModule));
}

void StopSpotifyOggHook()
{
    if (!Running.exchange(false)) return;
    if (QueueEvent) SetEvent(QueueEvent);
    if (WorkerThread) {
        WaitForSingleObject(WorkerThread, 5000);
        CloseHandle(WorkerThread);
        WorkerThread = nullptr;
    }
    if (QueueEvent) {
        CloseHandle(QueueEvent);
        QueueEvent = nullptr;
    }
    if (DroppedPages) LogWarn("Dropped {} Ogg pages because the capture queue was busy", DroppedPages.load());
    PageSink = {};
    Queue.reset();
}
