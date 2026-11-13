#include "windows/startup.h"

#include <windows.h>

#include <string>

namespace mdu::startup {

namespace {

constexpr wchar_t runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t valueName[] = L"MDU";

std::wstring commandFor(const std::filesystem::path& executable)
{
    return L"\"" + executable.wstring() + L"\" --startup";
}

std::wstring readValue()
{
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, runKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, runKey, valueName, RRF_RT_REG_SZ, nullptr, value.data(), &size) !=
        ERROR_SUCCESS) {
        return {};
    }
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

}  // namespace

bool isRegistered()
{
    return !readValue().empty();
}

bool registerExecutable(const std::filesystem::path& executable)
{
    const auto command = commandFor(executable);
    if (readValue() == command) {
        return true;
    }
    const auto bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, runKey, valueName, REG_SZ, command.c_str(), bytes) == ERROR_SUCCESS;
}

bool unregister()
{
    const LSTATUS status = RegDeleteKeyValueW(HKEY_CURRENT_USER, runKey, valueName);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

}  // namespace mdu::startup
