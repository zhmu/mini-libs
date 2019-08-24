#include "gtest/gtest.h"
#include "mini-adler32.h"

namespace
{
    template<typename Container>
    void Verify(const Container& input, mini_adler32::Value expected)
    {
        mini_adler32::Adler32 adler;
        adler.Update(input.begin(), input.end());
        EXPECT_EQ(expected, *adler);
    }
}

TEST(Adler32, Empty)
{
    constexpr std::array<uint8_t, 0> data{};
    Verify(data, mini_adler32::constants::initial_Adler32);
}

TEST(Adler32, Wikipedia)
{
    // From https://en.wikipedia.org/wiki/Adler-32
    constexpr std::array<uint8_t, 9> data{
        'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'
    };
    Verify(data, 0x11e60398);
}
