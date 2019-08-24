#include "gtest/gtest.h"
#include "mini-zlib.h"

namespace
{
    template<typename T>
    struct MemoryStreamer
    {
        MemoryStreamer(const T& data) : data(data) { }

        std::optional<std::uint8_t> GetByte()
        {
            if (pos >= data.size()) return {};
            return data[pos++];
        }

        void Skip(std::size_t n)
        {
            pos += n;
        }

        const T& data;
        std::size_t pos{};
    };

    // Combines all decompressed data into output
    template<typename Data> auto DecompressInto(const Data& data, std::vector<uint8_t>& output)
    {
        MemoryStreamer s{data};
        return mini_zlib::Decompress(s, data.size(), [&](const auto& v) {
            std::copy(v.begin(), v.end(), std::back_inserter(output));
        });
    }

    // Decompresses data and compares with expected
    template<typename Data, typename Expected> void VerifyDecompress(const Data& data, const Expected& expected)
    {
        std::vector<uint8_t> output;
        auto result = DecompressInto(data, output);
        ASSERT_EQ(mini_zlib::Result::OK, result);

        EXPECT_EQ(expected.size(), output.size());
        EXPECT_EQ(expected, output);
    }
}

TEST(zlib, EndOfStream)
{
    constexpr std::array<uint8_t, 0> data;

    std::vector<uint8_t> output;
    const auto result = DecompressInto(data, output);
    EXPECT_EQ(mini_zlib::Result::PrematureEndOfStream, result);
    EXPECT_TRUE(data.empty());
}

TEST(zlib, Content_HelloWorld)
{
    // Plain zlib-ed, no dictionary
    constexpr std::array<uint8_t, 19> data{
        0x78, 0x9c, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x28, 0xcf, 0x2f, 0xca,
        0x49, 0x01, 0x00, 0x1a, 0x0b, 0x04, 0x5d
    };
    const std::vector<uint8_t> expected_output{ 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };
    VerifyDecompress(data, expected_output);
}
