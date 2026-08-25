/**
 * @file crilayla.cpp
 * @brief CRILAYLA compression and decompression.
 *
 * The format behavior follows CRI's CPK tools. The implementation is
 * CriCodecs work by Youjose.
 */

#include "crilayla.hpp"

#include "../utilities/io_endian.hpp"
#include "../utilities/io_reader.hpp"
#include "../utilities/simd.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <vector>

namespace cricodecs::crilayla {

namespace {

using io::read_le;
using io::write_le;

constexpr std::array<uint8_t, 8> magic = {'C', 'R', 'I', 'L', 'A', 'Y', 'L', 'A'};
constexpr size_t header_size = 0x10;
constexpr size_t prefix_size = 0x100;
constexpr uint32_t hash_bucket_count = 0x40000;
constexpr uint32_t hash_node_capacity = 0x4002;
constexpr uint32_t hash_window_size = 0x2002;
constexpr uint32_t max_bucket_depth = 0x100;
constexpr uint32_t max_general_match_length = 0xB20;

uint32_t low_mask(uint32_t bit_count) {
    if (bit_count == 0) {
        return 0;
    }
    if (bit_count >= 32) {
        return 0xFFFFFFFFu;
    }
    return (1u << bit_count) - 1u;
}

class ReverseBitReader {
public:
    explicit ReverseBitReader(std::span<const uint8_t> bytes)
        : m_bytes(bytes), m_index(static_cast<int64_t>(bytes.size()) - 1) {}

    std::expected<uint32_t, std::string> read(uint32_t bit_count) {
        // CRILAYLA decodes the payload from the end toward the start.
        while (m_bit_count < bit_count) {
            const uint32_t bytes_to_load = ((24u - m_bit_count) >> 3u) + 1u;
            m_bit_count += bytes_to_load * 8u;

            for (uint32_t i = 0; i < bytes_to_load; ++i) {
                if (m_index < 0) {
                    return std::unexpected("CRILAYLA decompress failed: bitstream underflow");
                }
                m_bit_data = (m_bit_data << 8u) | m_bytes[static_cast<size_t>(m_index--)];
            }
        }

        const uint32_t value = (m_bit_data >> (m_bit_count - bit_count)) & low_mask(bit_count);
        m_bit_count -= bit_count;
        return value;
    }

private:
    std::span<const uint8_t> m_bytes;
    int64_t m_index = -1;
    uint32_t m_bit_count = 0;
    uint32_t m_bit_data = 0;
};

bool write_decoded_byte(std::vector<uint8_t>& output, int64_t& write_index, uint8_t byte) {
    output[static_cast<size_t>(write_index)] = byte;
    if (write_index == static_cast<int64_t>(prefix_size)) {
        --write_index;
        return true;
    }
    --write_index;
    return false;
}

std::expected<uint32_t, std::string> read_match_length(ReverseBitReader& reader) {
    auto base = reader.read(2);
    if (!base) {
        return std::unexpected(base.error());
    }
    uint32_t length = *base;
    if (length < 3) {
        return length + 3;
    }

    auto extra = reader.read(3);
    if (!extra) {
        return std::unexpected(extra.error());
    }
    length += *extra;
    if (length < 10) {
        return length + 3;
    }

    extra = reader.read(5);
    if (!extra) {
        return std::unexpected(extra.error());
    }
    length += *extra;
    if (length == 41) {
        uint32_t extension;
        do {
            auto value = reader.read(8);
            if (!value) {
                return std::unexpected(value.error());
            }
            extension = *value;
            length += extension;
        } while (extension == 255);
    }
    return length + 3;
}

std::expected<std::vector<uint8_t>, std::string> decode_payload(
    std::span<const uint8_t> payload,
    std::span<const uint8_t> prefix,
    uint32_t decompressed_body_size
) {
    // The first 0x100 bytes are stored verbatim after the payload and copied
    // into the front of the output before the backwards body reconstruction.
    std::vector<uint8_t> output(static_cast<size_t>(decompressed_body_size) + prefix_size, 0);
    std::copy(prefix.begin(), prefix.end(), output.begin());

    if (decompressed_body_size == 0) {
        return output;
    }

    ReverseBitReader reader(payload);
    int64_t write_index = static_cast<int64_t>(output.size()) - 1;

    while (write_index >= static_cast<int64_t>(prefix_size)) {
        auto marker = reader.read(1);
        if (!marker) {
            return std::unexpected(marker.error());
        }

        if (*marker == 0) {
            auto literal = reader.read(8);
            if (!literal) {
                return std::unexpected(literal.error());
            }
            if (write_decoded_byte(output, write_index, static_cast<uint8_t>(*literal))) {
                break;
            }
            continue;
        }

        auto offset_result = reader.read(13);
        if (!offset_result) {
            return std::unexpected(offset_result.error());
        }
        const uint32_t offset = *offset_result;

        auto length_result = read_match_length(reader);
        if (!length_result) {
            return std::unexpected(length_result.error());
        }
        uint32_t length = *length_result;
        // Offsets are relative to the current backwards write position.
        int64_t read_index = write_index + static_cast<int64_t>(offset) + 3;
        while (length > 0) {
            if (read_index < static_cast<int64_t>(prefix_size) ||
                read_index >= static_cast<int64_t>(output.size())) {
                return std::unexpected("CRILAYLA decompress failed: back-reference is out of range");
            }

            if (write_decoded_byte(output, write_index, output[static_cast<size_t>(read_index--)])) {
                break;
            }
            --length;
        }
    }

    return output;
}

uint32_t hash_triplet(std::span<const uint8_t> bytes, uint32_t index) {
    return (
        35023u * bytes[index] +
        45007u * bytes[index + 1] +
        55001u * bytes[index + 2]
    ) % hash_bucket_count;
}

bool spans_equal(std::span<const uint8_t> bytes, uint32_t lhs, uint32_t rhs, uint32_t length) {
    return std::equal(
        bytes.begin() + static_cast<std::ptrdiff_t>(lhs),
        bytes.begin() + static_cast<std::ptrdiff_t>(lhs + length),
        bytes.begin() + static_cast<std::ptrdiff_t>(rhs)
    );
}

bool write_bits(io::bit_writer& writer, uint32_t width, uint32_t value) {
    const size_t position = writer.position();
    writer.write(value, static_cast<int>(width));
    return writer.position() == position + width;
}

struct MatchResult {
    uint32_t position = 0;
    uint32_t length = 0;
};

struct HashNode {
    int32_t next = -1;
    uint32_t position = 0;
};

class HashMatchState {
public:
    explicit HashMatchState(std::span<const uint8_t> bytes)
        : m_bytes(bytes),
          m_heads(hash_bucket_count, -1),
          m_nodes(hash_node_capacity) {
        m_free_nodes.reserve(hash_node_capacity);
        for (int32_t index = static_cast<int32_t>(hash_node_capacity) - 1; index >= 0; --index) {
            m_free_nodes.push_back(index);
        }
    }

    MatchResult find(uint32_t position, uint32_t window_low) const {
        MatchResult best;
        const uint32_t bucket = hash_triplet(m_bytes, position);

        // The native encoder walks only the current hash bucket, not a full
        // brute-force search over the entire 0x2002-byte window.
        for (int32_t node_index = m_heads[bucket]; node_index >= 0; node_index = m_nodes[static_cast<size_t>(node_index)].next) {
            const uint32_t candidate = m_nodes[static_cast<size_t>(node_index)].position;
            if (candidate < window_low || position - candidate < 3u) {
                continue;
            }

            const uint32_t max_length = std::min(
                static_cast<uint32_t>(m_bytes.size()) - position,
                max_general_match_length
            );

            if (m_bytes[candidate] != m_bytes[position]) {
                continue;
            }

            uint32_t length = 0;
            for (; length + simd::bytes32::size() <= max_length;
                 length += simd::bytes32::size()) {
                const auto different =
                    simd::load<simd::bytes32>(m_bytes.data() + candidate + length) !=
                    simd::load<simd::bytes32>(m_bytes.data() + position + length);
                if (std::simd::any_of(different)) {
                    length += static_cast<uint32_t>(std::simd::reduce_min_index(different));
                    break;
                }
            }
            while (length < max_length &&
                   m_bytes[candidate + length] == m_bytes[position + length]) {
                ++length;
            }

            if (length > best.length) {
                best.position = candidate;
                best.length = length;
            }
        }

        if (best.length < 3) {
            return {};
        }

        return best;
    }

    void insert(uint32_t position, uint32_t window_low) {
        const uint32_t bucket = hash_triplet(m_bytes, position);
        uint32_t carried = position;
        int32_t last = -1;
        uint32_t depth = 0;

        // The bucket keeps newer positions closer to the head by swapping the
        // carried position forward through older entries.
        for (int32_t node_index = m_heads[bucket]; node_index >= 0; node_index = m_nodes[static_cast<size_t>(node_index)].next) {
            HashNode& node = m_nodes[static_cast<size_t>(node_index)];
            if (node.position < carried) {
                std::swap(node.position, carried);
            }
            last = node_index;
            ++depth;
        }

        if (last >= 0 && position - carried > 3u) {
            const uint32_t tail_position = m_nodes[static_cast<size_t>(last)].position;
            const uint32_t compare_length = std::min<uint32_t>(
                static_cast<uint32_t>(m_bytes.size()) - tail_position,
                max_general_match_length
            );
            // The native code avoids extending the chain with a duplicate tail
            // candidate when the candidate bytes would be identical anyway.
            if (compare_length == 0 || spans_equal(m_bytes, tail_position, carried, compare_length)) {
                return;
            }
        }

        if (depth >= max_bucket_depth || carried <= window_low) {
            return;
        }

        const int32_t new_node = allocate_node(window_low);
        if (new_node < 0) {
            return;
        }

        m_nodes[static_cast<size_t>(new_node)] = HashNode{.next = -1, .position = carried};
        if (last < 0) {
            m_heads[bucket] = new_node;
        } else {
            m_nodes[static_cast<size_t>(last)].next = new_node;
        }
    }

private:
    int32_t allocate_node(uint32_t window_low) {
        if (m_free_nodes.empty()) {
            prune_old_nodes(window_low);
        }
        if (m_free_nodes.empty()) {
            return -1;
        }

        const int32_t node = m_free_nodes.back();
        m_free_nodes.pop_back();
        return node;
    }

    void recycle_node(int32_t node_index) {
        m_nodes[static_cast<size_t>(node_index)] = {};
        m_free_nodes.push_back(node_index);
    }

    void prune_old_nodes(uint32_t window_low) {
        for (uint32_t bucket = 0; bucket < hash_bucket_count; ++bucket) {
            int32_t* link = &m_heads[static_cast<size_t>(bucket)];
            while (*link >= 0) {
                const int32_t node_index = *link;
                HashNode& node = m_nodes[static_cast<size_t>(node_index)];
                if (node.position < window_low) {
                    *link = node.next;
                    recycle_node(node_index);
                    continue;
                }
                link = &node.next;
            }
        }
    }

    std::span<const uint8_t> m_bytes;
    std::vector<int32_t> m_heads;
    std::vector<HashNode> m_nodes;
    std::vector<int32_t> m_free_nodes;
};

bool encode_match_length(io::bit_writer& writer, uint32_t length) {
    // Match lengths are stored as length-3 using the native 2/3/5/8/+255 tiers.
    uint32_t encoded = length - 3u;
    if (encoded < 3u) {
        return write_bits(writer, 2, encoded);
    }
    if (!write_bits(writer, 2, 3u)) {
        return false;
    }

    if (encoded < 10u) {
        return write_bits(writer, 3, encoded - 3u);
    }
    if (!write_bits(writer, 3, 7u)) {
        return false;
    }

    if (encoded < 41u) {
        return write_bits(writer, 5, encoded - 10u);
    }
    if (!write_bits(writer, 5, 31u)) {
        return false;
    }

    if (encoded < 296u) {
        return write_bits(writer, 8, encoded - 41u);
    }
    if (!write_bits(writer, 8, 255u)) {
        return false;
    }

    encoded -= 296u;
    while (encoded >= 255u) {
        if (!write_bits(writer, 8, 255u)) {
            return false;
        }
        encoded -= 255u;
    }
    return write_bits(writer, 8, encoded);
}

std::expected<uint32_t, std::string> encode_body(
    std::span<const uint8_t> reversed_body,
    std::span<uint8_t> inner_block
) {
    // The core encoder operates on the reversed post-prefix body so that the
    // final payload can still be decoded backwards by the stock Layla decoder.
    io::bit_writer writer(inner_block.subspan(header_size));
    HashMatchState state(reversed_body);

    const uint32_t body_size = static_cast<uint32_t>(reversed_body.size());
    const uint32_t search_limit = body_size > 3u ? body_size - 3u : 0u;
    uint32_t position = 0;

    while (position < search_limit) {
        const uint32_t window_low = position > hash_window_size ? position - hash_window_size : 0u;

        uint32_t consumed = 0;
        if (position >= 3u && position + 16u < search_limit) {
            const uint8_t repeated = reversed_body[position];
            uint32_t run_probe = position - 3u;
            bool repeated_window = true;
            // Offset 0 is a special repeated-byte path in the native encoder,
            // gated by a 19-byte all-equal window from pos-3 through pos+15.
            for (uint32_t i = 0; i < 19u; ++i) {
                if (reversed_body[run_probe + i] != repeated) {
                    repeated_window = false;
                    break;
                }
            }

            if (repeated_window) {
                uint32_t run_length = 3u;
                while (position + run_length < body_size && reversed_body[position + run_length] == repeated) {
                    ++run_length;
                }

                if (!write_bits(writer, 1, 1u) ||
                    !write_bits(writer, 13, 0u) ||
                    !encode_match_length(writer, run_length)) {
                    return std::unexpected("CRILAYLA encoder buffer overflow");
                }
                consumed = run_length;
            }
        }

        if (consumed == 0) {
            MatchResult match = state.find(position, window_low);
            if (match.length == 0) {
                if (!write_bits(writer, 1, 0u) ||
                    !write_bits(writer, 8, reversed_body[position])) {
                    return std::unexpected("CRILAYLA encoder buffer overflow");
                }
                consumed = 1;
            } else {
                const uint32_t length = std::min(match.length, max_general_match_length);
                const uint32_t offset = position - match.position - 3u;
                // General matches use a 13-bit offset and are capped to 0xB20
                // bytes exactly like the native path in CpkMaker.dll.
                if (!write_bits(writer, 1, 1u) ||
                    !write_bits(writer, 13, offset) ||
                    !encode_match_length(writer, length)) {
                    return std::unexpected("CRILAYLA encoder buffer overflow");
                }
                consumed = length;
            }
        }

        const uint32_t next_position = position + consumed;
        while (position < next_position) {
            if (position < search_limit) {
                state.insert(
                    position,
                    position > hash_window_size ? position - hash_window_size : 0u
                );
            }
            ++position;
        }
    }

    while (position < body_size) {
        if (!write_bits(writer, 1, 0u) || !write_bits(writer, 8, reversed_body[position])) {
            return std::unexpected("CRILAYLA encoder buffer overflow");
        }
        ++position;
    }

    return static_cast<uint32_t>((writer.position() + 7u) / 8u);
}

uint32_t align_payload_size(uint32_t raw_size) {
    // The official wrapper stores an extra zero dword before the visible
    // bitstream and then rounds the payload to a 4-byte boundary.
    return (raw_size + 7u) & ~3u;
}

} // namespace

std::expected<std::vector<uint8_t>, std::string> decompress(std::span<const uint8_t> src) {
    if (src.size() < header_size + prefix_size) {
        return std::unexpected("CRILAYLA payload is too small");
    }
    if (!std::equal(magic.begin(), magic.end(), src.begin())) {
        return std::unexpected("CRILAYLA decompress failed: invalid header");
    }

    const uint32_t decompressed_body_size = read_le<uint32_t>(src.data() + 0x08);
    const uint32_t compressed_size = read_le<uint32_t>(src.data() + 0x0C);
    if (header_size + static_cast<size_t>(compressed_size) + prefix_size > src.size()) {
        return std::unexpected("CRILAYLA payload is truncated");
    }

    const auto payload = src.subspan(header_size, compressed_size);
    const auto prefix = src.subspan(header_size + compressed_size, prefix_size);
    return decode_payload(payload, prefix, decompressed_body_size);
}

std::vector<uint8_t> compress(std::span<const uint8_t> src) {
    if (src.size() < prefix_size) {
        return std::vector<uint8_t>(src.begin(), src.end());
    }

    const auto prefix = src.first(prefix_size);
    const auto body = src.subspan(prefix_size);
    if (body.empty()) {
        return std::vector<uint8_t>(src.begin(), src.end());
    }

    std::vector<uint8_t> reversed_body(body.begin(), body.end());
    std::reverse(reversed_body.begin(), reversed_body.end());

    std::vector<uint8_t> inner_block(body.size() + 0x200 + header_size, 0);
    auto encoded_size = encode_body(reversed_body, inner_block);
    if (!encoded_size) {
        return std::vector<uint8_t>(src.begin(), src.end());
    }

    const uint32_t payload_size = align_payload_size(*encoded_size);
    const uint32_t encoded_total_size = payload_size + static_cast<uint32_t>(header_size);
    if (encoded_total_size >= body.size()) {
        return std::vector<uint8_t>(src.begin(), src.end());
    }

    write_le<uint32_t>(inner_block.data() + 0x08, static_cast<uint32_t>(body.size()));
    write_le<uint32_t>(inner_block.data() + 0x0C, payload_size);
    std::fill(
        inner_block.begin() + static_cast<std::ptrdiff_t>(header_size + *encoded_size),
        inner_block.begin() + static_cast<std::ptrdiff_t>(encoded_total_size),
        uint8_t{0});
    // Reverse the inner block back into the on-disk ordering expected by the
    // official decoder: header dwords first, backwards-decoded payload next.
    std::reverse(
        inner_block.begin(),
        inner_block.begin() + static_cast<std::ptrdiff_t>(encoded_total_size));

    std::vector<uint8_t> output(header_size + payload_size + prefix_size, 0);
    std::copy(magic.begin(), magic.end(), output.begin());
    write_le<uint32_t>(output.data() + 0x08, static_cast<uint32_t>(body.size()));
    write_le<uint32_t>(output.data() + 0x0C, payload_size);
    std::copy(
        inner_block.begin(),
        inner_block.begin() + static_cast<std::ptrdiff_t>(payload_size),
        output.begin() + static_cast<std::ptrdiff_t>(header_size));
    std::copy(prefix.begin(), prefix.end(), output.begin() + static_cast<std::ptrdiff_t>(header_size + payload_size));

    auto roundtrip = decompress(output);
    // The stock compressor verifies its own output before keeping it; matching
    // that here avoids emitting a nominally smaller but invalid payload.
    if (!roundtrip || roundtrip->size() != src.size() || !std::equal(roundtrip->begin(), roundtrip->end(), src.begin())) {
        return std::vector<uint8_t>(src.begin(), src.end());
    }

    return output;
}

} // namespace cricodecs::crilayla
