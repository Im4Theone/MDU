#include "app/app.h"
#include "app/commands.h"
#include "app/single_instance.h"
#include "settings/paths.h"
#include "utilities/log.h"

#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <string_view>

namespace {

// Installer commands; see app/commands.h.
struct CommandRequest {
    bool exitRunning = false;
    bool uninstallCleanup = false;
    int setStartup = -1;  // -1 = not requested, 0 = off, 1 = on
    bool any() const { return exitRunning || uninstallCleanup || setStartup >= 0; }
};

CommandRequest parseCommands()
{
    CommandRequest request;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; arguments && i < count; ++i) {
        const std::wstring_view argument = arguments[i];
        if (argument == L"--exit") {
            request.exitRunning = true;
        } else if (argument == L"--uninstall-cleanup") {
            request.uninstallCleanup = true;
        } else if (argument == L"--set-startup" && i + 1 < count) {
            const std::wstring_view value = arguments[++i];
            request.setStartup = value == L"on" ? 1 : value == L"off" ? 0 : -1;
        }
    }
    LocalFree(arguments);
    return request;
}

mdu::LaunchOptions parseCommandLine()
{
    mdu::LaunchOptions options;
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; arguments && i < count; ++i) {
        const std::wstring_view argument = arguments[i];
        const bool hasValue = i + 1 < count;
        if (argument == L"--safe-mode") {
            options.safeMode = true;
        } else if (argument == L"--startup") {
            options.fromStartup = true;
        } else if (argument == L"--data-dir" && hasValue) {
            options.dataDirectory = arguments[++i];
        } else if (argument == L"--self-test" && hasValue) {
            options.selfTestReport = std::filesystem::absolute(arguments[++i]);
        }
    }
    LocalFree(arguments);

    // Holding Shift while MDU starts is an escape hatch that needs no files or
    // menus: it always starts in safe mode. (Not for automated self-tests.)
    if (options.selfTestReport.empty() && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        options.safeMode = true;
    }
    return options;
}

LONG WINAPI logUnhandledException(EXCEPTION_POINTERS* info)
{
    // Best effort only; the crash guard detects the crash on the next start anyway.
    const auto code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    mdu::log::error(std::format("fatal: unhandled exception 0x{:08X}", static_cast<unsigned>(code)));
    mdu::log::close();
    return EXCEPTION_CONTINUE_SEARCH;
}

class ComInitialization {
public:
    ComInitialization() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComInitialization()
    {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    ComInitialization(const ComInitialization&) = delete;
    ComInitialization& operator=(const ComInitialization&) = delete;
    bool succeeded() const { return SUCCEEDED(result_); }

private:
    HRESULT result_;
};

void relaunch()
{
    const auto exe = mdu::executablePath();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = L"\"" + exe.wstring() + L"\"";
    if (CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                       &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    SetUnhandledExceptionFilter(&logUnhandledException);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    const auto options = parseCommandLine();

    if (const auto command = parseCommands(); command.any()) {
        const ComInitialization com;
        const auto paths = mdu::resolveAppPaths(options.dataDirectory);
        if (!paths) {
            return 1;
        }
        if (command.uninstallCleanup) {
            return mdu::commands::uninstallCleanup(*paths);
        }
        if (command.setStartup >= 0) {
            return mdu::commands::setStartup(*paths, command.setStartup == 1);
        }
        return mdu::commands::stopRunningInstance(std::chrono::seconds(15)) ? 0 : 1;
    }

    // A self-test runs isolated (own data folder) and may run next to a normal instance.
    mdu::SingleInstance instanceGuard(options.selfTestReport.empty()
                                          ? L"Local\\MDU.Instance.3f2b8c1e-5d47-4a8e-9b61-0c7e2f9a4d18"
                                          : L"Local\\MDU.SelfTest.3f2b8c1e-5d47-4a8e-9b61-0c7e2f9a4d18");
    if (!instanceGuard.isFirstInstance()) {
        if (const HWND existing = FindWindowW(mdu::App::messageWindowClass, nullptr)) {
            PostMessageW(existing, mdu::App::secondInstanceMessage, 0, 0);
        }
        return 0;
    }

    const ComInitialization com;
    if (!com.succeeded()) {
        return 1;
    }

    const auto paths = mdu::resolveAppPaths(options.dataDirectory);
    if (!paths) {
        if (options.selfTestReport.empty()) {
            MessageBoxW(nullptr, L"MDU cannot access its data folder in %LOCALAPPDATA%.", L"MDU", MB_ICONERROR);
        }
        return 1;
    }

    bool restart = false;
    int exitCode = 0;
    {
        mdu::App app(instance, *paths, options);
        exitCode = app.run();
        restart = app.restartRequested();
    }

    if (restart) {
        instanceGuard.release();
        relaunch();
    }
    return exitCode;
}
