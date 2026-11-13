#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace mdu {

// Detects repeated unclean shutdowns and decides when to start in safe mode.
//
// A "running" marker is written at startup and cleared on a clean exit
// (including Windows logoff/shutdown). If the marker is still set at the next
// startup, the previous session ended unexpectedly. Too many of those within a
// time window switch MDU into safe mode, which starts nothing that could have
// caused the crash. MDU never restarts itself automatically, so a crash loop is
// bounded by the user's own launches.
class CrashGuard {
public:
    struct Policy {
        int maxCrashes = 3;
        std::chrono::seconds window{std::chrono::minutes(10)};
    };

    struct StartupResult {
        bool previousRunCrashed = false;
        int recentCrashes = 0;
        bool safeModeRecommended = false;
    };

    CrashGuard(std::filesystem::path stateFile, Policy policy);

    StartupResult onStartup(std::chrono::system_clock::time_point now);
    void onCleanExit();

    // Forgets recorded crashes, for example when the user leaves safe mode.
    void reset();

private:
    bool save(bool running) const;

    std::filesystem::path stateFile_;
    Policy policy_;
    std::vector<std::int64_t> crashTimes_;  // seconds since the Unix epoch
};

}  // namespace mdu
