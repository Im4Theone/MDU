#include "utilities/file_io.h"

#include <fstream>
#include <system_error>

namespace mdu {

std::optional<std::string> readTextFile(const std::filesystem::path& path, std::uintmax_t maxBytes)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes) {
        return std::nullopt;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::nullopt;
    }

    std::string contents(static_cast<std::size_t>(size), '\0');
    stream.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (stream.gcount() != static_cast<std::streamsize>(contents.size())) {
        return std::nullopt;
    }
    return contents;
}

bool writeFileAtomically(const std::filesystem::path& path, std::string_view contents,
                         std::string& error)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "cannot create directory: " + ec.message();
            return false;
        }
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "cannot open temporary file for writing";
            return false;
        }
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.flush();
        if (!stream) {
            error = "write failed";
            return false;
        }
    }

    // std::filesystem::rename replaces an existing target on both Windows
    // (MoveFileExW with MOVEFILE_REPLACE_EXISTING) and POSIX.
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "cannot replace file: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace mdu
