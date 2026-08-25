/**
 * @file acb_builder.cpp
 * @brief ACB save/build helpers.
 *
 * The ACB builder follows vgmstream, PyCriCodecsEx, and Cri Atom Craft.
 * I added the build-side structure and validation used here.
 */

#include "acb_container.hpp"

#include "../utilities/io.hpp"

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
    if (has_embedded_awb() || m_source_path.empty()) {
        return std::nullopt;
    }

    auto candidate = m_source_path.parent_path() / (std::string(name()) + ".awb");
    return std::filesystem::exists(candidate) ? std::optional(candidate) : std::nullopt;
}

std::expected<awb::AwbContainer, std::string> AcbContainer::load_awb() const {
    if (auto data = embedded_awb()) {
        auto loaded = awb::AwbContainer::load(*data);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        return std::move(*loaded);
    }

    auto companion = companion_awb_path();
    if (!companion) {
        return std::unexpected("ACB load_awb failed: no embedded AWB data or companion AWB file was found");
    }

    auto loaded = awb::AwbContainer::load(*companion);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return std::move(*loaded);
}

std::expected<uint16_t, std::string> AcbContainer::awb_subkey() const {
    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return awb->get().subkey();
}

std::expected<std::reference_wrapper<const awb::AwbContainer>, std::string> AcbContainer::associated_awb() const {
    if (!m_associated_awb) {
        auto loaded = load_awb();
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        m_associated_awb.emplace(std::move(*loaded));
    }

    return std::cref(*m_associated_awb);
}

std::expected<awb::EntryCodec, std::string> AcbContainer::waveform_codec(uint32_t index) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform codec failed: waveform index is out of range");
    }
    if (const auto codec = encode_type_codec(m_waveforms[index].encode_type)) {
        return *codec;
    }

    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected("ACB waveform codec fallback failed: " + awb.error());
    }
    auto payload = waveform_data_from_awb(index, awb->get());
    if (!payload) {
        return std::unexpected("ACB waveform codec fallback failed: " + payload.error());
    }
    return awb::probe_entry_codec(*payload);
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::waveform_awb_entry(
    uint32_t index,
    bool prefer_stream_bank) const {
    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return waveform_awb_entry(index, awb->get(), prefer_stream_bank);
}

std::expected<WaveformAwbEntry, std::string> AcbContainer::waveform_awb_entry(
    uint32_t index,
    const awb::AwbContainer& awb,
    bool prefer_stream_bank) const {
    if (index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform AWB resolution failed: waveform index is out of range");
    }

    const auto& waveform = m_waveforms[index];
    const bool stream_bank =
        prefer_stream_bank && waveform.streaming != 0 && waveform.stream_awb_id != invalid_wave_id;
    const bool memory_bank = stream_bank ? false : uses_memory_bank_for_associated_awb(waveform);
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

    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }

    const bool is_memory_bank = uses_memory_bank_for_associated_awb(waveform);
    const uint16_t wave_id = waveform_id_for_bank(waveform, is_memory_bank);
    if (wave_id == invalid_wave_id) {
        return std::unexpected("ACB waveform AAC probe failed: waveform does not have a usable AWB ID");
    }

    const auto& awb_ref = awb->get();
    auto awb_index = awb_ref.find_index_by_wave_id(wave_id);
    if (!awb_index) {
        return std::unexpected("ACB waveform AAC probe failed: waveform AWB ID was not found in the associated AWB");
    }

    auto state = awb_ref.probe_aac_encryption(*awb_index, keycode);
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
    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }

    std::vector<uint32_t> indices;
    for (const auto& waveform : m_waveforms) {
        if (waveform.encode_type != 19) {
            continue;
        }
        const bool is_memory_bank = uses_memory_bank_for_associated_awb(waveform);
        const uint16_t wave_id = waveform_id_for_bank(waveform, is_memory_bank);
        if (wave_id == invalid_wave_id) {
            continue;
        }
        const auto awb_index = awb->get().find_index_by_wave_id(wave_id);
        if (awb_index && std::ranges::find(indices, *awb_index) == indices.end()) {
            indices.push_back(*awb_index);
        }
    }
    if (indices.empty()) {
        return std::unexpected("ACB AAC key recovery failed: cue sheet contains no AAC/M4A waveforms");
    }

    auto recovered = awb->get().recover_aac_key(indices);
    if (!recovered) {
        return std::unexpected("ACB AAC key recovery failed: " + recovered.error());
    }
    return *recovered;
}

std::expected<std::vector<uint8_t>, std::string> AcbContainer::extract_waveform_data(
    uint32_t index,
    uint64_t aac_keycode) const {
    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return extract_waveform_data_from_awb(index, awb->get(), aac_keycode);
}

std::expected<std::vector<uint8_t>, std::string> AcbContainer::extract_waveform_stream_data(
    uint32_t index,
    uint64_t aac_keycode) const {
    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return extract_waveform_data_from_awb(index, awb->get(), aac_keycode, true);
}

std::expected<std::span<const uint8_t>, std::string> AcbContainer::waveform_data_from_awb(
    uint32_t index,
    const awb::AwbContainer& awb,
    bool prefer_stream_bank) const {
    auto resolved = waveform_awb_entry(index, awb, prefer_stream_bank);
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
    bool prefer_stream_bank) const {
    auto data = waveform_data_from_awb(index, awb, prefer_stream_bank);
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
    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }
    return extract_file_from_awb(index, awb->get(), output_path, aac_keycode);
}

std::expected<void, std::string> AcbContainer::extract_file_from_awb(
    uint32_t index,
    const awb::AwbContainer& awb,
    const std::filesystem::path& output_path,
    uint64_t aac_keycode) const {
    auto data = waveform_data_from_awb(index, awb);
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

    auto awb = associated_awb();
    if (!awb) {
        return std::unexpected(awb.error());
    }

    std::unordered_map<std::string, uint32_t> filename_counts;
    filename_counts.reserve(waveform_count());
    for (uint32_t index = 0; index < waveform_count(); ++index) {
        ++filename_counts[waveform_filename(index)];
    }

    for (uint32_t index = 0; index < waveform_count(); ++index) {
        const auto unprefixed_name = waveform_filename(index);
        const bool needs_prefix = filename_counts[unprefixed_name] > 1;
        auto extracted = extract_file_from_awb(index, awb->get(), output_dir / waveform_filename(index, needs_prefix), aac_keycode);
        if (!extracted) {
            return std::unexpected(extracted.error());
        }
    }

    return {};
}

} // namespace cricodecs::acb
