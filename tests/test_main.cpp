#include "test_main.h"

#include <cstring>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& test : mdu::test::registry()) {
        if (filter && !std::strstr(test.name, filter)) {
            continue;
        }
        const int before = mdu::test::failureCount();
        test.body();
        ++run;
        std::cout << (mdu::test::failureCount() == before ? "[pass] " : "[FAIL] ") << test.name << "\n";
    }
    std::cout << run << " tests, " << mdu::test::failureCount() << " failed checks\n";
    return mdu::test::failureCount() == 0 && run > 0 ? 0 : 1;
}
