/**
 * @file usm_reader.cpp
 * @brief USM demuxer
 *
 * Keeps CRID metadata and chunk payloads inspectable while exposing decoded
 * stream payloads by channel.
 * The original chunk reader was ported from PyCriCodecsEx.
 */

#include "usm_container.hpp"

#include <algorithm>
#include <flat_set>
#include <limits>

namespace cricodecs::usm {

namespace {

std::string_view chunk_type_name(UsmChunkType type) {
    switch (type) {
    case UsmChunkType::CRID:
        return "crid";
    case UsmChunkType::SFSH:
        return "sfsh";
    case UsmChunkType::AHX:
        return "ahx";
    case UsmChunkType::ELM:
        return "elm";
    case UsmChunkType::ATP:
        return "atp";
    case UsmChunkType::PST:
        return "pst";
    case UsmChunkType::SFV:
        return "sfv";
    case UsmChunkType::SFA:
        return "sfa";
    case UsmChunkType::ALP:
        return "alp";
    case UsmChunkType::CUE:
        return "cue";
    case UsmChunkType::SBT:
        return "sbt";
    case UsmChunkType::STA:
        return "sta";
    case UsmChunkType::USR:
        return "usr";
    }
    return "stream";
}

std::string_view basename_of(std::string_view path) {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

std::string sanitize_output_name(std::string_view raw_name) {
    std::string sanitized;
    sanitized.reserve(raw_name.size());
    for (const unsigned char ch : raw_name) {
        if (ch == 0 || ch < 0x20 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
            ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
            sanitized.push_back('_');
        } else {
            sanitized.push_back(static_cast<char>(ch));
        }
    }
    return sanitized;
}

std::string fallback_stream_name(UsmStreamId id) {
    return std::string(chunk_type_name(id.stream_id)) + "_ch" + std::to_string(id.channel_no);
}

std::string dedupe_stream_name(std::string name, UsmStreamId id, const std::flat_set<std::string>& used_names) {
    if (name.empty()) {
        name = fallback_stream_name(id);
    }

    if (!used_names.contains(name)) {
        return name;
    }

    const auto dot = name.find_last_of('.');
    const std::string suffix = "." + std::string(chunk_type_name(id.stream_id)) + "_ch" + std::to_string(id.channel_no);
    const size_t suffix_offset = dot == std::string::npos ? name.size() : dot;
    std::string unique = name;
    unique.insert(suffix_offset, suffix);
    if (!used_names.contains(unique)) {
        return unique;
    }
    for (uint32_t copy_index = 2; ; ++copy_index) {
        std::string numbered = unique;
        numbered.insert(suffix_offset + suffix.size(), "_" + std::to_string(copy_index));
        if (!used_names.contains(numbered)) {
            return numbered;
        }
    }
}

std::expected<std::string, std::string> decode_bytes(std::string_view bytes, const text::EncodingOptions& encoding) {
    auto decoded = text::decode_to_utf8(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()),
        encoding
    );
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    return *decoded;
}

std::expected<UsmReader::OutputNameMap, std::string> build_output_name_map(
    std::span<const UsmStreamInfo> streams,
    const text::EncodingOptions& encoding
) {
    UsmReader::OutputNameMap output_names;
    std::flat_set<std::string> used_names;

    for (const auto& stream : streams) {
        auto decoded = decode_bytes(stream.filename_raw.empty() ? stream.filename : stream.filename_raw, encoding);
        if (!decoded) {
            return std::unexpected("USM filename decode failed: " + decoded.error());
        }
        auto stream_name = sanitize_output_name(basename_of(*decoded));
        stream_name = dedupe_stream_name(std::move(stream_name), stream.id(), used_names);
        used_names.insert(stream_name);
        output_names.emplace(stream.id(), std::move(stream_name));
    }

    return output_names;
}

std::expected<UsmChunkHeader, std::string> read_chunk_header(io::reader& reader) {
    if (reader.remaining() < UsmChunkHeader::raw_header_size) {
        return std::unexpected("USM parse failed: unexpected end of file while reading chunk header");
    }

    const auto header = UsmChunkHeader::read(reader.data().subspan(reader.tell()));
    reader.skip(UsmChunkHeader::raw_header_size);

    if (header.chunk_size < UsmChunkHeader::encoded_header_size) {
        return std::unexpected("USM chunk declares an invalid size");
    }
    if (header.payload_offset < UsmChunkHeader::encoded_header_size) {
        return std::unexpected("USM chunk declares an invalid payload offset");
    }

    return header;
}

std::expected<UsmChunk, std::string> read_chunk(io::reader& reader) {
    auto header = read_chunk_header(reader);
    if (!header) {
        return std::unexpected(header.error());
    }

    if (reader.remaining() < header->body_size()) {
        return std::unexpected("USM chunk body is truncated");
    }

    auto body = reader.read_bytes(header->body_size());
    if (header->data_offset() > body.size()) {
        return std::unexpected("USM chunk payload offset is out of range");
    }

    auto payload = body.subspan(header->data_offset());
    if (header->padding > payload.size()) {
        return std::unexpected("USM chunk padding exceeds payload size");
    }
    auto padding = payload.last(header->padding);
    payload = payload.first(payload.size() - header->padding);

    return UsmChunk{
        .header = *header,
        .payload = payload,
        .padding = padding,
    };
}

std::expected<SfshHeader, std::string> read_sfsh_header(std::span<const uint8_t> data) {
    if (data.size() < SfshHeader::raw_header_size) {
        return std::unexpected("USM SFSH parse failed: header is truncated");
    }
    if (io::read_be<uint32_t>(data.data()) != static_cast<uint32_t>(UsmChunkType::SFSH)) {
        return std::unexpected("USM SFSH parse failed: missing SFSH magic");
    }

    SfshHeader header;
    std::ranges::copy(data.first(SfshHeader::raw_header_size), header.raw.begin());
    header.version = io::read_le<uint16_t>(data.data() + 0x04);
    header.payload_size = io::read_le<uint32_t>(data.data() + 0x16);
    return header;
}

std::expected<UsmStreamInfo, std::string> make_stream_info(
    const utf::UtfTable& table,
    uint32_t row,
    const text::EncodingOptions& encoding
) {
    UsmStreamInfo info;
    if (auto filename = table.get_string(row, "filename"); filename) {
        info.filename_raw = std::string(*filename);
        if (auto decoded = decode_bytes(info.filename_raw, encoding); decoded) {
            info.filename = *decoded;
        } else {
            info.filename = info.filename_raw;
        }
    }
    if (auto stream_id = table.get<uint32_t>(row, "stmid"); stream_id) {
        info.stream_id = static_cast<UsmChunkType>(*stream_id);
    }
    if (auto channel_no = table.get<uint16_t>(row, "chno"); channel_no) {
        if (*channel_no > std::numeric_limits<uint8_t>::max()) {
            return std::unexpected(
                "USM parse failed: CRID stream channel exceeds the chunk-header field width"
            );
        }
        info.channel_no = static_cast<uint8_t>(*channel_no);
    }
    if (auto fmtver = table.get<uint32_t>(row, "fmtver"); fmtver) {
        info.fmtver = *fmtver;
    }
    if (auto filesize = table.get<uint32_t>(row, "filesize"); filesize) {
        info.filesize = *filesize;
    }
    if (auto minchk = table.get<uint16_t>(row, "minchk"); minchk) {
        info.minchk = *minchk;
    }
    if (auto minbuf = table.get<uint32_t>(row, "minbuf"); minbuf) {
        info.minbuf = *minbuf;
    }
    if (auto avbps = table.get<uint32_t>(row, "avbps"); avbps) {
        info.avbps = *avbps;
    }
    return info;
}

} // namespace

std::string_view audio_codec_name(uint8_t codec) noexcept {
    return audio_codec_name(static_cast<UsmAudioCodec>(codec));
}

std::expected<void, std::string> UsmReader::load(const std::filesystem::path& path) {
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("USM load failed: could not open input: " + std::string(source.error()));
    }
    m_source = std::move(*source);
    m_source_path = path;
    return parse_file();
}

std::expected<void, std::string> UsmReader::load(std::span<const uint8_t> data) {
    return load(std::vector<uint8_t>(data.begin(), data.end()));
}

std::expected<void, std::string> UsmReader::load(std::vector<uint8_t>&& data) {
    m_source = io::SourceView::from_owned(std::move(data));
    m_source_path.clear();
    return parse_file();
}

std::expected<void, std::string> UsmReader::parse_file() {
    m_container_filename.clear();
    m_crid_header = {};
    m_sfsh_header.reset();
    m_streams.clear();
    m_output_names.reset();
    m_chunks.clear();
    m_audio_codecs.clear();

    if (m_source.size() >= sizeof(uint32_t) &&
        io::read_be<uint32_t>(m_source.data()) == static_cast<uint32_t>(UsmChunkType::SFSH)) {
        return parse_sfsh_file();
    }

    io::reader source;
    static_cast<void>(source.open(m_source.bytes));
    while (source.remaining() >= UsmChunkHeader::raw_header_size) {
        auto chunk = read_chunk(source);
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        m_chunks.push_back(std::move(*chunk));
    }

    if (m_chunks.empty() || m_chunks.front().chunk_type() != UsmChunkType::CRID) {
        return std::unexpected("USM does not start with a CRID chunk");
    }
    if (!m_chunks.front().is_utf_payload()) {
        return std::unexpected("USM parse failed: CRID chunk payload is not a UTF table");
    }

    auto table = m_chunks.front().load_utf_payload();
    if (!table) {
        return std::unexpected("USM parse failed: could not parse CRID UTF: " + table.error());
    }
    m_crid_header = std::move(*table);
    if (auto container_filename = m_crid_header.get_string(0, "filename"); container_filename) {
        auto decoded = decode_bytes(*container_filename, m_encoding);
        if (!decoded) {
            return std::unexpected("USM container filename decode failed: " + decoded.error());
        }
        m_container_filename = std::string(basename_of(*decoded));
    }
    std::flat_set<UsmStreamId> stream_ids;
    for (uint32_t row = 1; row < m_crid_header.row_count(); ++row) {
        auto stream = make_stream_info(m_crid_header, row, m_encoding);
        if (!stream) {
            return std::unexpected(stream.error());
        }
        if (!stream_ids.insert(stream->id()).second) {
            return std::unexpected("USM parse failed: CRID contains a duplicate stream id and channel");
        }
        m_streams.push_back(std::move(*stream));
    }
    refresh_audio_codecs();

    return {};
}

std::expected<void, std::string> UsmReader::parse_sfsh_file() {
    const auto data = m_source.bytes;
    auto header = read_sfsh_header(data);
    if (!header) {
        return std::unexpected(header.error());
    }
    if (header->version != 1) {
        return std::unexpected(
            "USM SFSH parse failed: unsupported SFSH version " + std::to_string(header->version)
        );
    }
    if (header->payload_size > data.size() - SfshHeader::payload_offset_value) {
        return std::unexpected("USM SFSH parse failed: payload size exceeds file size");
    }

    const auto payload = data.subspan(SfshHeader::payload_offset_value, header->payload_size);
    m_sfsh_header = *header;
    m_streams.push_back(UsmStreamInfo{
        .filename = "sfsh_ch0",
        .filename_raw = "sfsh_ch0",
        .stream_id = UsmChunkType::SFSH,
        .channel_no = 0,
        .audio_codec = std::nullopt,
        .fmtver = header->version,
        .filesize = header->payload_size,
    });
    m_chunks.push_back(UsmChunk{
        .header = UsmChunkHeader{
            .magic = static_cast<uint32_t>(UsmChunkType::SFSH),
            .chunk_size = static_cast<uint32_t>(UsmChunkHeader::encoded_header_size + payload.size()),
            .payload_offset = UsmChunkHeader::encoded_header_size,
            .payload_type_and_flags = static_cast<uint16_t>(UsmPayloadType::Stream),
        },
        .payload = payload,
        .padding = {},
    });
    return {};
}

std::expected<const UsmReader::OutputNameMap*, std::string> UsmReader::output_name_map() const {
    if (!m_output_names) {
        m_output_names.emplace(build_output_name_map(m_streams, m_encoding));
    }
    if (!*m_output_names) {
        return std::unexpected(m_output_names->error());
    }
    return &m_output_names->value();
}

const UsmStreamInfo* UsmReader::find_stream(UsmStreamId id) const noexcept {
    const auto match = std::find_if(m_streams.begin(), m_streams.end(), [id](const UsmStreamInfo& stream) {
        return stream.id() == id;
    });
    return match == m_streams.end() ? nullptr : &*match;
}

std::string UsmReader::describe_stream(UsmStreamId id) const {
    if (id.stream_id == UsmChunkType::CRID) {
        return m_container_filename.empty() ? "root" : m_container_filename;
    }

    const auto output_names = output_name_map();
    if (!output_names) {
        return fallback_stream_name(id);
    }
    if (const auto output_name = (*output_names)->find(id); output_name != (*output_names)->end()) {
        return output_name->second;
    }

    return fallback_stream_name(id);
}

void UsmReader::refresh_audio_codecs() {
    m_audio_codecs.clear();
    for (const auto& chunk : m_chunks) {
        if (
            chunk.payload_type() != UsmPayloadType::Header ||
            (chunk.header.magic != static_cast<uint32_t>(UsmChunkType::SFA) &&
             chunk.header.magic != static_cast<uint32_t>(UsmChunkType::AHX)) ||
            !chunk.is_utf_payload()
        ) {
            continue;
        }

        auto table = chunk.load_utf_payload();
        if (!table) {
            continue;
        }
        if (auto codec = table->get<uint8_t>(0, "audio_codec"); codec) {
            const auto value = static_cast<UsmAudioCodec>(*codec);
            m_audio_codecs.insert_or_assign(chunk.stream_id(), value);
        }
    }

    for (auto& stream : m_streams) {
        if (const auto codec = m_audio_codecs.find(stream.id()); codec != m_audio_codecs.end()) {
            stream.audio_codec = codec->second;
        }
    }
}

bool UsmReader::chunk_needs_masking(const UsmChunk& chunk) const {
    if (!m_crypto.has_key()) {
        return false;
    }
    if (
        chunk.header.magic == static_cast<uint32_t>(UsmChunkType::SFV) ||
        chunk.header.magic == static_cast<uint32_t>(UsmChunkType::ALP)
    ) {
        return true;
    }
    if (chunk.header.magic != static_cast<uint32_t>(UsmChunkType::SFA)) {
        return false;
    }

    const auto codec = m_audio_codecs.find(chunk.stream_id());
    return codec != m_audio_codecs.end() && codec->second == UsmAudioCodec::Adx;
}

std::vector<uint8_t> UsmReader::decrypt_chunk_payload(const UsmChunk& chunk) const {
    std::vector<uint8_t> padded_payload(chunk.payload.begin(), chunk.payload.end());
    padded_payload.insert(padded_payload.end(), chunk.padding.begin(), chunk.padding.end());

    if (chunk.header.magic == static_cast<uint32_t>(UsmChunkType::SFA)) {
        m_crypto.decrypt_audio(padded_payload);
    } else {
        m_crypto.decrypt_video(padded_payload);
    }
    if (chunk.padding.size() > 0 && chunk.padding.size() <= padded_payload.size()) {
        padded_payload.resize(padded_payload.size() - chunk.padding.size());
    }
    return padded_payload;
}

std::span<const uint8_t> UsmReader::demuxed_payload(
    const UsmChunk& chunk,
    std::vector<uint8_t>& storage
) const {
    if (!chunk_needs_masking(chunk)) {
        return chunk.payload;
    }
    storage = decrypt_chunk_payload(chunk);
    return storage;
}

std::expected<void, std::string> UsmReader::visit_demuxed_payloads_impl(
    void* context,
    PayloadVisitor visitor
) const {
    auto output_names = output_name_map();
    if (!output_names) {
        return std::unexpected(output_names.error());
    }
    for (const auto& chunk : m_chunks) {
        if (chunk.payload_type() != UsmPayloadType::Stream) {
            continue;
        }

        const auto output_name = (*output_names)->find(chunk.stream_id());
        if (output_name == (*output_names)->end()) {
            continue;
        }

        std::vector<uint8_t> storage;
        const auto payload = demuxed_payload(chunk, storage);
        auto result = visitor(context, UsmPayloadView{
            .output_name = output_name->second,
            .stream_id = chunk.stream_id(),
            .payload = payload,
        });
        if (!result) {
            return std::unexpected(result.error());
        }
    }

    return {};
}

void UsmReader::append_stream_payloads(
    UsmStreamId id,
    std::vector<uint8_t>& output,
    size_t max_bytes
) const {
    if (max_bytes == 0) {
        return;
    }

    for (const auto& chunk : m_chunks) {
        if (chunk.stream_id() != id || chunk.payload_type() != UsmPayloadType::Stream) {
            continue;
        }

        std::vector<uint8_t> storage;
        const auto payload = demuxed_payload(chunk, storage);
        const auto count = std::min(payload.size(), max_bytes - output.size());
        output.insert(output.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(count));
        if (output.size() >= max_bytes) {
            break;
        }
    }
}

std::expected<void, std::string> UsmReader::write_stream_payloads(
    UsmStreamId id,
    const std::filesystem::path& output_path
) const {
    if (output_path.has_parent_path()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("USM extract failed: could not create export directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto result = writer.open(output_path); !result) {
        return std::unexpected("USM extract failed: could not open export output: " + output_path.string());
    }

    for (const auto& chunk : m_chunks) {
        if (chunk.stream_id() != id || chunk.payload_type() != UsmPayloadType::Stream) {
            continue;
        }

        std::vector<uint8_t> storage;
        if (auto result = writer.write(demuxed_payload(chunk, storage)); !result) {
            (void)writer.close();
            return std::unexpected("USM extract failed: could not write export output: " + output_path.string());
        }
    }

    if (auto result = writer.close(); !result) {
        return std::unexpected("USM extract failed: could not finalize export output: " + output_path.string());
    }
    return {};
}

std::expected<std::vector<uint8_t>, std::string> UsmReader::extract_stream(uint32_t index) {
    if (index >= m_streams.size()) {
        return std::unexpected("USM stream index is out of range");
    }

    std::vector<uint8_t> output;
    if (m_streams[index].filesize != 0) {
        output.reserve(m_streams[index].filesize);
    }
    append_stream_payloads(m_streams[index].id(), output);
    return output;
}

std::expected<std::vector<uint8_t>, std::string> UsmReader::extract_stream_sample(
    uint32_t index,
    size_t max_bytes
) const {
    if (index >= m_streams.size()) {
        return std::unexpected("USM stream index is out of range");
    }

    std::vector<uint8_t> output;
    const auto declared_size = static_cast<size_t>(m_streams[index].filesize);
    output.reserve(std::min(declared_size, max_bytes));
    append_stream_payloads(m_streams[index].id(), output, max_bytes);
    return output;
}

std::expected<std::vector<uint8_t>, std::string> UsmReader::decrypt() const {
    return transform_container(false);
}

std::expected<std::vector<uint8_t>, std::string> UsmReader::encrypt() const {
    return transform_container(true);
}

std::expected<std::vector<uint8_t>, std::string> UsmReader::transform_container(bool encrypt) const {
    if (!m_crypto.has_key()) {
        return std::unexpected("USM " + std::string(encrypt ? "encrypt" : "decrypt") + " requires a key");
    }

    const auto& audio_codecs = m_audio_codecs;

    const auto source = m_source.bytes;
    std::vector<uint8_t> output(source.begin(), source.end());
    for (auto chunk : chunk_views(std::span<uint8_t>(output.data(), output.size()))) {
        const auto& header = chunk.header();
        const bool should_transform =
            chunk.payload_type() == UsmPayloadType::Stream &&
            ((header.magic == static_cast<uint32_t>(UsmChunkType::SFV) ||
              header.magic == static_cast<uint32_t>(UsmChunkType::ALP)) ||
             (header.magic == static_cast<uint32_t>(UsmChunkType::SFA) &&
              [&]() {
                  const auto codec = audio_codecs.find(UsmStreamId{
                      .stream_id = UsmChunkType::SFA,
                      .channel_no = header.channel_no,
                  });
                  return codec != audio_codecs.end() &&
                      codec->second != UsmAudioCodec::Hca;
              }()));

        if (!should_transform) {
            continue;
        }

        auto payload = chunk.payload_with_padding();
        if (header.magic == static_cast<uint32_t>(UsmChunkType::SFA)) {
            if (encrypt) {
                m_crypto.encrypt_audio(payload);
            } else {
                m_crypto.decrypt_audio(payload);
            }
        } else if (encrypt) {
            m_crypto.encrypt_video(payload);
        } else {
            m_crypto.decrypt_video(payload);
        }
    }

    return output;
}

std::expected<void, std::string> UsmReader::extract_file(
    uint32_t index,
    const std::filesystem::path& output_path
) {
    if (index >= m_streams.size()) {
        return std::unexpected("USM stream index is out of range");
    }
    return write_stream_payloads(m_streams[index].id(), output_path);
}

std::expected<void, std::string> UsmReader::extract(const std::filesystem::path& output_dir) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("USM extract failed: could not create output directory: " + filesystem_error.message());
    }

    auto output_names = output_name_map();
    if (!output_names) {
        return std::unexpected(output_names.error());
    }
    for (const auto& stream : m_streams) {
        const auto output_name = (*output_names)->find(stream.id());
        const auto output_path = output_dir / (
            output_name == (*output_names)->end()
                ? fallback_stream_name(stream.id())
                : output_name->second
        );
        auto export_result = write_stream_payloads(stream.id(), output_path);
        if (!export_result) {
            return std::unexpected(export_result.error());
        }
    }

    return {};
}

std::expected<std::map<std::string, std::vector<uint8_t>>, std::string> UsmReader::demux() {
    std::map<std::string, std::vector<uint8_t>> output;
    auto output_names = output_name_map();
    if (!output_names) {
        return std::unexpected(output_names.error());
    }
    for (const auto& stream : m_streams) {
        const auto output_name = (*output_names)->find(stream.id());
        if (output_name == (*output_names)->end()) {
            continue;
        }
        auto [entry, inserted] = output.emplace(output_name->second, std::vector<uint8_t>{});
        if (inserted && stream.filesize != 0) {
            entry->second.reserve(stream.filesize);
        }
    }

    for (const auto& chunk : m_chunks) {
        switch (chunk.payload_type()) {
        case UsmPayloadType::Stream: {
            const auto output_name = (*output_names)->find(chunk.stream_id());
            if (output_name == (*output_names)->end()) {
                continue;
            }

            auto stream_bytes = output.find(output_name->second);
            if (stream_bytes == output.end()) {
                continue;
            }

            std::vector<uint8_t> storage;
            const auto payload = demuxed_payload(chunk, storage);
            stream_bytes->second.insert(stream_bytes->second.end(), payload.begin(), payload.end());
            break;
        }
        case UsmPayloadType::Header:
        case UsmPayloadType::Metadata:
        case UsmPayloadType::SectionEnd:
            break;
        }
    }

    return output;
}

} // namespace cricodecs::usm
