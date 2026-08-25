#pragma once
/**
 * @file awb_container.hpp
 * @brief AWB (AFS2/Atom Wave Bank) - CRI audio container
 *
 * Unified class for extraction, building, and editing of AWB containers.
 * AWB stores multiple audio files (HCA, ADX, etc.) with alignment.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../utilities/io.hpp"
#include "../utilities/numeric.hpp"
#include "awb_aac_encryption.hpp"
#include "awb_aac_key_recovery.hpp"
#include "awb_entry_codec.hpp"

namespace cricodecs::awb {

using io::read_le;
using io::write_le;
using io::read_le_n;
using io::write_le_n;
using util::align_up;

struct AwbEntry {
    uint64_t wave_id = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

struct AwbHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t offset_size;
    uint16_t id_size;
    uint32_t entry_count;
    uint16_t alignment;
    uint16_t subkey;
};

inline constexpr io::FourCC awb_magic{"AFS2"};

class AwbContainer {
public:
    static constexpr uint16_t DEFAULT_ALIGNMENT = 0x20;
    static constexpr uint8_t DEFAULT_VERSION = 2;
    static constexpr uint8_t DEFAULT_ID_SIZE = 2;
    static constexpr uint8_t DEFAULT_OFFSET_SIZE = 4;

    AwbContainer() = default;

    [[nodiscard]] static std::expected<AwbContainer, std::string> load(std::span<const uint8_t> data) {
        return load(std::vector<uint8_t>(data.begin(), data.end()));
    }

    [[nodiscard]] static std::expected<AwbContainer, std::string> load(std::vector<uint8_t>&& data) {
        AwbContainer awb;
        awb.m_source = io::SourceView::from_owned(std::move(data));
        return awb.parse_header().transform([&] { return std::move(awb); });
    }

    [[nodiscard]] static std::expected<AwbContainer, std::string> load(const std::filesystem::path& path) {
        auto source = io::SourceView::from_file(path);
        if (!source) {
            return std::unexpected("AWB load failed: failed to open " + path.string() + " (" + source.error() + ")");
        }
        AwbContainer awb;
        awb.m_source = std::move(*source);
        awb.m_source_path = path;
        return awb.parse_header().transform([&] { return std::move(awb); });
    }

    [[nodiscard]] uint32_t file_count() const noexcept { return static_cast<uint32_t>(m_entries.size()); }
    [[nodiscard]] uint8_t version() const noexcept { return m_version; }
    [[nodiscard]] uint8_t offset_size() const noexcept { return m_offset_size; }
    [[nodiscard]] uint8_t id_size() const noexcept { return m_id_size; }
    [[nodiscard]] uint16_t alignment() const noexcept { return m_alignment; }
    [[nodiscard]] uint16_t subkey() const noexcept { return m_subkey; }
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }

    [[nodiscard]] const AwbEntry& entry(uint32_t index) const { return m_entries[index]; }
    [[nodiscard]] const std::vector<AwbEntry>& entries() const noexcept { return m_entries; }

    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_data(uint32_t index) const {
        if (index >= m_entries.size()) {
            return std::unexpected("AWB file_data failed: file index out of range");
        }
        return file_payload(index, "file_data");
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> file_bytes(uint32_t index) const {
        return file_data(index).transform([](auto data) {
            return std::vector<uint8_t>(data.begin(), data.end());
        });
    }

    [[nodiscard]] std::expected<EntryCodec, std::string> entry_codec(uint32_t index) const {
        return file_data(index).transform(probe_entry_codec);
    }

    [[nodiscard]] std::expected<void, std::string> extract_file(
        uint32_t index,
        const std::filesystem::path& output_path) const {
        auto data = file_data(index);
        if (!data) {
            return std::unexpected(data.error());
        }

        std::error_code filesystem_error;
        if (const auto parent = output_path.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, filesystem_error);
            if (filesystem_error) {
                return std::unexpected("AWB extract failed: " + filesystem_error.message());
            }
        }

        return io::write_file_bytes(output_path, *data, "AWB extract failed");
    }

    [[nodiscard]] std::expected<std::filesystem::path, std::string> suggested_path(uint32_t index) const {
        if (index >= m_entries.size()) {
            return std::unexpected("AWB suggested_path failed: file index out of range");
        }
        auto codec = entry_codec(index);
        if (!codec) {
            return std::unexpected(codec.error());
        }
        auto name = std::to_string(index);
        if (name.size() < 5) name.insert(0, 5 - name.size(), '0');
        name += "_" + std::to_string(m_entries[index].wave_id);
        name += entry_codec_extension(*codec);
        return std::filesystem::path{name};
    }

    [[nodiscard]] std::expected<void, std::string> extract(const std::filesystem::path& output_dir) const {
        std::error_code filesystem_error;
        std::filesystem::create_directories(output_dir, filesystem_error);
        if (filesystem_error) {
            return std::unexpected("AWB extract failed: " + filesystem_error.message());
        }

        for (uint32_t index = 0; index < m_entries.size(); ++index) {
            auto suggested = suggested_path(index);
            if (!suggested) {
                return std::unexpected(suggested.error());
            }
            const auto output_path = output_dir / *suggested;
            auto result = extract_file(index, output_path);
            if (!result) {
                return result;
            }
        }

        return {};
    }

    [[nodiscard]] std::expected<AacEncryptionState, std::string> probe_aac_encryption(uint32_t index,
                                                                                       uint64_t keycode) const {
        return file_data(index).transform([&](auto data) {
            return ::cricodecs::awb::probe_aac_encryption(data, keycode);
        });
    }

    /// Return true when the bank contains a group that can be tested as CRI's
    /// encrypted M4A layout without treating unrelated AWB codecs as AAC.
    [[nodiscard]] bool has_aac_key_recovery_candidates() const {
        std::map<AacRecoverySignature, uint32_t> signatures;
        for (uint32_t index = 0; index < m_entries.size(); ++index) {
            auto data = file_data(index);
            const auto signature = data ? aac_recovery_signature(*data) : std::nullopt;
            if (!signature) {
                continue;
            }
            if (m_entries.size() == 1 || !signatures.emplace(*signature, index).second) {
                return true;
            }
        }
        return false;
    }

    /// Cheap encrypted-header grouping predicate used by frontends before they
    /// expose recovery for an individual otherwise-untyped AWB entry.
    [[nodiscard]] bool has_aac_key_recovery_candidate(uint32_t index) const {
        auto data = file_data(index);
        const auto signature = data ? aac_recovery_signature(*data) : std::nullopt;
        if (!signature) {
            return false;
        }
        if (m_entries.size() == 1) {
            return true;
        }
        for (uint32_t other = 0; other < m_entries.size(); ++other) {
            if (other == index) {
                continue;
            }
            auto other_data = file_data(other);
            if (other_data && aac_recovery_signature(*other_data) == signature) {
                return true;
            }
        }
        return false;
    }

    /// Recover one effective AAC key from encrypted-M4A entry groups. Repeated
    /// encrypted headers identify same-key AAC without feeding unrelated AWB
    /// codecs into the solver; a one-entry bank is tested directly.
    [[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_aac_key() const {
        std::map<AacRecoverySignature, std::vector<uint32_t>> grouped_indices;
        for (uint32_t index = 0; index < m_entries.size(); ++index) {
            auto data = file_data(index);
            const auto signature = data ? aac_recovery_signature(*data) : std::nullopt;
            if (!signature) {
                continue;
            }
            grouped_indices[*signature].push_back(index);
        }
        std::vector<std::vector<uint32_t>> groups;
        groups.reserve(grouped_indices.size());
        for (auto& [_, indices] : grouped_indices) {
            groups.push_back(std::move(indices));
        }

        std::ranges::sort(groups, {}, [](const auto& group) { return group.size(); });
        std::vector<KeyCandidate> candidates;
        size_t evidence_count = 0;
        for (auto group = groups.rbegin(); group != groups.rend(); ++group) {
            if (group->size() == 1u && m_entries.size() != 1u) {
                continue;
            }
            auto recovered = recover_aac_key(*group);
            if (!recovered) {
                continue;
            }
            evidence_count += recovered->evidence_count;
            for (const auto& candidate : recovered->candidates) {
                auto existing = std::ranges::find(candidates, candidate.key, &KeyCandidate::key);
                if (existing == candidates.end()) {
                    candidates.push_back(candidate);
                } else if (candidate.validated_sources > existing->validated_sources ||
                           (candidate.validated_sources == existing->validated_sources &&
                            candidate.score > existing->score)) {
                    *existing = candidate;
                }
            }
        }
        if (candidates.empty()) {
            return std::unexpected(
                "AWB AAC key recovery failed: bank contains no supported encrypted M4A entry group");
        }
        std::ranges::sort(candidates, [](const KeyCandidate& left, const KeyCandidate& right) {
            if (left.validated_sources != right.validated_sources) {
                return left.validated_sources > right.validated_sources;
            }
            if (left.score != right.score) return left.score > right.score;
            return left.key < right.key;
        });
        if (candidates.size() > MaxKeyRecoveryCandidates) candidates.resize(MaxKeyRecoveryCandidates);
        return KeyRecoveryResult{
            .candidates = std::move(candidates),
            .source_count = m_entries.size(),
            .evidence_count = evidence_count,
        };
    }

    /// Recover one effective AAC key from caller-selected same-key M4A entries.
    [[nodiscard]] std::expected<KeyRecoveryResult, std::string> recover_aac_key(
        std::span<const uint32_t> indices) const {
        if (indices.empty()) {
            return std::unexpected("AWB AAC key recovery failed: no entry indices were supplied");
        }

        std::vector<AacRecoverySource> sources;
        sources.reserve(indices.size());
        for (const uint32_t index : indices) {
            auto data = file_data(index);
            if (!data) {
                return std::unexpected(data.error());
            }
            sources.push_back(AacRecoverySource{*data});
        }
        return ::cricodecs::awb::recover_aac_key(sources);
    }

    [[nodiscard]] std::expected<uint64_t, std::string> wave_id(uint32_t index) const {
        if (index >= m_entries.size()) {
            return std::unexpected("AWB wave_id failed: file index out of range");
        }
        return m_entries[index].wave_id;
    }

    [[nodiscard]] std::optional<uint32_t> find_index_by_wave_id(uint64_t wave_id) const noexcept {
        const auto found = std::ranges::find(m_entries, wave_id, &AwbEntry::wave_id);
        if (found == m_entries.end()) return std::nullopt;
        return static_cast<uint32_t>(found - m_entries.begin());
    }

    [[nodiscard]] bool is_materialized() const noexcept {
        return !m_entries.empty() &&
               m_file_data.size() == m_entries.size() &&
               std::ranges::all_of(m_file_data, [](const auto& data) { return data.has_value(); });
    }

    [[nodiscard]] static AwbContainer create(uint8_t version = DEFAULT_VERSION,
                                             uint16_t alignment = DEFAULT_ALIGNMENT,
                                             uint16_t subkey = 0,
                                             uint8_t id_size = DEFAULT_ID_SIZE,
                                             uint8_t offset_size = DEFAULT_OFFSET_SIZE) {
        AwbContainer awb;
        awb.m_version = version;
        awb.m_alignment = alignment;
        awb.m_subkey = subkey;
        awb.m_id_size = id_size;
        awb.m_offset_size = offset_size;
        return awb;
    }

    uint64_t add_file(std::span<const uint8_t> data) {
        const uint64_t next_id = next_default_wave_id();
        add_file(data, next_id);
        return next_id;
    }

    void add_file(std::span<const uint8_t> data, uint64_t wave_id) {
        ensure_payload_slots();
        m_file_data.emplace_back(std::in_place, data.begin(), data.end());
        m_entries.push_back(AwbEntry{wave_id, 0, static_cast<uint64_t>(data.size())});
    }

    [[nodiscard]] std::expected<void, std::string> replace_file(uint32_t index, std::span<const uint8_t> data) {
        if (index >= m_entries.size()) {
            return std::unexpected("AWB replace_file failed: file index out of range");
        }
        ensure_payload_slots();
        m_file_data[index].emplace(data.begin(), data.end());
        m_entries[index].size = static_cast<uint64_t>(data.size());
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> remove_file(uint32_t index) {
        if (index >= m_entries.size()) {
            return std::unexpected("AWB remove_file failed: file index out of range");
        }
        ensure_payload_slots();
        m_file_data.erase(m_file_data.begin() + static_cast<std::ptrdiff_t>(index));
        m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(index));
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> move_file(uint32_t from_index, uint32_t to_index) {
        if (from_index >= m_entries.size() || to_index >= m_entries.size()) {
            return std::unexpected("AWB move_file failed: file index out of range");
        }
        if (from_index == to_index) {
            return {};
        }

        ensure_payload_slots();
        const auto from = static_cast<std::ptrdiff_t>(from_index);
        const auto to = static_cast<std::ptrdiff_t>(to_index);
        const auto move = [from, to](auto& values) {
            if (from < to) {
                std::ranges::rotate(values.begin() + from, values.begin() + from + 1, values.begin() + to + 1);
            } else {
                std::ranges::rotate(values.begin() + to, values.begin() + from, values.begin() + from + 1);
            }
        };
        move(m_entries);
        move(m_file_data);
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> set_wave_id(uint32_t index, uint64_t wave_id) {
        if (index >= m_entries.size()) {
            return std::unexpected("AWB set_wave_id failed: file index out of range");
        }
        m_entries[index].wave_id = wave_id;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> set_version(uint8_t version) {
        m_version = version;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> set_offset_size(uint8_t offset_size) {
        if (offset_size != 2 && offset_size != 4 && offset_size != 8) {
            return std::unexpected("AWB set_offset_size failed: unsupported offset size");
        }
        m_offset_size = offset_size;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> set_id_size(uint8_t id_size) {
        if (id_size != 1 && id_size != 2 && id_size != 4 && id_size != 8) {
            return std::unexpected("AWB set_id_size failed: unsupported ID size");
        }
        m_id_size = id_size;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> set_alignment(uint16_t alignment) {
        if (alignment == 0) {
            return std::unexpected("AWB set_alignment failed: alignment must be non-zero");
        }
        m_alignment = alignment;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> set_subkey(uint16_t subkey) {
        m_subkey = subkey;
        return {};
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> save() { return build(); }
    [[nodiscard]] std::expected<void, std::string> save_to_file(const std::filesystem::path& output_path) {
        return save().and_then([&](const auto& bytes) {
            return io::write_file_bytes(output_path, bytes, "AWB save failed");
        });
    }

    [[nodiscard]] std::expected<std::vector<uint8_t>, std::string> build() const {
        const uint32_t num_files = static_cast<uint32_t>(m_entries.size());
        if (num_files == 0) {
            return std::unexpected("AWB build failed: no files to build");
        }
        if (m_offset_size != 2 && m_offset_size != 4 && m_offset_size != 8) {
            return std::unexpected("AWB build failed: unsupported offset size");
        }
        if (m_id_size != 1 && m_id_size != 2 && m_id_size != 4 && m_id_size != 8) {
            return std::unexpected("AWB build failed: unsupported ID size");
        }

        std::vector<std::span<const uint8_t>> payloads;
        payloads.reserve(num_files);
        for (uint32_t i = 0; i < num_files; ++i) {
            auto payload = file_payload(i, "build");
            if (!payload) {
                return std::unexpected(payload.error());
            }
            payloads.push_back(*payload);
        }

        // AFS2 stores header tables as an unaligned byte blob; align offsets
        // using the file's alignment field before accessing payloads.
        const uint64_t header_raw = 16ull + (static_cast<uint64_t>(m_id_size) * num_files) +
                                    (static_cast<uint64_t>(m_offset_size) * (num_files + 1ull));
        std::vector<uint64_t> stored_offsets;
        stored_offsets.reserve(num_files + 1ull);
        stored_offsets.push_back(header_raw);

        for (const auto payload : payloads) {
            const uint64_t raw_end = align_up(stored_offsets.back(), m_alignment) + payload.size();
            stored_offsets.push_back(raw_end);
        }

        const uint64_t total_size = stored_offsets.back();
        if (total_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            return std::unexpected("AWB build failed: output size exceeds addressable memory");
        }

        std::vector<uint8_t> output(static_cast<size_t>(total_size), 0);

        write_le(output.data(), AwbHeader{
            .magic = awb_magic.le_value(),
            .version = m_version,
            .offset_size = m_offset_size,
            .id_size = m_id_size,
            .entry_count = num_files,
            .alignment = m_alignment,
            .subkey = m_subkey,
        });

        size_t pos = 16;
        for (uint32_t i = 0; i < num_files; ++i) {
            write_le_n<uint64_t>(output.data() + pos, m_entries[i].wave_id, m_id_size);
            pos += m_id_size;
        }

        for (uint32_t i = 0; i <= num_files; ++i) {
            write_le_n<uint64_t>(output.data() + pos, stored_offsets[i], m_offset_size);
            pos += m_offset_size;
        }

        for (uint32_t i = 0; i < num_files; ++i) {
            const auto offset = align_up(stored_offsets[i], m_alignment);
            std::memcpy(output.data() + offset, payloads[i].data(), payloads[i].size());
        }

        return output;
    }

    [[nodiscard]] std::expected<void, std::string> materialize() {
        if (is_materialized()) {
            return {};
        }
        ensure_payload_slots();

        for (uint32_t i = 0; i < m_entries.size(); ++i) {
            if (m_file_data[i]) {
                continue;
            }
            auto payload = file_payload(i, "materialize");
            if (!payload) {
                return std::unexpected(payload.error());
            }
            m_file_data[i].emplace(payload->begin(), payload->end());
        }
        return {};
    }

private:
    using AacRecoverySignature = std::array<uint8_t, 36>;

    [[nodiscard]] static std::optional<AacRecoverySignature> aac_recovery_signature(
        std::span<const uint8_t> data) {
        if (data.size() < 40u) {
            return std::nullopt;
        }
        const bool is_hca = (data[0] & 0x7Fu) == 'H' &&
            (data[1] & 0x7Fu) == 'C' && (data[2] & 0x7Fu) == 'A' && data[3] == 0;
        const bool is_adx_family = data[0] == 0x80u && data[1] == 0x00u;
        const bool is_clear_m4a = std::equal(data.begin() + 4, data.begin() + 8, "ftyp");
        if (is_hca || is_adx_family || is_clear_m4a) {
            return std::nullopt;
        }

        AacRecoverySignature signature{};
        std::copy_n(data.begin(), 32, signature.begin());
        std::copy_n(data.begin() + 36, 4, signature.begin() + 32);
        return signature;
    }

    io::SourceView m_source;
    std::filesystem::path m_source_path;

    uint8_t m_version = DEFAULT_VERSION;
    uint8_t m_offset_size = DEFAULT_OFFSET_SIZE;
    uint8_t m_id_size = DEFAULT_ID_SIZE;
    uint16_t m_alignment = DEFAULT_ALIGNMENT;
    uint16_t m_subkey = 0;

    std::vector<AwbEntry> m_entries;
    std::vector<std::optional<std::vector<uint8_t>>> m_file_data;

    [[nodiscard]] std::expected<void, std::string> parse_header() {
        if (m_source.size() < sizeof(AwbHeader)) {
            return std::unexpected("AWB parse failed: data is smaller than the header");
        }
        const auto header = read_le<AwbHeader>(m_source.data());
        if (header.magic != awb_magic.le_value()) {
            return std::unexpected("AWB parse failed: invalid magic, expected AFS2");
        }

        m_version = header.version;
        m_offset_size = header.offset_size;
        m_id_size = static_cast<uint8_t>(header.id_size);
        const uint32_t count = header.entry_count;
        m_alignment = header.alignment;
        m_subkey = header.subkey;

        if (m_offset_size != 2 && m_offset_size != 4 && m_offset_size != 8) {
            return std::unexpected("AWB parse failed: unsupported offset size");
        }
        if (m_id_size != 1 && m_id_size != 2 && m_id_size != 4 && m_id_size != 8) {
            return std::unexpected("AWB parse failed: unsupported ID size");
        }
        if (m_alignment == 0) {
            return std::unexpected("AWB parse failed: alignment is zero");
        }

        size_t pos = sizeof(AwbHeader);
        const size_t needed = pos + (static_cast<size_t>(m_id_size) * count) +
                             (static_cast<size_t>(m_offset_size) * (count + 1ull));
        if (m_source.size() < needed) {
            return std::unexpected("AWB parse failed: header tables exceed source size");
        }

        m_entries.clear();
        m_file_data.clear();
        m_entries.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            m_entries[i].wave_id = read_le_n<uint64_t>(m_source.data() + pos, m_id_size);
            pos += m_id_size;
        }

        const auto* offsets = m_source.data() + pos;
        for (uint32_t i = 0; i < count; ++i) {
            // Parse side follows the same AFS2 rule: align table offsets to the
            // stored m_alignment value before payload access.
            const uint64_t actual_offset = align_up(
                read_le_n<uint64_t>(offsets + static_cast<size_t>(i) * m_offset_size, m_offset_size),
                m_alignment
            );
            const uint64_t raw_end = read_le_n<uint64_t>(
                offsets + static_cast<size_t>(i + 1) * m_offset_size,
                m_offset_size
            );
            if (raw_end < actual_offset || raw_end > m_source.size()) {
                return std::unexpected("AWB parse failed: file entry offset/size is out of range");
            }

            m_entries[i].offset = actual_offset;
            m_entries[i].size = raw_end - actual_offset;
        }

        return {};
    }

    [[nodiscard]] uint64_t next_default_wave_id() const noexcept {
        uint64_t next_id = 0;
        for (const auto& entry : m_entries) {
            if (entry.wave_id >= next_id) {
                next_id = entry.wave_id + 1;
            }
        }
        return next_id;
    }

    void ensure_payload_slots() {
        if (m_file_data.size() < m_entries.size()) {
            m_file_data.resize(m_entries.size());
        }
    }

    [[nodiscard]] std::expected<std::span<const uint8_t>, std::string> file_payload(uint32_t index,
                                                                                    std::string_view operation) const {
        if (index < m_file_data.size() && m_file_data[index]) {
            return std::span<const uint8_t>(*m_file_data[index]);
        }

        if (m_source.empty()) {
            return std::unexpected("AWB " + std::string(operation) + " failed: source data is empty");
        }
        const auto& e = m_entries[index];
        if (e.offset > m_source.size() || e.size > m_source.size() - e.offset) {
            return std::unexpected("AWB " + std::string(operation) + " failed: entry offset/size is out of range");
        }
        return m_source.subspan(static_cast<size_t>(e.offset), static_cast<size_t>(e.size));
    }

};

} // namespace cricodecs::awb
