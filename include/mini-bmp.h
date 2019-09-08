#pragma once

#include <cstdint>
#include <vector>

namespace mini_bmp
{

namespace detail
{
    template<typename Streamer>
    void Put8(Streamer& s, std::uint8_t v)
    {
        s.Put(&v, 1);
    }

    template<typename Streamer>
    void Put16(Streamer& s, std::uint16_t v)
    {
        Put8(s, v & 0xff);
        Put8(s, (v >> 8) & 0xff);
    }

    template<typename Streamer>
    void Put24(Streamer& s, std::uint32_t v)
    {
        Put8(s, v & 0xff);
        Put8(s, (v >> 8) & 0xff);
        Put8(s, (v >> 16) & 0xff);
    }

    template<typename Streamer>
    void Put32(Streamer& s, std::uint32_t v)
    {
        Put16(s, v & 0xffff);
        Put16(s, (v >> 16) & 0xffff);
    }

} // namespace detail

struct MemoryStreamer
{
    void Put(const void* v, std::size_t n)
    {
        for (auto p = reinterpret_cast<const uint8_t*>(v); n > 0; n--, p++)
            buffer.push_back(*p);
    }

    const auto data() const { return buffer.data(); }
    const auto size() const { return buffer.size(); }

    std::vector<uint8_t> buffer;
};

enum class Result
{
    OK,
    InvalidBitsPerPixel
};

// All structures are from MSDN, https://docs.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfo
template<typename ByteStreamer>
Result Write(ByteStreamer& bs, const void* data, int height, int width, int bpp)
{
    using namespace detail;
    if (bpp != 24 && bpp != 32) return Result::InvalidBitsPerPixel;
    const auto bytesPP = bpp / 8;

    // BITMAPFILEHEADER
    Put8(bs, 0x42); Put8(bs, 0x4d); // 'BM' identifier
    {
        uint32_t file_size = 14 + 40;       // headers
        uint32_t row_length = width * bytesPP;
        while ((row_length % 4) != 0) row_length++;
        file_size += height * row_length;
        Put32(bs, file_size);       // file size in bytes
    }
    Put32(bs, 0);                   // reserved
    Put32(bs, 14 + 40);             // offset of bitmap data
    // BITMAPINFOHEADER - we'll always write a 32-bit bitmap
    Put32(bs, 40);                  // sizeof(BITMAPINFOHEADER)
    Put32(bs, width);               // width, in pixels
    Put32(bs, height);              // height, in pixes
    Put16(bs, 1);                   // planes
    Put16(bs, bpp);                 // per-pixel bit count
    Put32(bs, 0);                   // compression (BI_RGB)
    Put32(bs, 0);                   // image size, in bytes (0 for BI_RGB)
    Put32(bs, 0);                   // horizonal resolution (pixels-per-meter)
    Put32(bs, 0);                   // vertical resolution (pixels-per-meter)
    Put32(bs, 0);                   // number of colours used (0 = all)
    Put32(bs, 0);                   // number of colour indices used (0 = all)

    // Bitmap data - note that we have to invert it horizontally
    auto ptr = reinterpret_cast<const uint8_t*>(data) + (height - 1) * width * bytesPP;
    for(int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Convert colour: (A)RGB -> (A)BGR
            uint32_t v = 0;
            v |= *ptr++ << 16;
            v |= *ptr++ << 8;
            v |= *ptr++;
            if (bpp == 32) {
                v |= *ptr++ << 24;
                Put32(bs, v);
            } else {
                Put24(bs, v);
            }
        }
        // Pad to 4-byte boundary
        for (int written = width * bytesPP; (written % 4) != 0; written++)
            Put8(bs, 0);

        // Advance to the previous line: the 2 is because we processed the
        // previous line and want the line before that
        ptr -= 2 * width * bytesPP;
    }
    return Result::OK;
}

} // namespace mini_bmp
