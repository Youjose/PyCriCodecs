#pragma once
/**
 * @file acb_container.hpp
 * @brief ACB (Atom Cue sheet Binary) - CRI cue sheet container
 *
 * Unified class for extraction and name resolution from ACB files.
 * An ACB is a UTF table containing sub-tables that map cue names to waveforms.
 * 
 * Name resolution chain (from vgmstream):
 *  CueName → Cue → [Synth|Sequence|BlockSequence] → ... → Waveform
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <flat_map>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <expected>
#include <optional>
#include "acb_cue_graph.hpp"
#include "../utf/utf_table.hpp"
#include "../awb/awb_container.hpp"
#include "../utilities/io_reader.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::acb {

/**
 * Official CriAtomCraft raw-codec mapping for WaveformTable.EncodeType.
 *
 * The mapping is authoritative when ACB metadata is available. HCA-MX and
 * platform ADPCM payloads are not always distinguishable from bytes alone.
 */
[[nodiscard]] constexpr std::optional<awb::EntryCodec> encode_type_codec(uint8_t type) noexcept {
    using awb::EntryCodec;
    switch (type) {
    case 0:
    case 3:  return EntryCodec::Adx;
    case 1:  return EntryCodec::SwLpcm;
    case 2:  return EntryCodec::Hca;
    case 4:  return EntryCodec::WiiAdpcm;
    case 5:  return EntryCodec::DsAdpcm;
    case 6:  return EntryCodec::HcaMx;
    case 7:  return EntryCodec::Vag;
    case 8:  return EntryCodec::Atrac3;
    case 9:  return EntryCodec::ThreeDsAdpcm;
    case 10:
    case 14: return EntryCodec::Hevag;
    case 11:
    case 18: return EntryCodec::Atrac9;
    case 12: return EntryCodec::Xma2;
    case 13: return EntryCodec::WiiUAdpcm;
    case 19: return EntryCodec::AacM4a;
    case 24: return EntryCodec::SwitchOpus;
    default: return std::nullopt;
    }
}

[[nodiscard]] inline awb::EntryCodec resolve_entry_codec(
    uint8_t encode_type,
    std::span<const uint8_t> payload) noexcept {
    if (const auto codec = encode_type_codec(encode_type)) {
        return *codec;
    }
    return awb::probe_entry_codec(payload);
}

[[nodiscard]] constexpr std::string_view encode_type_extension(uint8_t type) noexcept {
    const auto codec = encode_type_codec(type);
    return codec ? awb::entry_codec_extension(*codec) : ".bin";
}

struct WaveformInfo {
    uint16_t id = 0xFFFF;
    uint16_t memory_awb_id = 0xFFFF;
    uint16_t stream_awb_id = 0xFFFF;
    uint16_t port_no = 0xFFFF;
    uint8_t  streaming = 0;    // 0=memory, 1=streaming, 2=prefetch+stream
    uint8_t  encode_type = 0;
    bool     loop_flag = false;   // CRI LoopFlag: 2 = loop, 1 = no loop
    int32_t  extension_data = -1; // WaveformExtensionDataTable row, -1 when absent
};

struct WaveNameInfo {
    uint32_t waveform_index = 0;
    uint16_t wave_id = 0;
    uint16_t port_no = 0xFFFF;
    std::string name;
    std::string name_raw;
    uint8_t streaming = 0;
    uint8_t encode_type = 0;
};

enum class AcbAwbBank : uint8_t {
    memory,
    stream,
};

struct AcbStreamAwbSlot {
    uint16_t port_no = 0;
    std::string name;
};

struct WaveformAwbEntry {
    uint32_t waveform_index = 0;
    uint16_t wave_id = 0xFFFF;
    uint32_t awb_index = 0;
    uint16_t port_no = 0xFFFF;
    bool stream_bank = false;
};

struct AcbSubtableInfo {
    std::string name;
    bool present = false;
    uint32_t row_count = 0;
    uint32_t column_count = 0;
    uint32_t data_size = 0;
};

class AcbContainer {
public:
    static constexpr int MAX_SYNTH_DEPTH = 3;
    static constexpr int MAX_SEQUENCE_DEPTH = 3;

    AcbContainer() = default;

    [[nodiscard]] static std::expected<AcbContainer, std::string> load(
        std::span<const uint8_t> data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<AcbContainer, std::string> load(
        std::vector<uint8_t>&& data,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] static std::expected<AcbContainer, std::string> load(
        const std::filesystem::path& path,
        const text::EncodingOptions& encoding = {}
    );
    [[nodiscard]] const std::vector<WaveNameInfo>& wave_names() const { return m_wave_names; }
    [[nodiscard]] std::string_view find_name(uint16_t wave_id) const;
    [[nodiscard]] std::string_view waveform_name(uint32_t index) const;
    [[nodiscard]] std::string_view waveform_name_raw(uint32_t index) const;
    /// Prefer ACB EncodeType; probe the resolved AWB payload only for unknown values.
    [[nodiscard]] std::expected<awb::EntryCodec, std::string> waveform_codec(uint32_t index) const;
    [[nodiscard]] std::string waveform_filename(uint32_t index, bool include_index_prefix = false) const;
    [[nodiscard]] std::optional<std::span<const uint8_t>> embedded_awb() const;
    [[nodiscard]] bool has_embedded_awb() const;
    [[nodiscard]] const std::vector<AcbStreamAwbSlot>& stream_awb_slots() const noexcept {
        return m_stream_awb_slots;
    }
    [[nodiscard]] std::optional<std::filesystem::path> stream_awb_path(uint16_t port_no = 0) const;
    [[nodiscard]] std::optional<std::filesystem::path> companion_awb_path() const;
    [[nodiscard]] std::expected<awb::AwbContainer, std::string> load_memory_awb() const;
    [[nodiscard]] std::expected<awb::AwbContainer, std::string> load_stream_awb(uint16_t port_no = 0) const;
    /// Load the primary full-audio bank: a stream AWB when present, otherwise the embedded bank.
    [[nodiscard]] std::expected<awb::AwbContainer, std::string> load_awb() const;
    [[nodiscard]] std::expected<uint16_t, std::string> awb_subkey() const;
    [[nodiscard]] std::expected<uint16_t, std::string> waveform_awb_subkey(uint32_t index) const;
    /// Resolve a waveform row through its AWB ID; row and AWB indices need not match.
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> waveform_awb_entry(
        uint32_t index,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> waveform_awb_entry(
        uint32_t index,
        AcbAwbBank bank) const;
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> waveform_awb_entry(
        uint32_t index,
        const awb::AwbContainer& awb,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> waveform_awb_entry(
        uint32_t index,
        const awb::AwbContainer& awb,
        AcbAwbBank bank) const;
    /// Replace the resolved entry in the supplied editable bank; the ACB itself is unchanged.
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> replace_waveform_data(
        uint32_t index,
        awb::AwbContainer& awb,
        std::span<const uint8_t> data,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<WaveformAwbEntry, std::string> replace_waveform_file(
        uint32_t index,
        awb::AwbContainer& awb,
        const std::filesystem::path& input_path,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<awb::AacEncryptionState, std::string> probe_waveform_aac_encryption(
        uint32_t index,
        uint64_t keycode) const;
    [[nodiscard]] bool has_aac_waveforms() const noexcept;
    [[nodiscard]] std::expected<awb::KeyRecoveryResult, std::string> recover_aac_key() const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_waveform_data(
        uint32_t index,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_waveform_stream_data(
        uint32_t index,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] std::expected<void, std::string> extract(
        const std::filesystem::path& output_dir,
        uint64_t aac_keycode = 0) const;
    [[nodiscard]] uint32_t waveform_count() const { return static_cast<uint32_t>(m_waveforms.size()); }
    [[nodiscard]] const WaveformInfo& waveform(uint32_t index) const { return m_waveforms[index]; }
    [[nodiscard]] const AcbCueGraph& cue_graph() const noexcept { return m_cue_graph; }
    [[nodiscard]] const std::vector<AcbWaveformCueView>& waveform_cue_views() const noexcept {
        return m_waveform_cue_views;
    }
    [[nodiscard]] const utf::UtfTable& header_table() const noexcept { return m_header; }
    [[nodiscard]] std::vector<AcbSubtableInfo> subtable_info() const;
    [[nodiscard]] std::optional<std::reference_wrapper<const utf::UtfTable>> subtable(std::string_view name) const;
    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }

private:
    // Source data
    io::SourceView m_source;
    std::filesystem::path m_source_path;
    text::EncodingOptions m_encoding;
    mutable std::optional<awb::AwbContainer> m_memory_awb;
    mutable std::flat_map<uint16_t, awb::AwbContainer> m_stream_awbs;
    
    // Root "Header" table
    utf::UtfTable m_header;
    
    // Sub-tables (loaded lazily, cached)
    struct SubTables {
        std::optional<utf::UtfTable> cue_name_table;
        std::optional<utf::UtfTable> cue_table;
        std::optional<utf::UtfTable> synth_table;
        std::optional<utf::UtfTable> sequence_table;
        std::optional<utf::UtfTable> track_table;
        std::optional<utf::UtfTable> track_event_table;  // >= v1.28
        std::optional<utf::UtfTable> command_table;       // <= v1.27
        std::optional<utf::UtfTable> waveform_table;
        std::optional<utf::UtfTable> block_table;
        std::optional<utf::UtfTable> block_sequence_table;
        
        // Sub-table data must outlive the UtfTable that references it
        std::vector<std::vector<uint8_t>> table_data;
    };

    struct SubtableDescriptor {
        const char* name = "";
        std::optional<utf::UtfTable> SubTables::* table = nullptr;
    };

    [[nodiscard]] static constexpr std::array<SubtableDescriptor, 10> subtable_descriptors() noexcept {
        return {{
            {"CueNameTable", &SubTables::cue_name_table},
            {"CueTable", &SubTables::cue_table},
            {"SynthTable", &SubTables::synth_table},
            {"SequenceTable", &SubTables::sequence_table},
            {"TrackTable", &SubTables::track_table},
            {"TrackEventTable", &SubTables::track_event_table},
            {"CommandTable", &SubTables::command_table},
            {"WaveformTable", &SubTables::waveform_table},
            {"BlockTable", &SubTables::block_table},
            {"BlockSequenceTable", &SubTables::block_sequence_table},
        }};
    }

    mutable SubTables m_sub;
    
    // Extracted info
    std::vector<WaveformInfo> m_waveforms;
    std::vector<AcbStreamAwbSlot> m_stream_awb_slots;
    std::vector<WaveNameInfo> m_wave_names;
    std::vector<std::string> m_waveform_names;
    std::vector<std::string> m_waveform_names_raw;
    AcbCueGraph m_cue_graph;
    std::vector<AcbWaveformCueView> m_waveform_cue_views;

    // Name lookup
    std::flat_map<uint16_t, std::string> m_name_map;

    std::optional<std::reference_wrapper<const utf::UtfTable>> load_subtable(const SubtableDescriptor& descriptor) const;
    std::optional<std::reference_wrapper<const utf::UtfTable>> load_subtable(std::string_view name) const;
    [[nodiscard]] std::expected<void, std::string> finish_load_from_source();
    [[nodiscard]] static std::expected<AcbContainer, std::string> load_source(
        io::SourceView source,
        const text::EncodingOptions& encoding);
    bool preload_waveforms();
    void preload_stream_awb_slots();

    void resolve_all_names();

    [[nodiscard]] static uint16_t waveform_id_for_bank(const WaveformInfo& waveform, bool is_memory_bank) noexcept;
    [[nodiscard]] static AcbAwbBank waveform_bank(const WaveformInfo& waveform) noexcept;
    [[nodiscard]] static uint16_t waveform_stream_port(const WaveformInfo& waveform) noexcept;
    [[nodiscard]] std::expected<std::reference_wrapper<const awb::AwbContainer>, std::string> awb_for_bank(
        AcbAwbBank bank,
        uint16_t port_no = 0) const;
    [[nodiscard]] std::expected<std::reference_wrapper<const awb::AwbContainer>, std::string> awb_for_waveform(
        uint32_t index,
        bool prefer_stream_bank = false) const;
    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> waveform_data_from_awb(
        uint32_t index,
        const awb::AwbContainer& awb,
        AcbAwbBank bank) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_waveform_data_from_awb(
        uint32_t index,
        const awb::AwbContainer& awb,
        uint64_t aac_keycode,
        AcbAwbBank bank) const;
    [[nodiscard]] std::expected<void, std::string> extract_file_from_awb(
        uint32_t index,
        const awb::AwbContainer& awb,
        AcbAwbBank bank,
        const std::filesystem::path& output_path,
        uint64_t aac_keycode) const;
};

} // namespace cricodecs::acb
