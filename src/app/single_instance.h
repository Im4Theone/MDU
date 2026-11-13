#pragma once

#include <windows.h>

namespace mdu {

// Holds a per-session named mutex while MDU runs.
class SingleInstance {
public:
    explicit SingleInstance(const wchar_t* name);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool isFirstInstance() const { return first_; }

    // Lets a replacement instance start before this process has fully exited.
    void release();

private:
    HANDLE mutex_ = nullptr;
    bool first_ = false;
};

}  // namespace mdu
