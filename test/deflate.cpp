#include "gtest/gtest.h"
#include "mini-deflate.h"

namespace
{
    // Combines all decompressed data into output
    template<typename Data> mini_deflate::Result DecompressInto(const Data& data, std::vector<uint8_t>& output)
    {
        mini_deflate::BitStreamer bs{data};
        return mini_deflate::Decompress(bs, [&](const auto& v) {
            std::copy(v.begin(), v.end(), std::back_inserter(output));
        });
    }

    // Decompresses data and compares with expected
    template<typename Data, typename Expected> void VerifyDecompress(const Data& data, const Expected& expected)
    {
        std::vector<uint8_t> output;
        auto result = DecompressInto(data, output);
        ASSERT_EQ(mini_deflate::Result::OK, result);

        EXPECT_EQ(expected.size(), output.size());
        EXPECT_EQ(expected, output);
    }

    // Invokes 'extract' to extract values and compares them with expected
    template<typename Data, typename Expected, typename ExtractBit> void VerifyExtractedBits(const Data& data, const Expected& expected, ExtractBit extract)
    {
        mini_deflate::BitStreamer bs{data};
        auto cur_expected = expected.begin();
        while(!bs.eof() && cur_expected != expected.end()) {
            const auto bit = extract(bs);
            ASSERT_TRUE(bit.has_value());
            EXPECT_EQ(*cur_expected, *bit);
            cur_expected++;
        }
        EXPECT_TRUE(bs.eof());
        EXPECT_EQ(expected.end(), cur_expected);
    }
}

TEST(BitStreamer, Eof)
{
    std::array<uint8_t, 0> empty;
    mini_deflate::BitStreamer bs{empty};
    EXPECT_TRUE(bs.eof());
    const auto bit = bs.GetBit();
    EXPECT_FALSE(bit.has_value());
    EXPECT_TRUE(bs.eof());
}

TEST(BitStreamer, GetBit)
{
    constexpr std::array<uint8_t, 3> data{ 0x12, 0x34, 0x5a };
    constexpr std::array<int, 24> expected{
        0, 1, 0, 0, 1, 0, 0, 0,
        0, 0, 1, 0, 1, 1, 0, 0,
        0, 1, 0, 1, 1, 0, 1, 0
    };
    VerifyExtractedBits(data, expected, [](auto& bs) { return bs.GetBit(); });
}

TEST(BitStreamer, GetBits)
{
    {
        // Example from RFC 1951, paragraph 3.1
        constexpr std::array b{8, 2};
        mini_deflate::BitStreamer bs{b};
        EXPECT_EQ(520, bs.GetDataBits(16));
    }

    {
        // Data bits are counted from LSB->MSB; whereras Huffman bits are
        // counted from MSB->LSB (RFC 1951, paragraph 3.1.1). We have:
        //
        //                        Data bits           Huffman bits
        // 0x8d = 1000 1101
        //        bbaa aaaa   --> a = 001101 = 0x0d   a = 101100 = 0x2c
        // 0x93 = 1001 0011       b = 001110 = 0x0e   b = 011100 = 0x1c
        //        cccc bbbb       c = 011001 = 0x19   c = 100110 = 0x26
        // 0xf1 = 1111 0001       d = 111100 = 0x3c   d = 001111 = 0x0f
        // 0x00 = dddd ddcc
        constexpr std::array<uint8_t, 3> data{ 0x8d, 0x93, 0xf1 };
        constexpr std::array<int, 4> expected_databits{ 0x0d, 0x0e, 0x19, 0x3c };
        constexpr std::array<int, 4> expected_huffman_bits{ 0x2c, 0x1c, 0x26, 0x0f };
        VerifyExtractedBits(data, expected_databits, [](auto& bs) { return bs.GetDataBits(6); });
        VerifyExtractedBits(data, expected_huffman_bits, [](auto& bs) { return bs.GetHuffmanBits(6); });
    }
}

TEST(Deflate, EmptyBuffer)
{
    {
        std::vector<uint8_t> output;
        std::array<uint8_t, 0> emptyArray;
        EXPECT_EQ(mini_deflate::Result::EndOfStream, DecompressInto(emptyArray, output));
        EXPECT_TRUE(output.empty());
    }
    {
        std::vector<uint8_t> output;
        std::vector<uint8_t> emptyVector;
        EXPECT_EQ(mini_deflate::Result::EndOfStream, DecompressInto(emptyVector, output));
        EXPECT_TRUE(output.empty());
    }
}

TEST(Deflate, Content_Test)
{
    // Fixed huffman tree, no repeats
    constexpr std::array<uint8_t, 10> data{ 0x2b,0x49,0x2d,0x2e,0x51,0x28,0x81,0x11,0x8a,0x00};
    const std::vector<uint8_t> expected_output{ 't', 'e', 's', 't', ' ', 't', 'e', 's', 't', ' ', 't', 'e', 's', 't', '!' };
    VerifyDecompress(data, expected_output);
}

TEST(Deflate, Content_HelloWorld)
{
    // Fixed huffman tree, no repeats
    constexpr std::array<uint8_t, 13> data{ 0xcb,0x48,0xcd,0xc9,0xc9,0x57,0x28,0xcf,0x2f,0xca,0x49,0x01,0x00 };
    const std::vector<uint8_t> expected_output{ 'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd' };
    VerifyDecompress(data, expected_output);
}

TEST(Deflate, Content_TestTestTest)
{
    // Fixed huffman tree, repeats
    constexpr std::array<uint8_t, 6> data{ 0x2b, 0x49, 0x2d, 0x2e, 0x01, 0x00 };
    const std::vector<uint8_t> expected_output{ 't', 'e', 's', 't' };
    VerifyDecompress(data, expected_output);
}

TEST(Deflate, Content_256)
{
    // Literal data copy
    constexpr std::array<uint8_t, 261> data{
        0x01, 0x00, 0x01, 0xff, 0xfe, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,
        0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
        0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
        0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
        0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a,
        0x5b, 0x5c, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
        0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
        0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a,
        0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
        0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa,
        0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
        0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
        0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
        0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
        0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
        0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };

    const auto expected_output = []() {
        std::vector<uint8_t> result;
        for (int n = 0; n < 256; n++) result.push_back(static_cast<uint8_t>(n));
        return result;
    }();

    VerifyDecompress(data, expected_output);
}

TEST(Deflate, Content_RFC1951_1000)
{
    // Dynamic Huffman tree (yields first 1000 chars of RFC1951 text
    constexpr std::array<uint8_t, 509> data{
        0x8d, 0x93, 0x4f, 0x6b, 0xe3, 0x40, 0x0c, 0xc5, 0xef, 0xfe, 0x14, 0x3a, 0xee, 0x42, 0x08, 0x84,
        0xb2, 0x85, 0xe6, 0x56, 0x92, 0xb4, 0x14, 0xda, 0x12, 0x9a, 0xc2, 0x9e, 0xa7, 0x1e, 0xc5, 0x16,
        0x1d, 0x8f, 0xdc, 0x19, 0x4d, 0x8a, 0xbf, 0xfd, 0x4a, 0x93, 0x3f, 0xdd, 0xbd, 0x2c, 0xf5, 0x21,
        0x66, 0x6c, 0xeb, 0xbd, 0x9f, 0xf4, 0x94, 0xa6, 0xb1, 0xeb, 0x19, 0xe5, 0x93, 0xd3, 0x3b, 0xfc,
        0xd6, 0x1f, 0x8a, 0x1d, 0xdc, 0x27, 0x2e, 0x23, 0x7c, 0xf7, 0xda, 0xce, 0x61, 0x8d, 0x45, 0x72,
        0xdb, 0x37, 0x2f, 0xf8, 0x51, 0x30, 0x0b, 0xec, 0x39, 0xc1, 0x8a, 0x87, 0x01, 0xa3, 0xe4, 0x25,
        0x2c, 0x6e, 0x7e, 0x2d, 0xfe, 0x53, 0x7f, 0x1b, 0x9c, 0xf7, 0x14, 0x61, 0x13, 0x05, 0xd3, 0x98,
        0x28, 0x63, 0x6e, 0x56, 0x4e, 0xb0, 0xe3, 0x34, 0x2d, 0xe1, 0x21, 0xaa, 0xd8, 0xe0, 0x84, 0x38,
        0xba, 0xf0, 0x6d, 0xa6, 0x27, 0x37, 0xa9, 0xed, 0xcd, 0xb5, 0x36, 0x77, 0x7e, 0xb4, 0xde, 0xdc,
        0x3d, 0xde, 0xbe, 0x6e, 0x8c, 0x6b, 0x4c, 0x98, 0x33, 0x7a, 0x58, 0x3b, 0x71, 0x70, 0x57, 0xe5,
        0x61, 0x37, 0x62, 0x4b, 0x7b, 0x6a, 0xab, 0x13, 0x1c, 0x30, 0x65, 0xbb, 0x2f, 0xe6, 0x57, 0x4d,
        0xb3, 0x13, 0x27, 0x25, 0x03, 0xef, 0xe1, 0xb5, 0xa7, 0x0c, 0x4f, 0x38, 0x70, 0x95, 0xad, 0xa7,
        0x41, 0x4f, 0x30, 0x26, 0x3e, 0x90, 0xc7, 0x0c, 0xf4, 0x45, 0x5b, 0x87, 0x20, 0x3d, 0x6a, 0x07,
        0xda, 0x57, 0x44, 0x81, 0x56, 0x27, 0x52, 0x22, 0xc9, 0x34, 0xff, 0xab, 0xd6, 0x84, 0x3c, 0x6b,
        0x69, 0x64, 0x81, 0x5c, 0x21, 0x26, 0x70, 0xf1, 0xab, 0x2a, 0x8b, 0x8b, 0xde, 0x25, 0x6f, 0xfe,
        0x2e, 0x4e, 0xa0, 0x09, 0x79, 0x15, 0x58, 0x53, 0x96, 0x44, 0x6f, 0xa5, 0x5a, 0xf1, 0xde, 0x64,
        0xe4, 0xc2, 0xa3, 0xf7, 0x12, 0x03, 0x0d, 0x24, 0xe8, 0xe7, 0x4d, 0xf3, 0xb0, 0xd9, 0xdd, 0xc3,
        0x33, 0x0b, 0x2e, 0x4f, 0xdc, 0x0a, 0x65, 0x8f, 0xc4, 0xbd, 0x57, 0x63, 0x18, 0x39, 0xd3, 0x51,
        0x28, 0x56, 0xe4, 0x83, 0x0b, 0xe4, 0x15, 0xf4, 0xec, 0x69, 0x30, 0x21, 0x60, 0x2b, 0xc5, 0x05,
        0x53, 0xd8, 0x26, 0x1e, 0x31, 0xe9, 0xfb, 0x17, 0xea, 0x7a, 0xc9, 0xc6, 0x28, 0x58, 0xd3, 0xd6,
        0x26, 0xa3, 0x38, 0x8a, 0x3a, 0x5d, 0x8a, 0x47, 0x22, 0xcf, 0x6d, 0xb1, 0x77, 0x0a, 0xa2, 0x0c,
        0xd4, 0x6a, 0xbc, 0xa6, 0xb1, 0xe2, 0x71, 0x4a, 0x56, 0x0e, 0x3f, 0xda, 0x9f, 0x35, 0x2c, 0x78,
        0x9c, 0xc3, 0x16, 0xb5, 0xed, 0xcb, 0x3e, 0x55, 0x2f, 0x4c, 0x03, 0xe5, 0x9a, 0x86, 0x8a, 0x75,
        0xc9, 0x29, 0x8b, 0x07, 0x61, 0x75, 0x1a, 0x6d, 0x52, 0x1e, 0xfc, 0x79, 0x14, 0xf8, 0xaf, 0x61,
        0x4d, 0x40, 0xf1, 0x4d, 0x65, 0x2c, 0x49, 0x9b, 0xc4, 0xfa, 0xfd, 0x27, 0x49, 0xcf, 0x45, 0xf3,
        0xe8, 0x5d, 0xea, 0x70, 0xa6, 0xa0, 0x6d, 0x28, 0xde, 0x16, 0x5f, 0x54, 0x3d, 0x87, 0x1a, 0x9f,
        0x65, 0xa9, 0x26, 0xac, 0xe3, 0x48, 0x26, 0x10, 0x5c, 0xec, 0x8a, 0xeb, 0x74, 0x5e, 0x26, 0xa1,
        0x25, 0xac, 0x82, 0xe9, 0x98, 0x74, 0xfd, 0x52, 0xd3, 0x1d, 0xe9, 0x54, 0x3b, 0x3b, 0x6f, 0x84,
        0x82, 0xf6, 0xba, 0x5c, 0x2a, 0x62, 0x1a, 0xed, 0xa5, 0xe5, 0x58, 0x07, 0x51, 0xa5, 0x2a, 0xf2,
        0xf9, 0x9c, 0x10, 0x6c, 0x39, 0x31, 0x1d, 0xd0, 0xcf, 0x4e, 0xaf, 0xb5, 0xfe, 0xd4, 0x44, 0x2e,
        0x6f, 0xb6, 0x0d, 0x42, 0x07, 0x34, 0xf8, 0x68, 0x34, 0xda, 0xa2, 0xc7, 0x80, 0x47, 0xe4, 0x7d,
        0xe2, 0xa1, 0x06, 0xc8, 0x6a, 0x43, 0xf6, 0x87, 0x31, 0xc5, 0x36, 0xe0, 0x1f
    };

    const auto expected_output = []() {
        const char s[] = R"TEXT(





Network Working Group                                         P. Deutsch
Request for Comments: 1951                           Aladdin Enterprises
Category: Informational                                         May 1996


        DEFLATE Compressed Data Format Specification version 1.3

Status of This Memo

   This memo provides information for the Internet community.  This memo
   does not specify an Internet standard of any kind.  Distribution of
   this memo is unlimited.

IESG Note:

   The IESG takes no position on the validity of any Intellectual
   Property Rights statements contained in this document.

Notices

   Copyright (c) 1996 L. Peter Deutsch

   Permission is granted to copy and distribute this document for any
   purpose and without charge, including translations into other
   languages and incorporation into compilations, provided that the
   copyright notice and this notice are preserved, and that any
   substantive changes or deletions from the original are cle)TEXT";
        std::vector<uint8_t> output;
        std::copy(std::begin(s), std::end(s) - 1 /* skip \0 */, std::back_inserter(output));
        return output;
    }();
    VerifyDecompress(data, expected_output);
}
