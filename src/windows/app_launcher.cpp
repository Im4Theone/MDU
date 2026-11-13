#include "windows/app_launcher.h"

#include "utilities/strings.h"

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <system_error>

namespace mdu {

std::wstring expandEnvironment(const std::wstring& text)
{
    const DWORD needed = ExpandEnvironmentStringsW(text.c_str(), nullptr, 0);
    if (needed == 0) {
        return text;
    }
    std::wstring result(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(text.c_str(), result.data(), needed);
    if (written == 0 || written > needed) {
        return text;
    }
    result.resize(written - 1);  // drop the terminator
    return result;
}

bool launchItem(const PinnedItem& item, std::wstring& error)
{
    const auto target = expandEnvironment(toWide(item.target));
    const auto arguments = expandEnvironment(toWide(item.arguments));

    // Start file targets in their own folder, like Explorer does.
    std::wstring directory;
    const std::filesystem::path targetPath(target);
    if (targetPath.is_absolute() && targetPath.has_parent_path()) {
        directory = targetPath.parent_path().wstring();
    }

    // The click went to the dock, so MDU currently holds the foreground right.
    // Pass it on so the launched window comes to the front instead of flashing
    // in the taskbar.
    AllowSetForegroundWindow(ASFW_ANY);

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.lpFile = target.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info)) {
        const DWORD code = GetLastError();
        error = toWide(std::system_category().message(static_cast<int>(code)));
        return false;
    }
    return true;
}

}  // namespace mdu
