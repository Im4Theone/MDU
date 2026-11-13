#pragma once

#include <filesystem>
#include <string_view>

// Minimal file logger. It is intentionally process-wide: logging is called from
// every module and threading a logger object through all of them would add
// noise without any benefit. The log stays small and local; nothing is sent
// anywhere.
namespace mdu::log {

// Opens the log file. Before that call, messages go to stderr / the debugger.
// The previous log is kept as <name>.old so the cause of a crash survives the
// next start.
void open(const std::filesystem::path& file);
void close();

void info(std::string_view message);
void warning(std::string_view message);
void error(std::string_view message);

}  // namespace mdu::log
