#pragma once

#include "mini-deflate.h"
#include "mini-adler32.h"

namespace mini_zlib
{

namespace constants
{
    constexpr inline std::uint8_t compressionMethod_deflate = 8;
    constexpr inline std::uint8_t flag_FDICT = (1 << 5);
}

enum class Result
{
    OK,
    PrematureEndOfStream,
    UnsupportedCompressionMethod,
    HeaderChecksumError,
    DeflateError,
    ChecksumError
};

template<typename Streamer, typename Callback>
Result Decompress(Streamer& s, std::size_t length, Callback callback)
{
    const auto cmf = s.GetByte();
    const auto flg = s.GetByte();
    if (!cmf.has_value() || !flg.has_value()) return Result::PrematureEndOfStream;

    const auto cm = *cmf & 0xf;
    const auto cinfo = (*cmf >> 4) & 0xf;
    if (cm != constants::compressionMethod_deflate) return Result::UnsupportedCompressionMethod;
    if (((*cmf * 256) + *flg) % 31 != 0) return Result::HeaderChecksumError;

    const bool fdict = (*flg & constants::flag_FDICT) != 0;
    if (fdict) {
        // Skip preset dictionary; this is only useful for recompression
        s.Skip(4);
    }

    // TODO we should be able to stream towards the deflate code
    std::vector<uint8_t> compressedData;
    compressedData.reserve(length);
    for(std::size_t n = 2 + sizeof(mini_adler32::Value); n < length; n++)
    {
        const auto v = s.GetByte();
        if (!v.has_value()) return Result::PrematureEndOfStream;
        compressedData.push_back(*v);
    }
    const auto checksum = mini_adler32::ReadChecksum(s);
    if (!checksum.has_value()) return Result::PrematureEndOfStream;

    mini_deflate::BitStreamer bis{compressedData};
    mini_adler32::Adler32 adler;
    auto result = mini_deflate::Decompress(bis, [&](const auto& output) {
        adler.Update(output.begin(), output.end());
        callback(output);
    });
    if (result != mini_deflate::Result::OK) return Result::ChecksumError;
    if (*adler != checksum) return Result::ChecksumError;
    return Result::OK;
}

} // namespace mini_zlib
