#include "app/single_instance.h"

namespace mdu {

SingleInstance::SingleInstance(const wchar_t* name)
{
    mutex_ = CreateMutexW(nullptr, TRUE, name);
    // If the mutex could not be created at all, run anyway: refusing to start
    // would be worse than an unlikely duplicate.
    first_ = mutex_ == nullptr || GetLastError() != ERROR_ALREADY_EXISTS;
}

SingleInstance::~SingleInstance()
{
    release();
}

void SingleInstance::release()
{
    if (mutex_) {
        if (first_) {
            ReleaseMutex(mutex_);
        }
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

}  // namespace mdu
