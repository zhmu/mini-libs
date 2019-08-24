#pragma once

#include <cstdint>
#include <optional>

namespace mini_adler32
{
using Value = std::uint32_t;

namespace constants
{
    constexpr inline Value base = 65521;
    constexpr inline Value initial_Adler32 = 1;
}

template<typename Streamer>
std::optional<Value> ReadChecksum(Streamer& s)
{
    const auto c1 = static_cast<Value>(s.GetByte().value_or(0)) << 24;
    const auto c2 = static_cast<Value>(s.GetByte().value_or(0)) << 16;
    const auto c3 = static_cast<Value>(s.GetByte().value_or(0)) << 8;
    const auto c4 = s.GetByte();
    if (!c4.has_value()) return {};
    return static_cast<Value>(c1 | c2 | c3 | *c4);
}

struct Adler32
{
    template<typename Iterator>
    void Update(Iterator it, Iterator endIt)
    {
        uint32_t s1 = value & 0xffff;
        uint32_t s2 = (value >> 16) & 0xffff;
        for (/* nothing */; it != endIt; ++it) {
            s1 = (s1 + *it) % constants::base;
            s2 = (s2 + s1) % constants::base;
        }
        value = (s2 << 16) + s1;
    }

    inline Value operator*() const { return value; }

private:
    Value value{ constants::initial_Adler32 };
};

} // namespace mini_adler32
