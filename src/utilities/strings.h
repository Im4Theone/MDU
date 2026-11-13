#pragma once

#include <string>
#include <string_view>

namespace mdu {

// UTF-8 <-> UTF-16 conversion for Win32 calls. Invalid sequences are replaced
// with U+FFFD rather than failing.
std::wstring toWide(std::string_view utf8);
std::string toUtf8(std::wstring_view wide);

}  // namespace mdu
