#pragma once

#include <cstdint>
#include <optional>

#include "mini-zlib.h"

namespace mini_png
{

template<typename Data>
struct ByteStreamer
{
    ByteStreamer(const Data& data): data(data)
    {
    }

    bool eof() const
    {
        return pos >= data.size();
    }

    std::optional<uint8_t> GetByte()
    {
        if (eof()) return {};
        return data[pos++];
    }

    void Skip(std::size_t length)
    {
        pos += length;
    }

    template<typename Value> std::optional<Value> Get()
    {
        // 2.1. All integers that require more than one byte are stored in
        // network order
        Value v = 0;
        for(int n = 0; n < sizeof(Value); n++)
        {
            const auto b = GetByte();
            if (!b.has_value()) return {};
            v = (v << 8) | *b;
        }
        return v;
    }

    const Data& data;
    std::size_t pos = 0;
};

namespace field
{
    // Layout and types outlined in 3.2
    using Type = std::uint32_t;
    using Length = std::uint32_t;
    using Checksum = std::uint32_t;
    // IHDR, 4.1.1
    using Width = std::uint32_t;
    using Height = std::uint32_t;
    using BitDepth = std::uint8_t;
    using ColorType = std::uint8_t;
    using CompressionMethod = std::uint8_t;
    using FilterMethod = std::uint8_t;
    using InterlaceMethod = std::uint8_t;

    namespace constants
    {
        const std::array<uint8_t, 8> png_signature{ 137, 80, 78, 71, 13, 10, 26, 10 };

        constexpr ColorType colorType_Palette = 1;
        constexpr ColorType colorType_Color = 2;
        constexpr ColorType colorType_Alpha = 4;

        constexpr CompressionMethod compressionMethod_Deflate = 0;

        constexpr FilterMethod filterMethod_Adaptive = 0;

        constexpr InterlaceMethod interlaceMethod_None = 0;
        constexpr InterlaceMethod interlaceMethod_Adam7 = 1;

        constexpr uint8_t filterType_None = 0;
        constexpr uint8_t filterType_Sub = 1;
        constexpr uint8_t filterType_Up = 2;
        constexpr uint8_t filterType_Average = 3;
        constexpr uint8_t filterType_Paeth = 4;
    }

    namespace checks
    {
        // 4.1.1 describes the acceptable values
        inline constexpr bool IsHeightValid(Height h)
        {
            return h <= (1UL << 31) - 1;
        }

        inline constexpr bool IsWidthValid(Height v)
        {
            return v <= (1UL << 31) - 1;
        }

        inline constexpr bool IsColorTypeAndBitDepthCombinationValid(ColorType ct, BitDepth bd)
        {
            switch(ct) {
                case 0: return bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16;
                case 3: return bd == 1 || bd == 2 || bd == 4 || bd == 8;
                case 2:
                case 4:
                case 6: return bd == 8 || bd == 16;
            }
            return false;
        }
    }
} // namespace field

struct ChunkType
{
    constexpr ChunkType() = default;
    constexpr ChunkType(field::Type type) : type(type) { }

    std::string AsString() const
    {
        std::string s;
        std::uint32_t shift = 8 * (sizeof(field::Type) - 1);
        for(std::size_t n = 0; n < sizeof(field::Type); n++)
        {
            s += (char)((type >> shift)) & 0xff;
            shift -= 8;
        }
        return s;
    }

    bool IsAncillary() const { return ((type >> 24) & 0x20) != 0; }
    bool IsPrivate() const { return ((type >> 16) & 0x20) != 0; }
    bool IsReserved() const { return ((type >> 8) & 0x20) != 0; }
    bool IsSafeToCopy() const { return (type & 0x20) != 0; }

    bool operator==(const ChunkType& rhs) const { return type == rhs.type; }
    bool operator!=(const ChunkType& rhs) const { return !operator==(rhs); }

    field::Type type{0};
};

namespace chunk_types
{
    inline constexpr ChunkType FromIdentifier(const std::array<char, 4>& id)
    {
        field::Type v{0};
        v = (id[0] << 24) | (id[1] << 16) | (id[2] << 8) | id[3];
        return ChunkType{ v };
    }

    constexpr auto type_IHDR = FromIdentifier({ 'I', 'H', 'D', 'R' });
    constexpr auto type_IDAT = FromIdentifier({ 'I', 'D', 'A', 'T' });
    constexpr auto type_IEND = FromIdentifier({ 'I', 'E', 'N', 'D' });
} // namespace chunk_types

template<typename ByteStreamer>
struct Chunk
{
    Chunk(ByteStreamer& bs) : bs(bs) { }

    bool ReadHeader()
    {
        const auto l = bs.template Get<field::Length>();
        const auto t = bs.template Get<field::Type>();
        if (!l.has_value() || !t.has_value()) return false;
        length = *l;
        type = ChunkType{*t};
        return true;
    }

    void Skip()
    {
        bs.Skip(length + sizeof(field::Checksum));
    }

    ByteStreamer& bs;
    field::Length length;
    ChunkType type;
};

enum class Result
{
    OK,
    PrematureEndOfFile,
    BadSignature,
    InvalidFirstChunk,
    MultipleIHDR,
    InvalidWidth,
    InvalidHeight,
    InvalidColorTypeAndBitDepthCombination,
    UnsupportedCompressionMethod,
    UnsupportedFilterMethod,
    UnsupportedInterlaceMethod,
    UnsupportedCriticalChunkEncountered,
    ZlibError,
    UnsupportedFilterType
};

struct ImageHeader
{
    field::Width width;
    field::Height height;
    field::BitDepth bitDepth;
    field::ColorType colorType;
    field::CompressionMethod compressionMethod;
    field::FilterMethod filterMethod;
    field::InterlaceMethod interlaceMethod;

    std::size_t GetBytesPerPixel() const
    {
        const int samplesPerPixel = [&]() {
            switch(colorType) {
                default:
                case 0: return 1;
                case 2: return 3;
                case 3: return 1; // TODO palette
                case 4: return 2;
                case 6: return 4;
            }
        }();
        return samplesPerPixel * (bitDepth / 8);
    }

    std::size_t GetScanLineLengthInBytes() const
    {
        return width * GetBytesPerPixel();
    }
};

uint16_t PaethPredictor(uint8_t a, uint8_t b, uint8_t c)
{
    int p = static_cast<int>(a) + static_cast<int>(b) - static_cast<int>(c);
    auto pa = std::abs(p - a);
    auto pb = std::abs(p - b);
    auto pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

struct DecodeContext
{
    DecodeContext(const ImageHeader& ihdr)
        : ihdr(ihdr)
        , bytesPerPixel(ihdr.GetBytesPerPixel())
        , scanLineLengthInBytes(ihdr.GetScanLineLengthInBytes())
    {
        for(auto& s: scanLine)
            s.resize(scanLineLengthInBytes, 0);
    }

    // Assumes dataIterator can access scanLineLengthInBytes + 1 items
    template<typename Iterator, typename ScanLineFn>
    void ProcessScanLine(Iterator dataIterator, ScanLineFn scanLineFn)
    {
        const auto filterType = *dataIterator++;
        //printf("line %2d/%2d, filter %d\n", currentLine, ihdr.height, filterType);

        auto& currentScanLine = scanLine[currentLine % scanLine.size()];
        const auto& previousScanLine = scanLine[(currentLine - 1) % scanLine.size()];
        auto raw = [&](int x) {
            if (x < 0) return static_cast<uint16_t>(0);
            return static_cast<uint16_t>(currentScanLine[x]);
        };
        auto prior = [&](int x) {
            if (x < 0) return static_cast<uint16_t>(0);
            return static_cast<uint16_t>(previousScanLine[x]);
        };

        switch(filterType) {
            case field::constants::filterType_None:
                std::copy(dataIterator, dataIterator + scanLineLengthInBytes, currentScanLine.begin());
                break;
            case field::constants::filterType_Sub:
                for(std::size_t x = 0; x < scanLineLengthInBytes; x++, dataIterator++) {
                    currentScanLine[x] = (static_cast<uint16_t>(*dataIterator) + raw(x - bytesPerPixel)) & 0xff;
                }
                break;
            case field::constants::filterType_Up:
                for(std::size_t x = 0; x < scanLineLengthInBytes; x++, dataIterator++) {
                    currentScanLine[x] = (static_cast<uint16_t>(*dataIterator) + prior(x)) & 0xff;
                }
                break;
            case field::constants::filterType_Average:
                for(std::size_t x = 0; x < scanLineLengthInBytes; x++, dataIterator++) {
                    currentScanLine[x] = (*dataIterator + (raw(x - bytesPerPixel) + prior(x)) / 2) & 0xff;
                }
                break;
            case field::constants::filterType_Paeth:
                for(std::size_t x = 0; x < scanLineLengthInBytes; x++, dataIterator++) {
                    currentScanLine[x] = (static_cast<uint16_t>(*dataIterator) + PaethPredictor(raw(x - bytesPerPixel), prior(x), prior(x - bytesPerPixel))) & 0xff;
                }
                break;
            default:
                result = Result::UnsupportedFilterType;
                return; // do not call scanLineFn()
        }

        scanLineFn(currentScanLine);
    }

    template<typename ScanLineFn>
    void ProcessImageData(const std::vector<uint8_t>& data, ScanLineFn scanLineFn)
    {
        if (result != Result::OK) return; // don't make things worse
        auto dataIterator = data.begin();

        // pendingData is always at most one scanline; expand and process as needed
        if (!pendingData.empty()) {
            const auto toCopy = std::min((scanLineLengthInBytes + 1) - pendingData.size(), data.size());
            std::copy(dataIterator, dataIterator + toCopy, std::back_inserter(pendingData));
            std::advance(dataIterator, toCopy);
        }

        // Proces the complete pending scanline, if we have one
        if (pendingData.size() == scanLineLengthInBytes + 1) {
            ProcessScanLine(pendingData.begin(), scanLineFn);
            pendingData.clear();
            currentLine++;
        }

        // Process all complete scanlines
        while (result == Result::OK && std::distance(dataIterator, data.end()) >= scanLineLengthInBytes + 1) {
            ProcessScanLine(dataIterator, scanLineFn);
            std::advance(dataIterator, scanLineLengthInBytes + 1);
            currentLine++;
        }

        // Append what's left to the pending scanline
        std::copy(dataIterator, data.end(), std::back_inserter(pendingData));
        //printf("%d left, line %2d/%2d\n", (int)std::distance(dataIterator, data.end()), currentLine, ihdr.height);
    }


    const ImageHeader& ihdr;
    Result result{ Result::OK };
    std::vector<uint8_t> pendingData;
    std::uint32_t currentLine{0};
    const std::size_t bytesPerPixel{0};
    const std::size_t scanLineLengthInBytes{0};

    std::array<std::vector<uint8_t>, 2> scanLine;
};

template<typename ByteStreamer, typename ScanLineFn>
Result ParseImageData(const Chunk<ByteStreamer>& chunk, ByteStreamer& bs, DecodeContext& dctx, ScanLineFn scanLineFn)
{
    const auto result = mini_zlib::Decompress(bs, chunk.length, [&](const auto& output) {
        dctx.ProcessImageData(output, scanLineFn);
    });
    if (result != mini_zlib::Result::OK) return Result::ZlibError;

    return dctx.result;
}

template<typename ByteStreamer>
Result ParseImageHeader(ByteStreamer& bs, ImageHeader& ihdr)
{
    auto w = bs.template Get<field::Width>();
    auto h = bs.template Get<field::Height>();
    auto bd = bs.template Get<field::BitDepth>();
    auto ct = bs.template Get<field::ColorType>();
    auto cm = bs.template Get<field::CompressionMethod>();
    auto fm = bs.template Get<field::FilterMethod>();
    auto im = bs.template Get<field::InterlaceMethod>();
    if (!w.has_value() || !h.has_value() || !bd.has_value() || !ct.has_value() || !cm.has_value() || !fm.has_value() || !im.has_value()) return Result::PrematureEndOfFile;
    ihdr.width = *w;
    ihdr.height = *h;
    ihdr.bitDepth = *bd;
    ihdr.colorType = *ct;
    ihdr.compressionMethod = *cm;
    ihdr.filterMethod = *fm;
    ihdr.interlaceMethod = *im;

    if (!field::checks::IsWidthValid(ihdr.width)) return Result::InvalidWidth;
    if (!field::checks::IsHeightValid(ihdr.height)) return Result::InvalidHeight;
    if (!field::checks::IsColorTypeAndBitDepthCombinationValid(ihdr.colorType, ihdr.bitDepth)) return Result::InvalidColorTypeAndBitDepthCombination;

    if (ihdr.compressionMethod != field::constants::compressionMethod_Deflate) return Result::UnsupportedCompressionMethod;
    if (ihdr.filterMethod != field::constants::filterMethod_Adaptive) return Result::UnsupportedFilterMethod;
    if (ihdr.interlaceMethod != field::constants::interlaceMethod_None) return Result::UnsupportedInterlaceMethod; // XXX what about Adam7
    bs.Skip(sizeof(field::Checksum)); // XXX TODO PARSE
    return Result::OK;
}

template<typename ByteStreamer, typename ImageHeaderFn, typename ScanLineFn>
Result Parse(ByteStreamer& bs, ImageHeaderFn imageHeaderFn, ScanLineFn scanLineFn)
{
    // 3.1 PNG file signature
    {
        for(auto signature_byte: field::constants::png_signature) {
            const auto byte = bs.GetByte();
            if (!byte.has_value()) return Result::PrematureEndOfFile;
            if (*byte != signature_byte) return Result::BadSignature;
        }
    }

    // 3.2 First chunk must be IHDR
    Chunk header{bs};
    if (!header.ReadHeader()) return Result::PrematureEndOfFile;
    if (header.type != chunk_types::type_IHDR) return Result::InvalidFirstChunk;
    ImageHeader ihdr;
    if (auto result = ParseImageHeader(bs, ihdr); result != Result::OK) return result;
    imageHeaderFn(ihdr);

    // Set up the decode context; data may be scattered over multiple IDAT
    // chunks and doesn't even have to be split per scanline
    DecodeContext dctx{ihdr};

    // Parse remaining chunks sequentially
    while(!bs.eof())
    {
        Chunk chunk{bs};
        if (!chunk.ReadHeader()) return Result::PrematureEndOfFile;
        if (chunk.type == chunk_types::type_IHDR) return Result::MultipleIHDR;
        if (chunk.type == chunk_types::type_IDAT)
        {
            if (auto result = ParseImageData(chunk, bs, dctx, scanLineFn); result != Result::OK) return result;
            bs.Skip(sizeof(field::Checksum)); // XXX TODO PARSE
            continue;
        }
        if (chunk.type == chunk_types::type_IEND)
        {
            bs.Skip(sizeof(field::Checksum)); // XXX TODO PARSE
            break;
        }
        if (!chunk.type.IsAncillary()) return Result::UnsupportedCriticalChunkEncountered;
        chunk.Skip();
    }

    return Result::OK;
}

} // namespace mini_png
