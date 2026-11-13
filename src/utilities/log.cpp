#include "utilities/log.h"

#include <chrono>
#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mdu::log {

namespace {

struct LogState {
    std::mutex mutex;
    std::ofstream file;
};

LogState& state()
{
    static LogState instance;
    return instance;
}

void write(std::string_view level, std::string_view message)
{
    const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const auto line = std::format("{:%Y-%m-%d %H:%M:%S} [{}] {}\n", now, level, message);

    auto& s = state();
    const std::scoped_lock lock(s.mutex);
    if (s.file.is_open()) {
        s.file << line;
        s.file.flush();
        return;
    }
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#else
    std::fputs(line.c_str(), stderr);
#endif
}

}  // namespace

void open(const std::filesystem::path& file)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    auto previous = file;
    previous += ".old";
    if (std::filesystem::exists(file, ec)) {
        std::filesystem::rename(file, previous, ec);
    }

    auto& s = state();
    const std::scoped_lock lock(s.mutex);
    s.file.open(file, std::ios::out | std::ios::trunc);
}

void close()
{
    auto& s = state();
    const std::scoped_lock lock(s.mutex);
    s.file.close();
}

void info(std::string_view message)
{
    write("info", message);
}

void warning(std::string_view message)
{
    write("warn", message);
}

void error(std::string_view message)
{
    write("error", message);
}

}  // namespace mdu::log
