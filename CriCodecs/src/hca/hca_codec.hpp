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
#include <optional>
#include <span>
#include <string>
#include <type_traits>
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
    // Optional raw comp.channel_config override. Its high bit must agree with
    // ambisonics; the remaining high-bit value is preserved as opaque metadata.
    std::optional<uint8_t> channel_config;
    HcaQuality quality = HcaQuality::High;
    bool loop_enabled = false;
    bool ms_stereo = false;
    // Ambisonics is explicit because 4, 9, and 16 channels are also valid
    // ordinary layouts. Full-sphere orders use (order + 1)^2 channels.
    bool ambisonics = false;
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
        auto source = io::SourceView::from_file(path);
        if (!source) {
            return std::unexpected(
                "HCA load failed: could not open " + path.string() + " (" + source.error() + ")");
        }
        return load_source(std::move(*source), path);
    }

    [[nodiscard]] static std::expected<Hca, std::string> load(std::span<const uint8_t> data) {
        return load_source(io::SourceView::from_copy(data), {});
    }

    [[nodiscard]] const HcaHeader& header() const noexcept { return m_header; }
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> bytes() const {
        return with_source("HCA byte load failed", [](auto source) {
            return std::expected<std::vector<uint8_t>, std::string>(
                std::in_place, source.begin(), source.end());
        });
    }

    [[nodiscard]] std::expected<std::vector<int16_t>, std::string> decode(
        uint64_t keycode = 0,
        uint16_t subkey = 0
    ) const {
        return with_source("HCA decode failed", [=](auto source) {
            return hca::decode(source, keycode, subkey);
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encrypt(
        uint16_t cipher_type,
        uint64_t keycode = 0,
        uint16_t subkey = 0
    ) const {
        return with_source("HCA encrypt failed", [=](auto source) {
            return hca::encrypt(source, cipher_type, keycode, subkey);
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> decrypt(
        uint64_t keycode = 0,
        uint16_t subkey = 0
    ) const {
        return with_source("HCA decrypt failed", [=](auto source) {
            return hca::decrypt(source, keycode, subkey);
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> rebuild() const {
        return with_source("HCA rebuild failed", [](auto source) {
            return std::expected<std::vector<uint8_t>, std::string>(
                std::in_place, source.begin(), source.end());
        });
    }

private:
    friend struct HcaRecoveryAccess;
    friend std::expected<KeyRecoveryResult, std::string> recover_key(std::span<const Hca> sources);

    [[nodiscard]] static std::expected<HcaHeader, std::string> parse_header(std::span<const uint8_t> data);

    Hca(io::SourceView source, HcaHeader header, std::filesystem::path source_path)
        : m_source(std::move(source))
        , m_header(std::move(header))
        , m_source_path(std::move(source_path)) {}

    [[nodiscard]] static std::expected<Hca, std::string> load_source(
        io::SourceView source, std::filesystem::path path) {
        return parse_header(source).transform([&](HcaHeader header) {
            return Hca(std::move(source), std::move(header), std::move(path));
        });
    }

    template <typename Operation>
    [[nodiscard]] auto with_source(std::string_view context, Operation operation) const
        -> std::invoke_result_t<Operation, std::span<const uint8_t>> {
        std::error_code error;
        if (!m_source_path.empty() && !std::filesystem::exists(m_source_path, error)) {
            return std::unexpected(
                std::string(context) + ": source is unavailable: " + m_source_path.string() +
                (error ? " (" + error.message() + ")" : ""));
        }
        return operation(m_source.bytes);
    }

    io::SourceView m_source;
    HcaHeader m_header;
    std::filesystem::path m_source_path;
};

} // namespace cricodecs::hca
