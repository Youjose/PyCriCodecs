/**
 * @file acb_container.cpp
 * @brief ACB container object helpers.
 *
 * ACB/AWB traversal follows vgmstream, PyCriCodecsEx, and Cri Atom Craft.
 * I added the inspectable C++23 object model used here.
 */

#include "acb_container.hpp"

namespace cricodecs::acb {

namespace {

constexpr uint16_t invalid_wave_id = 0xFFFF;

} // namespace

std::string_view AcbContainer::find_name(uint16_t wave_id) const {
    auto it = m_name_map.find(wave_id);
    if (it != m_name_map.end()) {
        return it->second;
    }
    return {};
}

std::string_view AcbContainer::waveform_name(uint32_t index) const {
    if (index >= m_waveform_names.size()) {
        return {};
    }
    return m_waveform_names[index];
}

std::string_view AcbContainer::waveform_name_raw(uint32_t index) const {
    if (index >= m_waveform_names_raw.size()) {
        return {};
    }
    return m_waveform_names_raw[index];
}

std::string AcbContainer::waveform_filename(uint32_t index, bool include_index_prefix) const {
    if (index >= m_waveforms.size()) {
        return {};
    }

    std::string result;
    if (include_index_prefix) {
        result = std::to_string(index + 1);
        result += "_";
    }

    if (auto resolved_name = waveform_name(index); !resolved_name.empty()) {
        result += resolved_name;
    } else {
        result += "wave_";
        result += std::to_string(index + 1);
    }

    const auto codec = waveform_codec(index).value_or(awb::EntryCodec::Unknown);
    result += awb::entry_codec_extension(codec);
    return result;
}

std::string_view AcbContainer::name() const {
    if (auto name = m_header.get_string(0, "Name")) {
        return *name;
    }
    return m_header.table_name();
}

uint16_t AcbContainer::waveform_id_for_bank(const WaveformInfo& waveform, bool is_memory_bank) noexcept {
    if (is_memory_bank) {
        if (waveform.memory_awb_id != invalid_wave_id) {
            return waveform.memory_awb_id;
        }
        if (waveform.id != invalid_wave_id) {
            return waveform.id;
        }
        return waveform.stream_awb_id;
    }

    if (waveform.stream_awb_id != invalid_wave_id) {
        return waveform.stream_awb_id;
    }
    if (waveform.id != invalid_wave_id) {
        return waveform.id;
    }
    return waveform.memory_awb_id;
}

bool AcbContainer::prefers_memory_bank(const WaveformInfo& waveform) noexcept {
    return waveform.streaming == 0 || waveform.stream_awb_id == invalid_wave_id;
}

bool AcbContainer::uses_memory_bank_for_associated_awb(const WaveformInfo& waveform) const {
    if (has_embedded_awb()) {
        return waveform.streaming != 1;
    }
    return prefers_memory_bank(waveform);
}

} // namespace cricodecs::acb
