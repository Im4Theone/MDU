#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mdu {

// Reads a whole file. Returns std::nullopt if it does not exist or cannot be read.
// Files larger than maxBytes are rejected to avoid loading something unexpected.
std::optional<std::string> readTextFile(const std::filesystem::path& path,
                                        std::uintmax_t maxBytes = 4 * 1024 * 1024);

// Writes to a temporary sibling file and renames it over the target.
bool writeFileAtomically(const std::filesystem::path& path, std::string_view contents,
                         std::string& error);

}  // namespace mdu
