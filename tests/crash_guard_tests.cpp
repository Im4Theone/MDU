#include "app/crash_guard.h"
#include "test_main.h"
#include "test_paths.h"

#include <fstream>

using namespace mdu;
using namespace std::chrono_literals;

namespace {

const auto start = std::chrono::system_clock::time_point{} + std::chrono::hours(24 * 365 * 50);

}  // namespace

TEST_CASE("crash guard: clean exits never trigger safe mode")
{
    const auto file = test::makeTemporaryDirectory("guard-clean") / "state.json";
    for (int i = 0; i < 10; ++i) {
        CrashGuard guard(file, {});
        const auto result = guard.onStartup(start + i * 1s);
        CHECK(!result.previousRunCrashed);
        CHECK(!result.safeModeRecommended);
        guard.onCleanExit();
    }
}

TEST_CASE("crash guard: repeated crashes within the window trigger safe mode")
{
    const auto file = test::makeTemporaryDirectory("guard-crash") / "state.json";
    const CrashGuard::Policy policy{3, 10min};

    CrashGuard first(file, policy);
    CHECK(!first.onStartup(start).safeModeRecommended);  // first ever run

    // Each following run starts without a clean exit before it.
    CrashGuard::StartupResult result;
    for (int i = 1; i <= 3; ++i) {
        CrashGuard guard(file, policy);
        result = guard.onStartup(start + i * 30s);
        CHECK(result.previousRunCrashed);
        CHECK_EQ(result.recentCrashes, i);
        CHECK_EQ(result.safeModeRecommended, i >= 3);
    }
}

TEST_CASE("crash guard: old crashes expire")
{
    const auto file = test::makeTemporaryDirectory("guard-expire") / "state.json";
    const CrashGuard::Policy policy{2, 10min};

    CrashGuard(file, policy).onStartup(start);
    CrashGuard(file, policy).onStartup(start + 1min);  // crash #1 recorded
    const auto later = CrashGuard(file, policy).onStartup(start + 30min);  // #1 expired, #2 recorded
    CHECK_EQ(later.recentCrashes, 1);
    CHECK(!later.safeModeRecommended);
}

TEST_CASE("crash guard: reset clears history")
{
    const auto file = test::makeTemporaryDirectory("guard-reset") / "state.json";
    const CrashGuard::Policy policy{1, 10min};

    CrashGuard(file, policy).onStartup(start);
    CrashGuard guard(file, policy);
    CHECK(guard.onStartup(start + 1s).safeModeRecommended);
    guard.reset();
    guard.onCleanExit();
    CHECK(!CrashGuard(file, policy).onStartup(start + 2s).safeModeRecommended);
}

TEST_CASE("crash guard: corrupt state file is ignored")
{
    const auto file = test::makeTemporaryDirectory("guard-corrupt") / "state.json";
    std::ofstream(file) << "not json at all";
    const auto result = CrashGuard(file, {}).onStartup(start);
    CHECK(!result.previousRunCrashed);
    CHECK(!result.safeModeRecommended);
}
