#pragma once

#include "settings/config.h"

#include <string>

namespace mdu {

// Starts a pinned item through the shell, exactly as double-clicking it in
// Explorer would. Only items from the native configuration can be launched;
// the frontend merely names them by id.
bool launchItem(const PinnedItem& item, std::wstring& error);

// Expands %VARIABLES% in a configured string.
std::wstring expandEnvironment(const std::wstring& text);

}  // namespace mdu
