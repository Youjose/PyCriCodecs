#pragma once
/**
 * @file adx_codec.hpp
 * @brief Public ADX/AHX decode and encode surface.
 *
 * ADX is the primary CRI ADPCM path; AHX is routed through the same ADX-family
 * container/header surface when the header encoding mode marks it as AHX.
 */

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "adx_crypto.hpp"
#include "../ahx/ahx_codec.hpp"
#include "../utilities/io.hpp"
#include "../wav/wav_container.hpp"

namespace cricodecs::adx {

    using AdxError = std::string;

    struct AdxLoop {
        uint16_t index;
        uint16_t type;
        uint32_t start_sample;
        uint32_t start_byte;
        uint32_t end_sample;
        uint32_t end_byte;

        wav::SampleLoop to_sample_loop() const {
            return wav::SampleLoop{
                .cue_point_id = index,
                .type = type,
                .start = start_sample,
                .end = end_sample,
                .fraction = 0,
                .play_count = 0
            };
        }
    };

    struct AdxHeader {
        uint16_t signature;
        uint16_t data_offset;
        uint8_t  encoding_mode;
        uint8_t  block_size;
        uint8_t  bit_depth;
        uint8_t  channels;
        uint32_t sample_rate;
        uint32_t sample_count;
        uint16_t highpass_freq;
        uint8_t  version;
        uint8_t  flags;
    };

    struct AdpcmHistory {
        int16_t prev1 = 0;
        int16_t prev2 = 0;
    };

    struct AdxDecodeResult {
        std::vector<int16_t> pcm_data;
        uint32_t sample_rate{};
        uint8_t channels{};
        uint32_t sample_count{};
        bool has_loops{};
        std::vector<AdxLoop> loops;
        uint32_t loop_start{};
        uint32_t loop_end{};
    };

    struct AdxEncodeConfig;

    class AdxDecoder {
        friend class Adx;

    public:
        AdxDecoder() = default;

        std::expected<void, AdxError> load(const std::string& path);
        std::expected<void, AdxError> load(std::span<const uint8_t> data);
        std::expected<AdxDecodeResult, AdxError> decode();
        std::expected<void, AdxError> decode_into(std::span<int16_t> pcm_output);

        const AdxHeader& header() const { return m_header; }
        bool has_loops() const { return !m_loops.empty(); }
        const std::vector<AdxLoop>& loops() const { return m_loops; }
        bool is_encrypted() const { return m_header.flags == 0x08 || m_header.flags == 0x09; }
        bool is_ahx() const { return m_header.encoding_mode == 0x10 || m_header.encoding_mode == 0x11; }
        
        void set_key_type8(std::string_view key);
        void set_key_type9(uint64_t key, uint16_t subkey = 0);
        void set_key_triplet(uint16_t start, uint16_t mult, uint16_t add);
        void set_ahx_key(uint16_t start, uint16_t mult, uint16_t add);

    private:
        io::reader m_reader;
        AdxHeader m_header{};
        std::vector<AdxLoop> m_loops;
        std::vector<AdpcmHistory> m_history;
        bool m_loaded = false;
        int32_t m_coefficients[2] = {0, 0};

        std::optional<AdxKeyState> m_key;
        std::optional<ahx::AhxKey> m_ahx_key;
        
        std::expected<void, AdxError> parse_header();
        [[nodiscard]] uint32_t samples_per_block() const noexcept;
        [[nodiscard]] ahx::AhxDecodeConfig ahx_config() const;
        void set_shared_key(AdxKeyState key);
        void calculate_coefficients();
        void decode_block(io::reader& reader, int16_t* output, 
                          size_t output_stride, AdpcmHistory& history,
                          AdxKeyState* key_state,
                          uint32_t samples_to_decode);
    };

    class Adx {
    public:
        Adx() = default;

        [[nodiscard]] static std::expected<Adx, AdxError> load(const std::filesystem::path& path);
        [[nodiscard]] static std::expected<Adx, AdxError> load(std::span<const uint8_t> data);

        [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
        [[nodiscard]] const AdxHeader& header() const { return m_decoder.header(); }
        [[nodiscard]] bool has_loops() const { return m_decoder.has_loops(); }
        [[nodiscard]] const std::vector<AdxLoop>& loops() const { return m_decoder.loops(); }
        [[nodiscard]] bool is_encrypted() const { return m_decoder.is_encrypted(); }
        [[nodiscard]] bool is_ahx() const { return m_decoder.is_ahx(); }

        void set_key_type8(std::string_view key) { m_decoder.set_key_type8(key); }
        void set_key_type9(uint64_t key, uint16_t subkey = 0) { m_decoder.set_key_type9(key, subkey); }
        void set_key_triplet(uint16_t start, uint16_t mult, uint16_t add) { m_decoder.set_key_triplet(start, mult, add); }
        void set_ahx_key(uint16_t start, uint16_t mult, uint16_t add) { m_decoder.set_ahx_key(start, mult, add); }

        [[nodiscard]] std::expected<AdxDecodeResult, AdxError> decode();
        [[nodiscard]] std::expected<void, AdxError> decode_into(std::span<int16_t> pcm_output);
        [[nodiscard]] std::expected<std::vector<uint8_t>, AdxError> decrypt() const;
        [[nodiscard]] std::expected<std::vector<uint8_t>, AdxError> rebuild() const;
        [[nodiscard]] std::expected<std::vector<uint8_t>, AdxError> encode(
            const AdxEncodeConfig& config,
            std::span<const AdxLoop> loops = {}
        );

    private:
        std::vector<uint8_t> m_source_bytes;
        std::filesystem::path m_source_path;
        AdxDecoder m_decoder;

        [[nodiscard]] std::expected<std::vector<uint8_t>, AdxError> source_bytes(
            std::string_view context) const;
        void copy_decode_settings_to(AdxDecoder& decoder) const;
    };

    struct AdxEncodeConfig {
        uint32_t sample_rate = 44100;
        uint8_t channels = 2;
        uint8_t bit_depth = 4;
        uint8_t block_size = 18;
        uint8_t encoding_mode = 3;
        uint16_t highpass_freq = 500;
        uint8_t filter_id = 0;
        uint8_t version = 4;
        
        uint8_t encryption_type = 0;
        bool delete_samples_after_loop_end = false;
        std::string key_string;
        uint64_t key64 = 0;
        uint16_t subkey = 0;
        ahx::AhxKey ahx_key{};
        ahx::AhxBitAllocationPattern ahx_bit_allocation_pattern = ahx::default_bit_allocation_pattern();
    };

    class AdxEncoder {
    public:
        static std::expected<std::vector<uint8_t>, AdxError> encode(
            std::span<const int16_t> pcm_data,
            const AdxEncodeConfig& config,
            std::span<const AdxLoop> loops = {}
        );
        static std::expected<std::vector<uint8_t>, AdxError> encode(
            const wav::WavContainer& wav,
            const AdxEncodeConfig& config,
            std::span<const AdxLoop> loops = {}
        );

        static std::expected<void, AdxError> encode_to_file(
            const std::string& path,
            std::span<const int16_t> pcm_data,
            const AdxEncodeConfig& config,
            std::span<const AdxLoop> loops = {}
        );

    private:
        static void calculate_coefficients(int32_t* coeffs, uint16_t highpass_freq, uint32_t sample_rate);
        static void encode_block(
            std::vector<uint8_t>& buffer,
            std::span<const int16_t> samples,
            int32_t* coeffs,
            AdpcmHistory& history,
            const AdxEncodeConfig& config,
            AdxKeyState* key_state = nullptr
        );
    };

} // namespace cricodecs::adx
