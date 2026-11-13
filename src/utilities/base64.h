#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace mdu {

std::string base64Encode(std::span<const std::uint8_t> data);

}  // namespace mdu
