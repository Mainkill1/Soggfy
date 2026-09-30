#include "pch.h"
#include "CefUtils.h"
#include "CefAbi.h"
#include "Utils/Log.h"

#include <atomic>
#include <mutex>
#include <new>

#ifndef NDEBUG
#define NDEBUG
#endif
#include <include/capi/cef_browser_capi.h>
#include <include/capi/cef_task_capi.h>

namespace {

std::wstring PendingScript;
std::mutex ScriptMutex;
std::atomic<bool> TaskPending = false;
int (*PostTask)(cef_thread_id_t, cef_task_t*) = nullptr;
cef_browser_t* (*GetBrowserByIdentifier)(int) = nullptr;

struct ExecuteScriptTask
{
    cef_task_t Api{};
    std::atomic<unsigned> References{ 1 };
};

void CEF_CALLBACK TaskAddRef(cef_base_ref_counted_t* base)
{
    ++reinterpret_cast<ExecuteScriptTask*>(base)->References;
}

int CEF_CALLBACK TaskHasOneRef(cef_base_ref_counted_t* base)
{
    return reinterpret_cast<ExecuteScriptTask*>(base)->References == 1;
}

int CEF_CALLBACK TaskHasAtLeastOneRef(cef_base_ref_counted_t* base)
{
    return reinterpret_cast<ExecuteScriptTask*>(base)->References > 0;
}

int CEF_CALLBACK TaskRelease(cef_base_ref_counted_t* base)
{
    auto task = reinterpret_cast<ExecuteScriptTask*>(base);
    if (--task->References) return 0;
    delete task;
    return 1;
}

void Release(cef_base_ref_counted_t* object)
{
    if (object && object->release) object->release(object);
}

void CEF_CALLBACK ExecuteScript(cef_task_t*)
{
    std::wstring script;
    { std::lock_guard lock(ScriptMutex); script = PendingScript; }

    bool executed = false;
    for (int identifier = 1; identifier <= 64; ++identifier) {
        cef_browser_t* browser = GetBrowserByIdentifier(identifier);
        if (!browser) continue;

        cef_frame_t* frame = browser->get_main_frame ? browser->get_main_frame(browser) : nullptr;
        if (frame && frame->is_main && frame->is_main(frame) && frame->execute_java_script && !script.empty()) {
            cef_string_t code{ reinterpret_cast<char16_t*>(script.data()), script.size(), nullptr };
            wchar_t sourceText[] = L"soggfy-uic.js";
            cef_string_t source{ reinterpret_cast<char16_t*>(sourceText), std::size(sourceText) - 1, nullptr };
            frame->execute_java_script(frame, &code, &source, 1);
            executed = true;
            LogDebug("Injected client JS into CEF browser {}", identifier);
        }
        if (frame) Release(&frame->base);
        Release(&browser->base);
    }
    TaskPending = false;
}

void ScheduleScript()
{
    if (!PostTask || !GetBrowserByIdentifier || TaskPending.exchange(true)) return;
    auto task = new(std::nothrow) ExecuteScriptTask;
    if (!task) { TaskPending = false; return; }
    task->Api.base = { sizeof(cef_task_t), TaskAddRef, TaskRelease,
        TaskHasOneRef, TaskHasAtLeastOneRef };
    task->Api.execute = ExecuteScript;
    if (!PostTask(TID_UI, &task->Api)) {
        TaskPending = false;
        TaskRelease(&task->Api.base);
    }
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) return {};
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!length) return {};
    std::wstring result(size_t(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), length);
    return result;
}

}

namespace CefUtils {

void InjectJS(const std::string& code)
{
    auto wide = Utf8ToWide(code);
    if (wide.empty()) return;
    { std::lock_guard lock(ScriptMutex); PendingScript = std::move(wide); }
    ScheduleScript();
}

void InitUrlBlocker(std::function<bool(std::wstring_view)>)
{
    HMODULE cef = GetModuleHandleW(L"libcef.dll");
    if (!cef) throw std::runtime_error("libcef.dll is not loaded");

    using VersionInfo = int(*)(int);
    auto version = reinterpret_cast<VersionInfo>(GetProcAddress(cef, "cef_version_info"));
    if (!version || !CefAbi::IsSupportedVersion(version(0), version(1), version(2)))
        throw std::runtime_error("CEF version is not supported (expected 146.0.10)");

    PostTask = reinterpret_cast<decltype(PostTask)>(GetProcAddress(cef, "cef_post_task"));
    GetBrowserByIdentifier = reinterpret_cast<decltype(GetBrowserByIdentifier)>(
        GetProcAddress(cef, "cef_browser_host_get_browser_by_identifier"));
    if (!PostTask || !GetBrowserByIdentifier)
        throw std::runtime_error("CEF browser discovery APIs are unavailable");
    LogInfo("CEF 146 public browser discovery initialized");
}

}
