#include "test_main.h"
#include "utilities/base64.h"

#include <string_view>

using namespace mdu;

namespace {

std::string encode(std::string_view text)
{
    return base64Encode({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

}  // namespace

TEST_CASE("base64: RFC 4648 test vectors")
{
    CHECK_EQ(encode(""), std::string(""));
    CHECK_EQ(encode("f"), std::string("Zg=="));
    CHECK_EQ(encode("fo"), std::string("Zm8="));
    CHECK_EQ(encode("foo"), std::string("Zm9v"));
    CHECK_EQ(encode("foob"), std::string("Zm9vYg=="));
    CHECK_EQ(encode("fooba"), std::string("Zm9vYmE="));
    CHECK_EQ(encode("foobar"), std::string("Zm9vYmFy"));
}
