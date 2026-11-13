#include "utilities/base64.h"

namespace mdu {

std::string base64Encode(std::span<const std::uint8_t> data)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);

    std::size_t i = 0;
    for (; i + 3 <= data.size(); i += 3) {
        const std::uint32_t triple = (std::uint32_t{data[i]} << 16) | (std::uint32_t{data[i + 1]} << 8) | data[i + 2];
        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        out += alphabet[(triple >> 6) & 0x3F];
        out += alphabet[triple & 0x3F];
    }

    const std::size_t remaining = data.size() - i;
    if (remaining > 0) {
        std::uint32_t triple = std::uint32_t{data[i]} << 16;
        if (remaining == 2) {
            triple |= std::uint32_t{data[i + 1]} << 8;
        }
        out += alphabet[(triple >> 18) & 0x3F];
        out += alphabet[(triple >> 12) & 0x3F];
        out += remaining == 2 ? alphabet[(triple >> 6) & 0x3F] : '=';
        out += '=';
    }
    return out;
}

}  // namespace mdu
