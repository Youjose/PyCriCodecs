#pragma once
/**
 * @file usm_container.hpp
 * @brief USM/SofDec 2 chunked stream container API.
 *
 * Chunk IDs, metadata schemas, SFSH handling, and stream-header behavior are
 * grounded in official SofDec 2 tool evidence.
 * Public C++23 surface by Youjose.
 */

#include <array>
#include <cstddef>
#include <optional>
#include <iterator>
#include <vector>
#include <string>
#include <cstdint>
#include <expected>
#include <flat_map>
#include <map>
#include <filesystem>
#include <functional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../utf/utf_table.hpp"
#include "../utilities/io.hpp"
#include "../utilities/text_encoding.hpp"
#include "../video/ivf.hpp"
#include "usm_crypto.hpp"
#include "usm_key_recovery.hpp"

namespace cricodecs::usm {

// Confirmed against official SofDec 2 tools. SFSH is a fixed header variant,
// while the uncommon @-prefixed metadata IDs are kept symbolic so demuxed
// streams remain inspectable before their full payload semantics are modeled.
// PST is a picture-size table, ELM is an element/index table, STA carries
// ofs_byte/ofs_frmid/num_skip/resv rows, and ATP carries pic_size rows.
enum class UsmChunkType : uint32_t {
    CRID = 0x43524944, // "CRID"
    SFSH = 0x53465348, // "SFSH"
    AHX  = 0x40414858, // "@AHX"
    ELM  = 0x40454C4D, // "@ELM"
    ATP  = 0x40505441, // "@ATP"
    PST  = 0x40505354, // "@PST"
    SFV  = 0x40534656, // "@SFV"
    SFA  = 0x40534641, // "@SFA"
    ALP  = 0x40414C50, // "@ALP"
    CUE  = 0x40435545, // "@CUE"
    SBT  = 0x40534254, // "@SBT"
    STA  = 0x40535441, // "@STA"
    USR  = 0x40555352, // "@USR"
};

enum class UsmPayloadType : uint8_t {
    Stream = 0,
    Header = 1,
    SectionEnd = 2,
    Metadata = 3,
};

enum class UsmAudioCodec : uint8_t {
    Adx = 2,
    Hca = 4,
    Unknown = 0xFF,
};

enum class UsmSubtitleFormat : uint8_t {
    Auto,
    SourceText,
    Srt,
    Ass,
    Sbt,
};

[[nodiscard]] std::string_view audio_codec_name(uint8_t codec) noexcept;
[[nodiscard]] constexpr std::string_view audio_codec_name(UsmAudioCodec codec) noexcept {
    switch (codec) {
    case UsmAudioCodec::Adx:
        return "adx";
    case UsmAudioCodec::Hca:
        return "hca";
    case UsmAudioCodec::Unknown:
        return "unknown";
    }
    return "unknown";
}

struct UsmStreamId {
    UsmChunkType stream_id = UsmChunkType::CRID;
    uint8_t channel_no = 0;

    auto operator<=>(const UsmStreamId&) const = default;
};

struct UsmChunkHeader {
    static constexpr uint32_t encoded_header_size = 0x18;
    static constexpr uint32_t raw_header_size = 0x20;

    uint32_t magic = 0;
    uint32_t chunk_size = 0;
    // Big-endian offset from byte 0x08 of the chunk to the payload. The
    // ordinary 0x18 value therefore places payload bytes at absolute 0x20.
    uint16_t payload_offset = encoded_header_size;
    uint16_t padding = 0;
    uint8_t channel_no = 0;
    uint8_t reserved_0d = 0;
    // Big-endian payload type/flags word. The low two bits select
    // UsmPayloadType; higher bits are preserved.
    uint16_t payload_type_and_flags = 0;
    uint32_t frame_time = 0;
    uint32_t frame_rate = 0;
    uint32_t reserved_18 = 0;
    uint32_t reserved_1c = 0;

    [[nodiscard]] uint32_t body_size() const noexcept {
        return chunk_size >= encoded_header_size ? chunk_size - encoded_header_size : 0;
    }

    [[nodiscard]] uint32_t data_offset() const noexcept {
        return payload_offset >= encoded_header_size ? payload_offset - encoded_header_size : 0;
    }
};

template <class Byte>
class BasicUsmChunkView {
    static_assert(std::is_same_v<std::remove_cv_t<Byte>, uint8_t>);

public:
    BasicUsmChunkView() = default;
    BasicUsmChunkView(std::span<Byte> source, size_t offset)
        : m_source(source)
        , m_offset(offset)
        , m_header(read_header(source, offset)) {}

    [[nodiscard]] const UsmChunkHeader& header() const noexcept { return m_header; }
    [[nodiscard]] size_t offset() const noexcept { return m_offset; }
    [[nodiscard]] UsmChunkType chunk_type() const noexcept { return static_cast<UsmChunkType>(m_header.magic); }
    [[nodiscard]] UsmPayloadType payload_type() const noexcept {
        return static_cast<UsmPayloadType>(m_header.payload_type_and_flags & 0x03u);
    }
    [[nodiscard]] UsmStreamId stream_id() const noexcept {
        return UsmStreamId{
            .stream_id = chunk_type(),
            .channel_no = m_header.channel_no,
        };
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return valid_chunk_at(m_source, m_offset);
    }
    [[nodiscard]] std::span<Byte> full_bytes() const noexcept {
        if (!is_valid()) {
            return {};
        }
        return m_source.subspan(m_offset, static_cast<size_t>(m_header.chunk_size) + 8u);
    }
    [[nodiscard]] std::span<Byte> body_bytes() const noexcept {
        if (!is_valid()) {
            return {};
        }
        return m_source.subspan(m_offset + UsmChunkHeader::raw_header_size, m_header.body_size());
    }
    [[nodiscard]] std::span<Byte> payload_with_padding() const noexcept {
        if (!is_valid()) {
            return {};
        }
        const size_t begin = m_offset + UsmChunkHeader::raw_header_size + m_header.data_offset();
        const size_t end = m_offset + 8u + static_cast<size_t>(m_header.chunk_size);
        if (begin > end || end > m_source.size()) {
            return {};
        }
        return m_source.subspan(begin, end - begin);
    }
    [[nodiscard]] std::span<Byte> payload() const noexcept {
        auto bytes = payload_with_padding();
        if (m_header.padding > bytes.size()) {
            return {};
        }
        return bytes.first(bytes.size() - m_header.padding);
    }
    [[nodiscard]] std::span<Byte> padding() const noexcept {
        auto bytes = payload_with_padding();
        if (m_header.padding > bytes.size()) {
            return {};
        }
        return bytes.last(m_header.padding);
    }

private:
    [[nodiscard]] static UsmChunkHeader read_header(std::span<Byte> source, size_t offset) noexcept {
        UsmChunkHeader header{};
        if (offset > source.size() || source.size() - offset < UsmChunkHeader::raw_header_size) {
            return header;
        }
        const auto* data = source.data() + offset;
        header.magic = io::read_be<uint32_t>(data + 0x00);
        header.chunk_size = io::read_be<uint32_t>(data + 0x04);
        header.payload_offset = io::read_be<uint16_t>(data + 0x08);
        header.padding = io::read_be<uint16_t>(data + 0x0A);
        header.channel_no = data[0x0C];
        header.reserved_0d = data[0x0D];
        header.payload_type_and_flags = io::read_be<uint16_t>(data + 0x0E);
        header.frame_time = io::read_be<uint32_t>(data + 0x10);
        header.frame_rate = io::read_be<uint32_t>(data + 0x14);
        header.reserved_18 = io::read_be<uint32_t>(data + 0x18);
        header.reserved_1c = io::read_be<uint32_t>(data + 0x1C);
        return header;
    }

    [[nodiscard]] static bool valid_chunk_at(std::span<Byte> source, size_t offset) noexcept {
        if (offset > source.size() || source.size() - offset < UsmChunkHeader::raw_header_size) {
            return false;
        }
        const auto* data = source.data() + offset;
        const auto chunk_size = io::read_be<uint32_t>(data + 0x04);
        const auto payload_offset = io::read_be<uint16_t>(data + 0x08);
        const auto padding = io::read_be<uint16_t>(data + 0x0A);
        if (chunk_size < UsmChunkHeader::encoded_header_size ||
            payload_offset < UsmChunkHeader::encoded_header_size ||
            8u + static_cast<size_t>(chunk_size) > source.size() - offset) {
            return false;
        }
        const size_t body_size = chunk_size - UsmChunkHeader::encoded_header_size;
        const size_t data_offset = payload_offset - UsmChunkHeader::encoded_header_size;
        if (data_offset > body_size) {
            return false;
        }
        return padding <= body_size - data_offset;
    }

    std::span<Byte> m_source;
    size_t m_offset = 0;
    UsmChunkHeader m_header;
};

template <class Byte>
class BasicUsmChunkRange {
    static_assert(std::is_same_v<std::remove_cv_t<Byte>, uint8_t>);

public:
    explicit BasicUsmChunkRange(std::span<Byte> source)
        : m_source(source) {}

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = BasicUsmChunkView<Byte>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;
        iterator(std::span<Byte> source, size_t offset)
            : m_source(source)
            , m_offset(valid_chunk_at(source, offset) ? offset : source.size()) {}

        [[nodiscard]] value_type operator*() const noexcept {
            return value_type(m_source, m_offset);
        }

        iterator& operator++() noexcept {
            if (!valid_chunk_at(m_source, m_offset)) {
                m_offset = m_source.size();
                return *this;
            }
            const auto chunk_size = io::read_be<uint32_t>(m_source.data() + m_offset + 0x04);
            const size_t next = m_offset + 8u + static_cast<size_t>(chunk_size);
            m_offset = valid_chunk_at(m_source, next) ? next : m_source.size();
            return *this;
        }

        void operator++(int) noexcept {
            ++*this;
        }

        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
            return m_offset >= m_source.size();
        }

    private:
        [[nodiscard]] static bool valid_chunk_at(std::span<Byte> source, size_t offset) noexcept {
            return BasicUsmChunkView<Byte>(source, offset).is_valid();
        }

        std::span<Byte> m_source;
        size_t m_offset = 0;
    };

    [[nodiscard]] iterator begin() const noexcept { return iterator(m_source, 0); }
    [[nodiscard]] std::default_sentinel_t end() const noexcept { return {}; }

private:
    std::span<Byte> m_source;
};

using UsmChunkView = BasicUsmChunkView<const uint8_t>;
using MutableUsmChunkView = BasicUsmChunkView<uint8_t>;
using UsmChunkRange = BasicUsmChunkRange<const uint8_t>;
using MutableUsmChunkRange = BasicUsmChunkRange<uint8_t>;

[[nodiscard]] inline UsmChunkRange chunk_views(std::span<const uint8_t> source) noexcept {
    return UsmChunkRange(source);
}

[[nodiscard]] inline MutableUsmChunkRange chunk_views(std::span<uint8_t> source) noexcept {
    return MutableUsmChunkRange(source);
}

struct SfshHeader {
    static constexpr uint32_t raw_header_size = 0x40;
    static constexpr uint32_t payload_offset_value = 0x40;

    std::array<uint8_t, raw_header_size> raw{};
    uint16_t version = 0;
    uint16_t field_06 = 0;
    uint16_t field_08 = 0;
    uint16_t field_0a = 0;
    uint16_t field_0c = 0;
    uint32_t field_0e = 0;
    uint32_t field_12 = 0;
    uint32_t payload_size = 0;
    uint32_t codec_word = 0;
    uint16_t field_1e = 0;
    uint16_t field_20 = 0;

    [[nodiscard]] uint32_t payload_offset() const noexcept {
        return payload_offset_value;
    }

    [[nodiscard]] uint8_t codec_marker() const noexcept {
        return static_cast<uint8_t>(codec_word >> 24);
    }

    [[nodiscard]] uint8_t normalized_codec_marker() const noexcept {
        switch (codec_marker()) {
        case 4:
            return 10;
        case 5:
            return 12;
        case 6:
            return 14;
        default:
            return 0;
        }
    }
};

struct UsmPayload {
    std::vector<uint8_t> owned;
    std::span<const uint8_t> view;

    UsmPayload() = default;

    UsmPayload(std::span<const uint8_t> bytes)
        : view(bytes) {}

    UsmPayload(std::vector<uint8_t>&& bytes)
        : owned(std::move(bytes))
        , view(owned) {}

    UsmPayload(const UsmPayload& other)
        : owned(other.owned)
        , view(owned.empty() ? other.view : std::span<const uint8_t>(owned)) {}

    UsmPayload& operator=(const UsmPayload& other) {
        if (this != &other) {
            owned = other.owned;
            view = owned.empty() ? other.view : std::span<const uint8_t>(owned);
        }
        return *this;
    }

    UsmPayload(UsmPayload&& other) noexcept
        : owned(std::move(other.owned))
        , view(owned.empty() ? other.view : std::span<const uint8_t>(owned)) {}

    UsmPayload& operator=(UsmPayload&& other) noexcept {
        if (this != &other) {
            owned = std::move(other.owned);
            view = owned.empty() ? other.view : std::span<const uint8_t>(owned);
        }
        return *this;
    }

    [[nodiscard]] const uint8_t* data() const noexcept { return view.data(); }
    [[nodiscard]] size_t size() const noexcept { return view.size(); }
    [[nodiscard]] bool empty() const noexcept { return view.empty(); }
    [[nodiscard]] const uint8_t* begin() const noexcept { return view.data(); }
    [[nodiscard]] const uint8_t* end() const noexcept { return view.data() + view.size(); }
    [[nodiscard]] const uint8_t& operator[](size_t index) const noexcept { return view[index]; }
    [[nodiscard]] operator std::span<const uint8_t>() const noexcept { return view; }
};

struct UsmChunk {
    UsmChunkHeader header;
    UsmPayload payload;
    UsmPayload padding;

    [[nodiscard]] UsmChunkType chunk_type() const noexcept {
        return static_cast<UsmChunkType>(header.magic);
    }

    [[nodiscard]] UsmStreamId stream_id() const noexcept {
        return UsmStreamId{
            .stream_id = chunk_type(),
            .channel_no = header.channel_no,
        };
    }

    [[nodiscard]] UsmPayloadType payload_type() const noexcept {
        return static_cast<UsmPayloadType>(header.payload_type_and_flags & 0x03u);
    }

    [[nodiscard]] bool is_stream() const noexcept {
        return payload_type() == UsmPayloadType::Stream;
    }

    [[nodiscard]] bool is_utf_payload() const noexcept {
        return payload.size() >= 4 &&
            payload[0] == '@' &&
            payload[1] == 'U' &&
            payload[2] == 'T' &&
            payload[3] == 'F';
    }

    [[nodiscard]] bool belongs_to(UsmStreamId id) const noexcept {
        return stream_id() == id;
    }

    [[nodiscard]] std::expected<utf::UtfTable, std::string> load_utf_payload() const {
        return utf::UtfTable::load(payload);
    }

    [[nodiscard]] size_t packed_size() const noexcept {
        return UsmChunkHeader::raw_header_size + payload.size() + header.padding;
    }

    void append_to(std::vector<uint8_t>& bytes) const {
        const size_t header_offset = bytes.size();
        bytes.resize(header_offset + UsmChunkHeader::raw_header_size, 0);
        auto* destination = bytes.data() + header_offset;
        io::write_be<uint32_t>(destination + 0x00, header.magic);
        io::write_be<uint32_t>(destination + 0x04, header.chunk_size);
        io::write_be<uint16_t>(destination + 0x08, header.payload_offset);
        io::write_be<uint16_t>(destination + 0x0A, header.padding);
        destination[0x0C] = header.channel_no;
        destination[0x0D] = header.reserved_0d;
        io::write_be<uint16_t>(destination + 0x0E, header.payload_type_and_flags);
        io::write_be<uint32_t>(destination + 0x10, header.frame_time);
        io::write_be<uint32_t>(destination + 0x14, header.frame_rate);
        io::write_be<uint32_t>(destination + 0x18, header.reserved_18);
        io::write_be<uint32_t>(destination + 0x1C, header.reserved_1c);
        bytes.insert(bytes.end(), payload.begin(), payload.end());
        if (padding.size() == header.padding) {
            bytes.insert(bytes.end(), padding.begin(), padding.end());
        } else {
            bytes.resize(bytes.size() + header.padding, 0);
        }
    }

    [[nodiscard]] std::vector<uint8_t> pack() const {
        std::vector<uint8_t> bytes;
        bytes.reserve(packed_size());
        append_to(bytes);
        return bytes;
    }
};

struct UsmStreamInfo {
    std::string filename;
    std::string filename_raw;
    UsmChunkType stream_id = UsmChunkType::CRID;
    uint8_t channel_no = 0;
    std::optional<UsmAudioCodec> audio_codec;
    uint32_t fmtver = 0;
    uint32_t filesize = 0;
    uint16_t minchk = 0;
    uint32_t minbuf = 0;
    uint32_t avbps = 0;

    [[nodiscard]] UsmStreamId id() const noexcept {
        return UsmStreamId{
            .stream_id = stream_id,
            .channel_no = channel_no,
        };
    }
};

struct UsmBuildInput {
    std::filesystem::path video_path;
    std::optional<std::filesystem::path> alpha_path;
    // Logical CRID stream filenames. Empty values derive the name from the
    // corresponding source path.
    std::string video_filename{};
    std::string alpha_filename{};
    text::EncodingOptions encoding;

    // Audio paths may contain ADX or HCA. Encryption is codec-aware: ADX uses
    // the USM audio mask, while plain HCA is converted to cipher type 56.
    // Already-encrypted HCA input is preserved rather than encrypted again.
    struct AudioTrack {
        std::filesystem::path path;
        std::optional<bool> encrypt;
        std::optional<uint8_t> channel_no;
        std::string filename{};
    };
    std::vector<AudioTrack> audio_tracks;

    struct SubtitleTrack {
        std::filesystem::path path;
        uint32_t language_id = 0;
        UsmSubtitleFormat format = UsmSubtitleFormat::Auto;
        std::optional<uint8_t> channel_no;
        std::string filename{};
    };
    std::vector<SubtitleTrack> subtitle_tracks;

    std::optional<bool> encrypt_audio;
    uint64_t key = 0;
};

struct UsmBuildPlan {
    struct AudioTrack {
        uint8_t channel_no = 0;
        UsmAudioCodec codec = UsmAudioCodec::Unknown;
        bool encrypt = false;
    };

    struct SubtitleTrack {
        uint8_t channel_no = 0;
    };

    std::vector<AudioTrack> audio_tracks;
    std::vector<SubtitleTrack> subtitle_tracks;
};

[[nodiscard]] std::expected<UsmBuildPlan, std::string> plan_build(const UsmBuildInput& input);

struct UsmPayloadView {
    std::string_view output_name;
    UsmStreamId stream_id;
    std::span<const uint8_t> payload;
};

struct UsmSubtitleCue {
    uint32_t language_id = 0;
    uint32_t time_unit = 1000;
    uint32_t start_time = 0;
    uint32_t duration = 0;
    std::string text;
    uint32_t terminator_size = 0;

    [[nodiscard]] uint32_t end_time() const noexcept {
        return start_time + duration;
    }
};

[[nodiscard]] std::expected<std::vector<UsmSubtitleCue>, std::string> parse_sbt_subtitles(
    std::span<const uint8_t> data
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build_sbt_subtitles(
    std::span<const UsmSubtitleCue> cues
);
[[nodiscard]] std::expected<std::string, std::string> sbt_to_subtitle_source_text(
    std::span<const uint8_t> data
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> subtitle_source_text_to_sbt(
    std::string_view text,
    uint32_t language_id = 0
);
[[nodiscard]] std::expected<std::string, std::string> sbt_to_srt(std::span<const uint8_t> data);
[[nodiscard]] std::expected<std::flat_map<uint32_t, std::string>, std::string> sbt_to_srt_tracks(
    std::span<const uint8_t> data
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> srt_to_sbt(
    std::string_view text,
    uint32_t language_id = 0,
    uint32_t time_unit = 1000
);
[[nodiscard]] std::expected<std::string, std::string> sbt_to_ass(
    std::span<const uint8_t> data,
    std::string_view title = "CriCodecs subtitles"
);
[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> ass_to_sbt(
    std::string_view text,
    uint32_t language_id = 0,
    uint32_t time_unit = 1000
);

class UsmReader {
public:
    UsmReader() = default;
    using AudioCodecMap = std::flat_map<UsmStreamId, UsmAudioCodec>;
    using OutputNameMap = std::flat_map<UsmStreamId, std::string>;

    std::expected<void, std::string> load(const std::filesystem::path& path);
    std::expected<void, std::string> load(std::span<const uint8_t> data);
    std::expected<void, std::string> load(std::vector<uint8_t>&& data);
    std::expected<std::map<std::string, std::vector<uint8_t>>, std::string> demux();
    template <class Callback>
    std::expected<void, std::string> visit_demuxed_payloads(Callback&& callback) const {
        auto&& callback_ref = callback;
        using CallbackRef = std::remove_reference_t<decltype(callback_ref)>;
        auto thunk = [](void* context, const UsmPayloadView& payload) -> std::expected<void, std::string> {
            auto& visitor = *static_cast<CallbackRef*>(context);
            if constexpr (std::is_void_v<std::invoke_result_t<CallbackRef&, const UsmPayloadView&>>) {
                std::invoke(visitor, payload);
                return {};
            } else {
                return std::invoke(visitor, payload);
            }
        };
        return visit_demuxed_payloads_impl(&callback_ref, thunk);
    }
    void set_key(uint64_t key) {
        if (key == 0) {
            m_crypto.clear_key();
        } else {
            m_crypto.init_key(key);
        }
    }
    void clear_key() noexcept { m_crypto.clear_key(); }
    void set_encoding(text::EncodingOptions options) {
        m_encoding = std::move(options);
        m_output_names_ready = false;
        m_output_names.clear();
        m_output_name_error.clear();
    }

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] std::string_view container_filename() const noexcept { return m_container_filename; }
    [[nodiscard]] const utf::UtfTable& crid_header() const noexcept { return m_crid_header; }
    [[nodiscard]] const std::optional<SfshHeader>& sfsh_header() const noexcept { return m_sfsh_header; }
    [[nodiscard]] const std::vector<UsmStreamInfo>& streams() const noexcept { return m_streams; }
    [[nodiscard]] const std::vector<UsmChunk>& chunks() const noexcept { return m_chunks; }
    [[nodiscard]] const UsmStreamInfo* find_stream(UsmStreamId id) const noexcept;
    [[nodiscard]] const UsmStreamInfo* find_stream(const UsmChunk& chunk) const noexcept {
        return find_stream(chunk.stream_id());
    }
    [[nodiscard]] std::string describe_stream(UsmStreamId id) const;
    [[nodiscard]] std::string describe_stream(const UsmChunk& chunk) const {
        return describe_stream(chunk.stream_id());
    }
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_stream(uint32_t index);
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> extract_stream_sample(
        uint32_t index,
        size_t max_bytes
    ) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> decrypt() const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encrypt() const;
    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path
    );
    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_dir);

private:
    using PayloadVisitor = std::expected<void, std::string> (*)(void*, const UsmPayloadView&);

    std::filesystem::path m_source_path;
    std::vector<uint8_t> m_owned_source;
    io::reader m_reader;
    std::string m_container_filename;
    utf::UtfTable m_crid_header;
    std::optional<SfshHeader> m_sfsh_header;
    std::vector<UsmStreamInfo> m_streams;
    std::vector<UsmChunk> m_chunks;
    AudioCodecMap m_audio_codecs;
    UsmCrypto m_crypto;
    text::EncodingOptions m_encoding;
    mutable OutputNameMap m_output_names;
    mutable std::string m_output_name_error;
    mutable bool m_output_names_ready = false;

    std::expected<void, std::string> parse_file();
    std::expected<void, std::string> parse_sfsh_file();
    [[nodiscard]] std::expected<const OutputNameMap*, std::string> output_name_map() const;
    void refresh_audio_codecs();
    [[nodiscard]] bool chunk_needs_masking(const UsmChunk& chunk) const;
    [[nodiscard]] std::vector<uint8_t> decrypt_chunk_payload(const UsmChunk& chunk) const;
    [[nodiscard]] std::span<const uint8_t> demuxed_payload(
        const UsmChunk& chunk,
        std::vector<uint8_t>& storage
    ) const;
    [[nodiscard]] std::expected<void, std::string> visit_demuxed_payloads_impl(
        void* context,
        PayloadVisitor visitor
    ) const;
    void append_stream_payloads(
        UsmStreamId id,
        std::vector<uint8_t>& output,
        size_t max_bytes = static_cast<size_t>(-1)
    ) const;
    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> transform_container(bool encrypt) const;
    [[nodiscard]] std::expected<void, std::string> write_stream_payloads(
        UsmStreamId id,
        const std::filesystem::path& output_path
    ) const;
};

class UsmBuilder {
public:
    UsmBuilder() = default;

    std::expected<std::vector<uint8_t>, std::string> build(const UsmBuildInput& input);
    std::expected<void, std::string> build_to_file(
        const std::filesystem::path& output_path,
        const UsmBuildInput& input
    );

private:
    UsmCrypto m_crypto;
};

} // namespace cricodecs::usm
