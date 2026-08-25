/**
 * @file acb_builder.cpp
 * @brief ACB save/build helpers.
 *
 * The ACB builder follows vgmstream, PyCriCodecsEx, and Cri Atom Craft.
 * I added the build-side structure and validation used here.
 */

#include "acb_container.hpp"

#include "../utilities/io.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace cricodecs::acb {

namespace {

constexpr uint16_t invalid_wave_id = 0xFFFF;

} // namespace

std::optional<std::span<const uint8_t>> AcbContainer::embedded_awb() const {
    auto data = m_header.get_data(0, "AwbFile");
    if (!data || data->empty()) {
        return std::nullopt;
    }
    return *data;
}

bool AcbContainer::has_embedded_awb() const {
    return embedded_awb().has_value();
}

std::optional<std::filesystem::path> AcbContainer::companion_awb_path() const {
    return stream_awb_path(0);
}

std::optional<std::filesystem::path> AcbContainer::stream_awb_path(uint16_t port_no) const {
    if (m_source_path.empty()) {
        return std::nullopt;
    }

    std::vector<std::string> names;
    if (const auto slot = std::ranges::find(m_stream_awb_slots, port_no, &AcbStreamAwbSlot::port_no);
        slot != m_stream_awb_slots.end() && !slot->name.empty()) {
        names.push_back(slot->name);
    }
    if (port_no == 0) {
        names.push_back(m_source_path.stem().string());
        names.emplace_back(name());
    }

    for (const auto& awb_name : names) {
        auto filename = std::filesystem::path(awb_name).filename();
        if (filename.extension() != ".awb") {
            filename += ".awb";
        }
        auto candidate = m_source_path.parent_path() / filename;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::expected<awb::AwbContainer, std::string> AcbContainer::load_memory_awb() const {
    if (auto data = embedded_awb()) {
        auto loaded = awb::AwbContainer::load(*data);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        return std::move(*loaded);
    }
    return std::unexpected("ACB memory AWB load failed: the cue sheet has no embedded AWB data");
}

std::expected<awb::AwbContainer, std::string> AcbContainer::load_stream_awb(uint16_t port_no) const {
    auto companion = stream_awb_path(port_no);
    if (!companion) {
        return std::unexpected(
            "ACB stream AWB load failed: no companion file was found for port " +
            std::to_string(port_no));
    }

    auto loaded = awb::AwbContainer::load(*companion);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return std::move(*loaded);
}

std::expected<awb::AwbContainer, std::string> AcbContainer::load_awb() const {
    for (const auto& waveform : m_waveforms) {
        if (waveform_bank(waveform) == AcbAwbBank::stream) {
            if (auto path = stream_awb_path(waveform_stream_port(waveform)); path) {
                return load_stream_awb(waveform_stream_port(waveform));
            }
        }
    }
    if (has_embedded_awb()) {
        return load_memory_awb();
    }
    if (auto path = companion_awb_path(); path) {
        return load_stream_awb();
    }
    return std::unexpected("ACB load_awb failed: no embedded AWB data or companion AWB file was found");
}

std::expected<uint16_t, std::string> AcbContainer::awb_subkey() const {
    for (const auto& waveform : m_waveforms) {
        if (waveform_bank(waveform) != AcbAwbBank::stream) {
            continue;
        }
        const auto port_no = waveform_stream_port(waveform);
        if (!stream_awb_path(port_no)) {
            continue;
        }
        auto awb = awb_for_bank(AcbAwbBank::stream, port_no);
        if (!awb) {
            return std::unexpected(awb.error());
        }
        return awb->get().subkey();
    }
    if (has_embedded_awb()) {
        auto awb = awb_for_bank(AcbAwbBank::memory);
        if (!awb) {
            return std::unexpected(awb.error());
        }
        return awb->get().subkey();
    }
    if (companion_awb_path()) {
        auto awb = awb_for_bank(AcbAwbBank::stream);
        if (!awb) {
            return std::unexpected(awb.error());
        }
        return awb->get().subkey();
    }
    return std::unexpected("ACB AWB subkey failed: no associated AWB was found");
}

std::expected<uint16_t, std::string> AcbContainer::waveform_awb_subkey(uint32_t index) const {
    auto awb = awb_for_waveform(index);
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return awb->get().subkey();
}

std::expected<std::reference_wrapper<const awb::AwbContainer>, std::string> AcbContainer::awb_for_bank(
    AcbAwbBank bank,
    uint16_t port_no) const {
    if (bank == AcbAwbBank::memory) {
        if (!m_memory_awb) {
            auto loaded = load_memory_awb();
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            m_memory_awb.emplace(std::move(*loaded));
        }
        return std::cref(*m_memory_awb);
    }

    if (const auto cached = m_stream_awbs.find(port_no); cached != m_stream_awbs.end()) {
        return std::cref(cached->second);
    }
    auto loaded = load_stream_awb(port_no);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    auto [cached, inserted] = m_stream_awbs.emplace(port_no, std::move(*loaded));
    static_cast<void>(inserted);
    return std::cref(cached->second);
}

std::expected<std::reference_wrapper<const awb::AwbContainer>, std::string> AcbContainer::awb_for_waveform(
    uint32_t index,
    bool prefer_stream_bank) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform AWB load failed: waveform index is out of range");
    }
    const auto& waveform = m_waveforms[index];
    const auto bank = prefer_stream_bank ? AcbAwbBank::stream : waveform_bank(waveform);
    return awb_for_bank(
        bank,
        bank == AcbAwbBank::stream ? waveform_stream_port(waveform) : 0);
}

std::expected<awb::EntryCodec, std::string> AcbContainer::waveform_codec(uint32_t index) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform codec failed: waveform index is out of range");
    }
    if (const auto codec = encode_type_codec(m_waveforms[index].encode_type)) {
        return *codec;
    }

    auto awb = awb_for_waveform(index);
    if (!awb) {
        return std::unexpected("ACB waveform codec fallback failed: " + awb.error());
    }
    auto payload = waveform_data_from_awb(index, awb->get(), waveform_bank(m_waveforms[index]));
    if (!payload) {
        return std::unexpected("ACB waveform codec fallback failed: " + payload.error());
    }
    return awb::probe_entry_codec(*payload);
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::waveform_awb_entry(
    uint32_t index,
    bool prefer_stream_bank) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform AWB resolution failed: waveform index is out of range");
    }
    const auto bank = prefer_stream_bank ? AcbAwbBank::stream : waveform_bank(m_waveforms[index]);
    return waveform_awb_entry(index, bank);
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::waveform_awb_entry(
    uint32_t index,
    AcbAwbBank bank) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform AWB resolution failed: waveform index is out of range");
    }
    const auto& waveform = m_waveforms[index];
    auto awb = awb_for_bank(
        bank,
        bank == AcbAwbBank::stream ? waveform_stream_port(waveform) : 0);
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return waveform_awb_entry(index, awb->get(), bank);
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::waveform_awb_entry(
    uint32_t index,
    const awb::AwbContainer& awb,
    bool prefer_stream_bank) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform AWB resolution failed: waveform index is out of range");
    }
    return waveform_awb_entry(
        index,
        awb,
        prefer_stream_bank ? AcbAwbBank::stream : waveform_bank(m_waveforms[index]));
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::waveform_awb_entry(
    uint32_t index,
    const awb::AwbContainer& awb,
    AcbAwbBank bank) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform AWB resolution failed: waveform index is out of range");
    }

    const auto& waveform = m_waveforms[index];
    const bool memory_bank = bank == AcbAwbBank::memory;
    const uint16_t wave_id = waveform_id_for_bank(waveform, memory_bank);
    if (wave_id == invalid_wave_id) {
        return std::unexpected("ACB waveform AWB resolution failed: waveform does not have a usable AWB ID");
    }

    const auto awb_index = awb.find_index_by_wave_id(wave_id);
    if (!awb_index) {
        return std::unexpected(
            "ACB waveform AWB resolution failed: waveform AWB ID was not found in the supplied bank (ID " +
            std::to_string(wave_id) + ")");
    }
    return WaveformAwbEntry{
        .waveform_index = index,
        .wave_id = wave_id,
        .awb_index = *awb_index,
        .port_no = memory_bank ? invalid_wave_id : waveform_stream_port(waveform),
        .stream_bank = !memory_bank,
    };
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::replace_waveform_data(
    uint32_t index,
    awb::AwbContainer& awb,
    std::span<const uint8_t> data,
    bool prefer_stream_bank) const {
    auto resolved = waveform_awb_entry(index, awb, prefer_stream_bank);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    if (auto replaced = awb.replace_file(resolved->awb_index, data); !replaced) {
        return std::unexpected("ACB waveform replacement failed: " + replaced.error());
    }
    return *resolved;
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::replace_waveform_file(
    uint32_t index,
    awb::AwbContainer& awb,
    const std::filesystem::path& input_path,
    bool prefer_stream_bank) const {
    auto data = io::read_file_bytes(input_path, "ACB waveform replacement failed");
    if (!data) {
        return std::unexpected(data.error());
    }
    return replace_waveform_data(index, awb, *data, prefer_stream_bank);
}

std::expected<awb::AacEncryptionState, std::string> AcbContainer::probe_waveform_aac_encryption(
    uint32_t index,
    uint64_t keycode) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform index is out of range");
    }

    const auto& waveform = m_waveforms[index];
    if (waveform.encode_type != 19) {
        return std::unexpected("ACB waveform AAC probe failed: waveform is not AAC/M4A (EncodeType != 19)");
    }

    auto awb = awb_for_waveform(index);
    if (!awb) {
        return std::unexpected(awb.error());
    }

    const auto& awb_ref = awb->get();
    auto resolved = waveform_awb_entry(index, awb_ref, waveform_bank(waveform));
    if (!resolved) {
        return std::unexpected("ACB waveform AAC probe failed: " + resolved.error());
    }

    auto state = awb_ref.probe_aac_encryption(resolved->awb_index, keycode);
    if (!state) {
        return std::unexpected(state.error());
    }

    return *state;
}

bool AcbContainer::has_aac_waveforms() const noexcept {
    return std::ranges::any_of(m_waveforms, [](const WaveformInfo& waveform) {
        return waveform.encode_type == 19;
    });
}

std::expected<awb::KeyRecoveryResult, std::string> AcbContainer::recover_aac_key() const {
    struct RecoveryGroup {
        AcbAwbBank bank = AcbAwbBank::memory;
        uint16_t port_no = 0;
        std::vector<uint32_t> indices;
    };
    std::vector<RecoveryGroup> groups;
    for (uint32_t waveform_index = 0; waveform_index < m_waveforms.size(); ++waveform_index) {
        const auto& waveform = m_waveforms[waveform_index];
        if (waveform.encode_type != 19) {
            continue;
        }
        const auto bank = waveform_bank(waveform);
        const auto port_no = bank == AcbAwbBank::stream
            ? waveform_stream_port(waveform)
            : uint16_t{0};
        auto awb = awb_for_bank(bank, port_no);
        if (!awb) {
            continue;
        }
        auto resolved = waveform_awb_entry(waveform_index, awb->get(), bank);
        if (!resolved) {
            continue;
        }
        auto group = std::ranges::find_if(groups, [&](const RecoveryGroup& candidate) {
            return candidate.bank == bank && candidate.port_no == port_no;
        });
        if (group == groups.end()) {
            groups.push_back({.bank = bank, .port_no = port_no, .indices = {}});
            group = std::prev(groups.end());
        }
        if (std::ranges::find(group->indices, resolved->awb_index) == group->indices.end()) {
            group->indices.push_back(resolved->awb_index);
        }
    }
    if (groups.empty()) {
        return std::unexpected("ACB AAC key recovery failed: cue sheet contains no AAC/M4A waveforms");
    }

    std::string errors;
    for (const auto& group : groups) {
        auto awb = awb_for_bank(group.bank, group.port_no);
        if (!awb) {
            continue;
        }
        if (auto recovered = awb->get().recover_aac_key(group.indices)) {
            return *recovered;
        } else {
            if (!errors.empty()) errors += "; ";
            errors += recovered.error();
        }
    }
    return std::unexpected("ACB AAC key recovery failed: " + errors);
}

std::expected<std::vector<uint8_t>, std::string> AcbContainer::extract_waveform_data(
    uint32_t index,
    uint64_t aac_keycode) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform extract failed: waveform index is out of range");
    }
    auto awb = awb_for_waveform(index);
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return extract_waveform_data_from_awb(
        index, awb->get(), aac_keycode, waveform_bank(m_waveforms[index]));
}

std::expected<std::vector<uint8_t>, std::string> AcbContainer::extract_waveform_stream_data(
    uint32_t index,
    uint64_t aac_keycode) const {
    auto awb = awb_for_waveform(index, true);
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return extract_waveform_data_from_awb(index, awb->get(), aac_keycode, AcbAwbBank::stream);
}

std::expected<std::span<const uint8_t>, std::string> AcbContainer::waveform_data_from_awb(
    uint32_t index,
    const awb::AwbContainer& awb,
    AcbAwbBank bank) const {
    auto resolved = waveform_awb_entry(index, awb, bank);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }

    auto data = awb.file_data(resolved->awb_index);
    if (!data) {
        return std::unexpected(data.error());
    }

    return *data;
}

std::expected<std::vector<uint8_t>, std::string> AcbContainer::extract_waveform_data_from_awb(
    uint32_t index,
    const awb::AwbContainer& awb,
    uint64_t aac_keycode,
    AcbAwbBank bank) const {
    auto data = waveform_data_from_awb(index, awb, bank);
    if (!data) {
        return std::unexpected(data.error());
    }

    const auto& waveform = m_waveforms[index];
    if (waveform.encode_type != 19 || aac_keycode == 0) {
        return std::vector<uint8_t>(data->begin(), data->end());
    }

    switch (awb::probe_aac_encryption(*data, aac_keycode)) {
        case awb::AacEncryptionState::Clear:
            return std::vector<uint8_t>(data->begin(), data->end());
        case awb::AacEncryptionState::Encrypted:
            return awb::decrypt_aac(*data, aac_keycode);
        case awb::AacEncryptionState::Indeterminate:
        default:
            return std::unexpected("ACB waveform extract failed: AAC payload did not match a clear or decryptable M4A header with the provided key");
    }
}

std::expected<void, std::string> AcbContainer::extract_file(
    uint32_t index,
    const std::filesystem::path& output_path,
    uint64_t aac_keycode) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform extract failed: waveform index is out of range");
    }
    const auto bank = waveform_bank(m_waveforms[index]);
    auto awb = awb_for_waveform(index);
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return extract_file_from_awb(index, awb->get(), bank, output_path, aac_keycode);
}

std::expected<void, std::string> AcbContainer::extract_file_from_awb(
    uint32_t index,
    const awb::AwbContainer& awb,
    AcbAwbBank bank,
    const std::filesystem::path& output_path,
    uint64_t aac_keycode) const {
    auto data = waveform_data_from_awb(index, awb, bank);
    if (!data) {
        return std::unexpected(data.error());
    }

    std::vector<uint8_t> decrypted;
    std::span<const uint8_t> output = *data;
    const auto& waveform = m_waveforms[index];
    if (waveform.encode_type == 19 && aac_keycode != 0) {
        switch (awb::probe_aac_encryption(*data, aac_keycode)) {
            case awb::AacEncryptionState::Clear:
                break;
            case awb::AacEncryptionState::Encrypted: {
                decrypted = awb::decrypt_aac(*data, aac_keycode);
                output = std::span<const uint8_t>(decrypted);
                break;
            }
            case awb::AacEncryptionState::Indeterminate:
            default:
                return std::unexpected("ACB waveform extract failed: AAC payload did not match a clear or decryptable M4A header with the provided key");
        }
    }

    std::error_code filesystem_error;
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path(), filesystem_error);
        if (filesystem_error) {
            return std::unexpected("ACB extract failed: could not create output directory: " + filesystem_error.message());
        }
    }

    io::writer writer;
    if (auto open_result = writer.open(output_path); !open_result) {
        return std::unexpected("ACB extract failed: could not open output: " + output_path.string());
    }

    if (auto write_result = writer.write(output); !write_result) {
        (void)writer.close();
        return std::unexpected("ACB extract failed: could not write output: " + output_path.string());
    }
    if (auto close_result = writer.close(); !close_result) {
        return std::unexpected("ACB extract failed: could not finalize output: " + output_path.string());
    }

    return {};
}

std::expected<void, std::string> AcbContainer::extract(
    const std::filesystem::path& output_dir,
    uint64_t aac_keycode) const {
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_dir, filesystem_error);
    if (filesystem_error) {
        return std::unexpected("ACB extract failed: could not create output directory: " + filesystem_error.message());
    }

    std::unordered_map<std::string, uint32_t> filename_counts;
    filename_counts.reserve(waveform_count());
    for (uint32_t index = 0; index < waveform_count(); ++index) {
        ++filename_counts[waveform_filename(index)];
    }

    for (uint32_t index = 0; index < waveform_count(); ++index) {
        const auto unprefixed_name = waveform_filename(index);
        const bool needs_prefix = filename_counts[unprefixed_name] > 1;
        auto extracted = extract_file(
            index, output_dir / waveform_filename(index, needs_prefix), aac_keycode);
        if (!extracted) {
            return std::unexpected(extracted.error());
        }
    }

    return {};
}

} // namespace cricodecs::acb
