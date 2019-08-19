#pragma once

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace mini_deflate
{

enum class Result
{
    OK,
    LengthCorrupt,
    InvalidBlockType,
    CorruptSymbol,
    EndOfStream,
    InvalidDynamicReference,
    CorruptDistance,
    InvalidSymbol
};


namespace detail
{
    constexpr int MAX_BITS = 15;
    constexpr int SYMBOL_LITERAL_FIRST = 0;
    constexpr int SYMBOL_LITERAL_LAST = 255;
    constexpr int SYMBOL_EOS = 256; // end-of-stream
    constexpr int SYMBOL_REPEAT_FIRST = 257;
    constexpr int SYMBOL_REPEAT_LAST = 285;

    // Byte offsets and number of bits to parse for repeat symbols
    constexpr std::array repeat_offset_base{ 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    constexpr std::array repeat_extra_bits { 0, 0, 0, 0, 0, 0, 0,  0,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,   4,   5,   5,   5,   5,   0 };
    static_assert(repeat_offset_base.size() == repeat_extra_bits.size());

    // Distance base offsets and number of bits to parse
    constexpr std::array dist_base { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    constexpr std::array dist_bits { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    static_assert(dist_base.size() == dist_bits.size());

    // Dynamic tree codelength order
    constexpr std::array codelen_order{ 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

    struct TreeNode {
        int symbol = 0;
        int length = 0;
        int code = 0;
    };

    struct Tree
    {
        Tree(std::size_t num_nodes, int min_bits, int max_bits)
            : nodes(num_nodes), min_bits(min_bits), max_bits(max_bits)
        {
        }

        Tree() = default;

        int min_bits{0};
        int max_bits{0};
        std::vector<TreeNode> nodes;

        using const_iterator = decltype(nodes)::const_iterator;

        const_iterator begin() const { return nodes.begin(); }
        const_iterator end() const { return nodes.end(); }

        TreeNode& operator[](std::size_t n) { return nodes[n]; }
        const TreeNode& operator[](std::size_t n) const { return nodes[n]; }
    };

    // 3.2.1 Constructs the unique Huffman tree for specified code lengths
    template<typename Iterator>
    Tree BuildCodeTree(Iterator cl_begin, Iterator cl_end)
    {
        // Step 1: count the number of codes for each code length
        std::array<int, MAX_BITS> bl_count{0};
        int min_bits = MAX_BITS + 1, max_bits = -1;
        std::for_each(cl_begin, cl_end, [&](auto cl) {
            bl_count[cl]++;
            if (cl != 0) {
                min_bits = std::min(min_bits, cl);
                max_bits = std::max(max_bits, cl);
            }
        });

        // Step 2: Find the numerical value of the smallest code for each code length
        std::array<int, MAX_BITS + 1> next_code{0};
        {
            int code = 0;
            for (int bits = 1; bits <= MAX_BITS; bits++) {
                code = (code + bl_count[bits - 1]) << 1;
                next_code[bits] = code;
            }
        }

        // Assign numerical values to all codes, using consecutive values for all
        // codes of the same length
        const auto max_code = std::distance(cl_begin, cl_end);
        Tree tree(max_code, min_bits, max_bits);
        for(std::size_t n = 0; n < max_code; n++) {
            const auto symbol_length = *cl_begin++;
            tree[n].symbol = n;
            if (symbol_length != 0) {
                tree[n].length = symbol_length;
                tree[n].code = (next_code[symbol_length] & ((1 << symbol_length) - 1));
                next_code[symbol_length]++;
            }
        }
        return tree;
    }

    // Default length tree (used for 'fixed huffman tree' blocks)
    const auto& GetFixedLengthTree()
    {
        static auto fixed_len_tree = []{
            std::array<int, 288> bl_count;
            for(int n =   0; n <= 143; n++) bl_count[n] = 8;
            for(int n = 144; n <= 255; n++) bl_count[n] = 9;
            for(int n = 256; n <= 279; n++) bl_count[n] = 7;
            for(int n = 280; n <= 287; n++) bl_count[n] = 8;
            return BuildCodeTree(bl_count.begin(), bl_count.end());
        }();
        return fixed_len_tree;
    }

    // Default distance tree (used for 'fixed huffman tree' blocks)
    const auto& GetFixedDistanceTree()
    {
        static auto fixed_dist_tree = []{
            std::array<int, 30> bl_count;
            for(int n =   0; n <= 29; n++) bl_count[n] = 5;
            return BuildCodeTree(bl_count.begin(), bl_count.end());
        }();
        return fixed_dist_tree;
    }

    template<typename BitStreamer>
    Result GetSymbol(BitStreamer& bs, const Tree& tree, int& symbol)
    {
        int cur_bits = tree.min_bits;
        const auto hb = bs.GetHuffmanBits(cur_bits);
        if (!hb.has_value()) return Result::EndOfStream;
        int cur_code = *hb;
        while(true) {
            auto it = std::find_if(tree.begin(), tree.end(), [&](const auto& n) {
                return n.code == cur_code && n.length == cur_bits;
            });
            if (it != tree.end()) {
                symbol = it->symbol;
                return Result::OK;
            }
            if (cur_bits == tree.max_bits)
                break;

            auto bit = bs.GetBit();
            if (!bit.has_value()) return Result::EndOfStream;
            cur_code = (cur_code << 1) | *bit;
            cur_bits++;
        }

        return Result::CorruptSymbol;
    }

    template<typename BitStreamer>
    Result ConstructDynamicTrees(BitStreamer& bs, Tree& len_tree, Tree& dist_tree)
    {
        // Note: no need to perform end-of-stream checking here; we do so
        // up once we get to GetSymbol()
        const int hlit = bs.GetDataBits(5).value_or(0) + 257;
        const int hdist = bs.GetDataBits(5).value_or(0) + 1;
        const int hclen = bs.GetDataBits(4).value_or(0) + 4;

        // Obtain code lengths; this is used to compress the code/distance trees
        std::array<int, codelen_order.size()> code_lengths{0};
        for(int n = 0; n < hclen; n++)
        {
            code_lengths[codelen_order[n]] = bs.GetDataBits(3).value_or(0);
        }
        const auto code_tree = BuildCodeTree(code_lengths.begin(), code_lengths.end());

        std::vector<int> bl_count;
        bl_count.reserve(hlit + hdist);
        while(bl_count.size() < hlit + hdist)
        {
            int symbol;
            if (auto result = GetSymbol(bs, code_tree, symbol); result != Result::OK)
                return result;
            if (symbol >= 0 && symbol <= 15) {
                bl_count.push_back(symbol);
            } else {
                int repeat = 0;
                int code = 0;
                switch(symbol)
                {
                    case 16: //  Copy the previous code length 3 - 6 times
                        if (bl_count.empty()) return Result::InvalidDynamicReference;
                        code = bl_count.back();
                        repeat = bs.GetDataBits(2).value_or(0) + 3;
                        break;
                    case 17: //  Repeat a code length of 0 for 3 - 10 times.
                        repeat = bs.GetDataBits(3).value_or(0) + 3;
                        break;
                    case 18: // Repeat a code length of 0 for 11 - 138 times
                        repeat = bs.GetDataBits(7).value_or(0) + 11;
                        break;

                }

                std::fill_n(std::back_inserter(bl_count), repeat, code);
            }
        }

        // Construct code/distance trees using counts decoded above
        len_tree = BuildCodeTree(bl_count.begin(), bl_count.begin() + hlit);
        dist_tree = BuildCodeTree(bl_count.begin() + hlit, bl_count.end());
        return Result::OK;
    }

    template<typename BitStreamer>
    Result DecompressBlock(BitStreamer& bs, const Tree& len_tree, const Tree& dist_tree, std::vector<uint8_t>& output)
    {
        while(true) {
            int symbol;
            if(auto result = GetSymbol(bs, len_tree, symbol); result != Result::OK)
                return result;
            if (symbol == SYMBOL_EOS) {
                break;
            }

            if (symbol >= SYMBOL_LITERAL_FIRST && symbol <= SYMBOL_LITERAL_LAST) {
                output.push_back(static_cast<uint8_t>(symbol - SYMBOL_LITERAL_FIRST));
            } else if (symbol >= SYMBOL_REPEAT_FIRST && symbol <= SYMBOL_REPEAT_LAST) {
                const int n = symbol - SYMBOL_REPEAT_FIRST;
                const int total_length = repeat_offset_base[n] + bs.GetDataBits(repeat_extra_bits[n]).value_or(0);
                int d_symbol;
                if (auto result = GetSymbol(bs, dist_tree, d_symbol); result != Result::OK) return result;
                auto dist = dist_base[d_symbol] + bs.GetDataBits(dist_bits[d_symbol]).value_or(0);
                if (output.size() < dist) return Result::CorruptDistance;

                int pos = output.size() - dist;
                for(int n = 0; n < total_length; n++, pos++) {
                    output.push_back(output[pos]);
                }
            } else {
                return Result::InvalidSymbol;
            }
        }
        return Result::OK;
    }

} // namespace detail

template<typename T>
struct BitStreamer
{
    BitStreamer(const T& data) : data(data) { }

    void reset()
    {
        byte_pos = 0;
        bit_pos = 0;
        bit_in_buf = 0;
        bit_buf = 0;
    }

    bool eof() const
    {
        return bit_in_buf == 0 && byte_pos == data.size();
    }

    // Bits for the data processed LSB->MSB
    std::optional<int> GetDataBits(int need)
    {
        while(bit_in_buf < need) {
            if (eof())
                return {};
            bit_buf |= (data[byte_pos] << bit_in_buf);
            bit_in_buf += 8;
            byte_pos++;
        }

        int value = bit_buf & ((1 << need) - 1);
        bit_buf >>= need;
        bit_in_buf -= need;
        return value;
    }

    auto GetBit()
    {
        return GetDataBits(1);
    }

    // Bits for the Huffmann code lookup are processed MSB->LSB
    std::optional<int> GetHuffmanBits(int need)
    {
        int v = 0;
        for(int n = 0; n < need; n++) {
            const auto bit = GetBit();
            if (!bit.has_value())
                return {};
            v = (v << 1) | *bit;
        }
        return v;
    }

    void SkipUntilByteBoundary()
    {
        if (bit_in_buf == 0 || bit_in_buf == 8)
            return;

        for (int n = bit_in_buf; !eof() && n > 0; n--) {
            GetBit();
        }
    }

private:
    const T& data;
    std::size_t byte_pos = 0;
    std::size_t bit_pos = 0;

    uint32_t bit_buf = 0;
    uint32_t bit_in_buf = 0;
};

template<typename BitStreamer, typename Callback>
Result Decompress(BitStreamer& bs, Callback callbackFn)
{
    while(true)
    {
        const auto bfinal = bs.GetDataBits(1);
        const auto btype = bs.GetDataBits(2);
        if (!bfinal.has_value() || !btype.has_value())
            return Result::EndOfStream;

        switch(*btype)
        {
            case 0: { // no compression
                bs.SkipUntilByteBoundary();
                auto getLength = [](BitStreamer& bs) -> std::optional<int> {
                    auto v = bs.GetDataBits(8);
                    auto w = bs.GetDataBits(8);
                    if (!v.has_value() || !w.has_value())
                        return {};
                    return std::optional(static_cast<uint16_t>(*v | (*w << 8)));
                };

                const auto len = getLength(bs);
                const auto nlen = getLength(bs);
                if (!len.has_value() || !nlen.has_value())
                    return Result::EndOfStream;
                if ((~*len & 0xffff) != *nlen) {
                    return Result::LengthCorrupt;
                }
                std::vector<uint8_t> output;
                for(int n = *len; n > 0; n--) {
                    const auto c = bs.GetDataBits(8);
                    if (!c.has_value())
                        return Result::EndOfStream;
                    output.push_back(*c);
                }
                callbackFn(output);
                break;
            }
            case 1: { // fixed hufmann codes
                std::vector<uint8_t> output;
                if (auto result = DecompressBlock(bs, detail::GetFixedLengthTree(), detail::GetFixedDistanceTree(), output); result != Result::OK) return result;
                callbackFn(output);
                break;
            }
            case 2: { // dynamic hufmann codes
                detail::Tree len_tree, dist_tree;
                if (auto result = detail::ConstructDynamicTrees(bs, len_tree, dist_tree); result != Result::OK)
                    return result;
                std::vector<uint8_t> output;
                if (auto result = DecompressBlock(bs, len_tree, dist_tree, output); result != Result::OK) return result;
                callbackFn(output);
                break;
            }
            case 3: // reserved
                return Result::InvalidBlockType;
        }
        if (bfinal)
            break;
    }
    return Result::OK;
}

} // namespace mini_deflate
