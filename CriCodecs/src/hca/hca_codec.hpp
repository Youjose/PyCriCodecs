#pragma once
/**
 * @file hca_codec.hpp
 * @brief Unified HCA (High Compression Audio) Codec - Decoder & Encoder
 * 
 * HCA support was initially bootstrapped from the public vgmstream and VGAudio
 * implementations, then cross-checked against CRI SDK encoder/decoder binaries.
 * 
 */

#include "hca_header.hpp"
#include "hca_key_recovery.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>
#include <expected>

#include "../utilities/io.hpp"
#include "../utilities/io_reader.hpp"
#include "../wav/wav_container.hpp"

namespace cricodecs::hca {

enum class HcaQuality : uint8_t {
    Highest = 0,  // compression ratio 4
    High    = 1,  // compression ratio 6
    Middle  = 2,  // compression ratio 8
    Low     = 3,  // compression ratio 10-12
    Lowest  = 4,  // compression ratio 12-16
};

struct HcaEncodeConfig {
    uint64_t keycode = 0;
    uint32_t sample_rate = 48000;
    uint32_t bitrate = 0;  // 0 = auto
    uint32_t loop_start = 0;
    uint32_t loop_end = 0;
    uint16_t version = HCA_VERSION_V200;
    uint16_t subkey = 0;
    uint8_t channel_count = 2;
    HcaQuality quality = HcaQuality::High;
    bool loop_enabled = false;
    bool ms_stereo = false;
};

class HcaDecoder;
class HcaEncoder;

[[nodiscard]] std::expected<std::vector<int16_t>, std::string> decode(
    std::span<const uint8_t> hca_data, 
    uint64_t keycode = 0, 
    uint16_t subkey = 0
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode(
    std::span<const int16_t> pcm_data,
    const HcaEncodeConfig& config
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode(
    const wav::WavContainer& wav,
    const HcaEncodeConfig& config
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encrypt(
    std::span<const uint8_t> hca_data,
    uint16_t cipher_type,
    uint64_t keycode = 0,
    uint16_t subkey = 0
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> decrypt(
    std::span<const uint8_t> hca_data,
    uint64_t keycode = 0,
    uint16_t subkey = 0
);

class Hca {
public:
    [[nodiscard]] static std::expected<Hca, std::string> load(const std::filesystem::path& path) {
        io::reader reader;
        if (auto result = reader.open(path); !result) {
            return std::unexpected("HCA load failed: could not open file `" + path.string() + "`");
        }

        auto parsed = parse_header(reader.data());
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        return Hca({}, std::move(parsed.value()), path);
    }

    [[nodiscard]] static std::expected<Hca, std::string> load(std::span<const uint8_t> data) {
        auto parsed = parse_header(data);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        return Hca(
            std::vector<uint8_t>(data.begin(), data.end()),
            std::move(parsed.value())
        );
    }

    [[nodiscard]] const HcaHeader& header() const noexcept { return m_header; }
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> bytes() const {
        if (!m_bytes.empty() || m_source_path.empty()) {
            return m_bytes;
        }
        return io::read_file_bytes(m_source_path, "HCA byte load failed");
    }

    [[nodiscard]] std::expected<std::vector<int16_t>, std::string> decode(
        uint64_t keycode = 0,
        uint16_t subkey = 0
    ) const {
        return with_source("HCA decode failed", [=](std::span<const uint8_t> source) {
            return hca::decode(source, keycode, subkey);
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encrypt(
        uint16_t cipher_type,
        uint64_t keycode = 0,
        uint16_t subkey = 0
    ) const {
        return with_source("HCA encrypt failed", [=](std::span<const uint8_t> source) {
            return hca::encrypt(source, cipher_type, keycode, subkey);
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> decrypt(
        uint64_t keycode = 0,
        uint16_t subkey = 0
    ) const {
        return with_source("HCA decrypt failed", [=](std::span<const uint8_t> source) {
            return hca::decrypt(source, keycode, subkey);
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild() const {
        if (!m_bytes.empty() || m_source_path.empty()) {
            return m_bytes;
        }
        return io::read_file_bytes(m_source_path, "HCA rebuild failed");
    }

private:
    friend struct HcaRecoveryAccess;
    friend std::expected<KeyRecoveryResult, std::string> recover_key(std::span<const Hca> sources);

    [[nodiscard]] static std::expected<HcaHeader, std::string> parse_header(std::span<const uint8_t> data);

    Hca(
        std::vector<uint8_t> bytes,
        HcaHeader header,
        std::filesystem::path source_path = {})
        : m_bytes(std::move(bytes))
        , m_header(std::move(header))
        , m_source_path(std::move(source_path)) {}

    template <typename Operation>
    [[nodiscard]] auto with_source(std::string_view context, Operation&& operation) const
        -> decltype(operation(std::span<const uint8_t>{})) {
        if (!m_bytes.empty() || m_source_path.empty()) {
            return operation(m_bytes);
        }

        io::reader reader;
        if (auto result = reader.open(m_source_path); !result) {
            return std::unexpected(
                std::string(context) + ": failed to open " + m_source_path.string() +
                " (" + result.error() + ")");
        }
        return operation(reader.data());
    }

    std::vector<uint8_t> m_bytes;
    HcaHeader m_header;
    std::filesystem::path m_source_path;
};

} // namespace cricodecs::hca
