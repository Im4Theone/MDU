#pragma once

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

namespace mdu::test {

inline std::filesystem::path makeTemporaryDirectory(const std::string& label)
{
    std::random_device random;
    const auto name = "mdu-test-" + label + "-" + std::to_string(random()) + "-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(path);
    return path;
}

}  // namespace mdu::test
