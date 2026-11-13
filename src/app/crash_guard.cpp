#include "app/crash_guard.h"

#include "utilities/file_io.h"
#include "utilities/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace mdu {

CrashGuard::CrashGuard(std::filesystem::path stateFile, Policy policy)
    : stateFile_(std::move(stateFile)), policy_(policy)
{
}

CrashGuard::StartupResult CrashGuard::onStartup(std::chrono::system_clock::time_point now)
{
    bool wasRunning = false;
    crashTimes_.clear();

    if (const auto text = readTextFile(stateFile_, 64 * 1024)) {
        const auto state = nlohmann::json::parse(*text, nullptr, false);
        if (state.is_object()) {
            if (const auto it = state.find("running"); it != state.end() && it->is_boolean()) {
                wasRunning = it->get<bool>();
            }
            if (const auto it = state.find("crashes"); it != state.end() && it->is_array()) {
                for (const auto& value : *it) {
                    if (value.is_number_integer()) {
                        crashTimes_.push_back(value.get<std::int64_t>());
                    }
                }
            }
        }
    }

    const auto nowSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    if (wasRunning) {
        crashTimes_.push_back(nowSeconds);
    }

    const auto oldest = nowSeconds - policy_.window.count();
    std::erase_if(crashTimes_, [&](std::int64_t t) { return t < oldest || t > nowSeconds; });

    StartupResult result;
    result.previousRunCrashed = wasRunning;
    result.recentCrashes = static_cast<int>(crashTimes_.size());
    result.safeModeRecommended = result.recentCrashes >= policy_.maxCrashes;

    if (!save(true)) {
        log::warning("crash guard: cannot write state file");
    }
    return result;
}

void CrashGuard::onCleanExit()
{
    save(false);
}

void CrashGuard::reset()
{
    crashTimes_.clear();
    save(true);
}

bool CrashGuard::save(bool running) const
{
    const nlohmann::json state = {{"running", running}, {"crashes", crashTimes_}};
    std::string error;
    return writeFileAtomically(stateFile_, state.dump(), error);
}

}  // namespace mdu
