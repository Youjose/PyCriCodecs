/**
 * @file hca_crypto.cpp
 * @brief HCA header and frame encryption/decryption helpers.
 *
 * Cipher behavior follows public HCA implementations and was cross-checked
 * against CRI SDK header masking/encryption behavior where recovered.
 */

#include "hca_crypto.hpp"
#include "hca_tables.hpp"

#include <array>

#include "../utilities/io.hpp"

namespace cricodecs::hca {

namespace {

using io::read_be;
using io::write_be;

constexpr uint32_t masked_chunk_id(uint32_t clear_id) noexcept {
    const uint32_t zero_lanes = ((clear_id - 0x01010101u) & ~clear_id & 0x80808080u);
    return clear_id ^ (zero_lanes ^ 0x80808080u);
}

void crypt_header(uint8_t* data, size_t header_size, uint16_t cipher_type) noexcept {
    if (header_size < 8) {
        return;
    }

    const size_t payload_end = header_size - 2;
    for (size_t offset = 0; offset + 4 <= payload_end;) {
        const uint32_t id = read_be<uint32_t>(data + offset) & HCA_MASK;
        size_t chunk_size = 0;
        switch (id) {
            case HCA_CHUNK_ID_HCA:
                chunk_size = 8;
                break;
            case HCA_CHUNK_ID_FMT:
                chunk_size = 16;
                break;
            case HCA_CHUNK_ID_COMP:
                chunk_size = 16;
                break;
            case HCA_CHUNK_ID_DEC:
                chunk_size = 12;
                break;
            case HCA_CHUNK_ID_VBR:
                chunk_size = 8;
                break;
            case HCA_CHUNK_ID_ATH:
                chunk_size = 6;
                break;
            case HCA_CHUNK_ID_LOOP:
                chunk_size = 16;
                break;
            case HCA_CHUNK_ID_CIPH:
                write_be<uint16_t>(data + offset + 4, cipher_type);
                chunk_size = 6;
                break;
            case HCA_CHUNK_ID_RVA:
                chunk_size = 8;
                break;
            case HCA_CHUNK_ID_COMM:
                chunk_size = offset + 5 <= payload_end ? 5 + data[offset + 4] : 0;
                break;
            case HCA_CHUNK_ID_PAD:
                chunk_size = payload_end - offset;
                break;
            default:
                break;
        }
        if (chunk_size == 0 || chunk_size > payload_end - offset) {
            break;
        }
        write_be<uint32_t>(data + offset, cipher_type == 56 ? masked_chunk_id(id) : id);
        offset += chunk_size;
    }

    write_be<uint16_t>(data + header_size - 2, tables::crc16_checksum(data, header_size - 2));
}

void transform_frames_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    std::span<const uint8_t, 256> table) noexcept {
    uint8_t* frame_data = hca_data.data() + info.file.header_size;
    const uint32_t available_frames = info.available_frame_count(hca_data.size());
    for (uint32_t frame_index = 0; frame_index < available_frames; ++frame_index) {
        auto* frame = frame_data + frame_index * info.codec.frame_size;
        cipher::transform_frame(table, {frame, info.codec.frame_size});
        write_be<uint16_t>(frame + info.codec.frame_size - 2, tables::crc16_checksum(frame, info.codec.frame_size - 2));
    }
}

} // namespace

std::expected<void, std::string> detail::encrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode) {
    if (cipher_type != 1 && cipher_type != 56) {
        return std::unexpected(std::string("HCA encrypt failed: unsupported target cipher type"));
    }

    std::array<uint8_t, 256> table{};
    cipher::init_cipher(table, cipher_type, keycode);

    std::array<uint8_t, 256> inverse{};
    for (size_t i = 0; i < inverse.size(); ++i) {
        inverse[table[i]] = static_cast<uint8_t>(i);
    }

    transform_frames_in_place(hca_data, info, inverse);
    crypt_header(hca_data.data(), info.file.header_size, cipher_type);
    return {};
}

std::expected<void, std::string> detail::encrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint16_t cipher_type,
    uint64_t keycode,
    uint16_t subkey) {
    return encrypt_in_place(hca_data, info, cipher_type, apply_subkey(keycode, subkey));
}

std::expected<void, std::string> detail::decrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode) {
    std::array<uint8_t, 256> table{};
    cipher::init_cipher(table, info.cipher.type, keycode);

    transform_frames_in_place(hca_data, info, table);
    crypt_header(hca_data.data(), info.file.header_size, 0);
    return {};
}

std::expected<void, std::string> detail::decrypt_in_place(
    std::span<uint8_t> hca_data,
    const HcaHeader& info,
    uint64_t keycode,
    uint16_t subkey) {
    return decrypt_in_place(hca_data, info, apply_subkey(keycode, subkey));
}

} // namespace cricodecs::hca
