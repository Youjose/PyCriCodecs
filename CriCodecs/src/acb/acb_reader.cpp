/**
 * @file acb_reader.cpp
 * @brief ACB loading and cue-name resolution implementation
 *
 * Follows vgmstream's chain: CueName -> Cue -> Synth/Sequence/BlockSequence -> ... -> Waveform
 */

#include "acb_container.hpp"

#include <initializer_list>
#include "../utilities/io.hpp"
#include "../utilities/text_encoding.hpp"

namespace cricodecs::acb {

using utf::UtfTable;

namespace {

[[nodiscard]] bool has_any_column(const UtfTable& table, std::initializer_list<std::string_view> names) {
    for (const auto name : names) {
        if (table.find_column(name) >= 0) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool looks_like_acb_header(const UtfTable& header) {
    if (header.table_name() != "Header" || header.row_count() != 1) {
        return false;
    }
    if (header.find_column("WaveformTable") < 0) {
        return false;
    }
    if (!has_any_column(header, {"CueNameTable", "CueTable", "AwbFile"})) {
        return false;
    }

    auto waveform_data = header.get_data(0, "WaveformTable");
    if (!waveform_data || waveform_data->empty()) {
        return false;
    }

    auto waveform_table = UtfTable::load(*waveform_data);
    if (!waveform_table || waveform_table->row_count() == 0) {
        return false;
    }

    const bool has_identity = has_any_column(*waveform_table, {"Id", "MemoryAwbId", "StreamAwbId"});
    const bool has_audio_shape = has_any_column(*waveform_table, {"EncodeType", "Streaming", "LoopFlag"});
    return has_identity && has_audio_shape;
}

} // namespace

std::optional<std::reference_wrapper<const UtfTable>> AcbContainer::load_subtable(
    const SubtableDescriptor& descriptor
) const {
    auto& out = m_sub.*descriptor.table;
    if (out.has_value()) {
        return std::cref(*out);
    }

    auto data_result = m_header.get_data(0, descriptor.name);
    if (!data_result || data_result->empty()) {
        return std::nullopt;
    }

    m_sub.table_data.emplace_back(data_result->begin(), data_result->end());
    auto& owned = m_sub.table_data.back();

    auto table = UtfTable::load(std::span<const uint8_t>(owned));
    if (!table) {
        m_sub.table_data.pop_back();
        return std::nullopt;
    }

    out = std::move(*table);
    return std::cref(*out);
}

std::optional<std::reference_wrapper<const UtfTable>> AcbContainer::load_subtable(std::string_view name) const {
    for (const auto& descriptor : subtable_descriptors()) {
        if (name == descriptor.name) {
            return load_subtable(descriptor);
        }
    }
    return std::nullopt;
}

std::vector<AcbSubtableInfo> AcbContainer::subtable_info() const {
    auto make_info = [this](const SubtableDescriptor& descriptor) {
        AcbSubtableInfo info;
        info.name = descriptor.name;
        if (auto data = m_header.get_data(0, descriptor.name); data && !data->empty()) {
            info.present = true;
            info.data_size = static_cast<uint32_t>(data->size());
            if (auto table = load_subtable(descriptor)) {
                info.row_count = table->get().row_count();
                info.column_count = table->get().column_count();
            }
        }
        return info;
    };

    std::vector<AcbSubtableInfo> info;
    info.reserve(10);
    for (const auto& descriptor : subtable_descriptors()) {
        info.push_back(make_info(descriptor));
    }
    return info;
}

std::optional<std::reference_wrapper<const UtfTable>> AcbContainer::subtable(std::string_view name) const {
    return load_subtable(name);
}

std::expected<AcbContainer, std::string> AcbContainer::load(
    std::span<const uint8_t> data,
    const text::EncodingOptions& encoding
) {
    return load_source(
        io::SourceView::from_copy(data),
        encoding);
}

std::expected<AcbContainer, std::string> AcbContainer::load(
    std::vector<uint8_t>&& data,
    const text::EncodingOptions& encoding
) {
    return load_source(io::SourceView::from_owned(std::move(data)), encoding);
}

std::expected<AcbContainer, std::string> AcbContainer::load_source(
    io::SourceView source,
    const text::EncodingOptions& encoding
) {
    AcbContainer acb;
    acb.m_encoding = encoding;
    acb.m_source = std::move(source);

    if (auto result = acb.finish_load_from_source(); !result) {
        return std::unexpected(result.error());
    }
    return acb;
}

std::expected<AcbContainer, std::string> AcbContainer::load(
    const std::filesystem::path& path,
    const text::EncodingOptions& encoding
) {
    auto source = io::SourceView::from_file(path);
    if (!source) {
        return std::unexpected("ACB load failed: " + path.string() + " (" + source.error() + ")");
    }
    return load_source(std::move(*source), encoding)
        .transform([&](AcbContainer acb) {
            acb.m_source_path = path;
            return acb;
        });
}

std::expected<void, std::string> AcbContainer::finish_load_from_source() {
    auto header = UtfTable::load(m_source.bytes);
    if (!header) {
        return std::unexpected("ACB load failed: " + header.error());
    }

    m_header = std::move(*header);
    if (m_header.row_count() == 0) {
        return std::unexpected("ACB load failed: header table has no rows");
    }
    if (!looks_like_acb_header(m_header)) {
        return std::unexpected("ACB load failed: UTF table is not an ACB header");
    }
    preload_waveforms();

    auto graph = AcbCueGraph::load(m_source.bytes, m_encoding);
    if (!graph) {
        return std::unexpected("ACB cue graph load failed: " + graph.error());
    }
    m_cue_graph = std::move(*graph);

    auto views = m_cue_graph.waveform_cue_views();
    if (!views) {
        return std::unexpected("ACB cue graph view failed: " + views.error());
    }
    m_waveform_cue_views = std::move(*views);
    resolve_all_names();
    return {};
}

bool AcbContainer::preload_waveforms() {
    if (!load_subtable("WaveformTable")) {
        return false;
    }

    auto& wt = *m_sub.waveform_table;
    const uint32_t rows = wt.row_count();
    m_waveforms.resize(rows);

    const int c_Id = wt.find_column("Id");
    const int c_MemoryAwbId = wt.find_column("MemoryAwbId");
    const int c_StreamAwbId = wt.find_column("StreamAwbId");
    const int c_StreamAwbPortNo = wt.find_column("StreamAwbPortNo");
    const int c_Streaming = wt.find_column("Streaming");
    const int c_EncodeType = wt.find_column("EncodeType");
    const int c_LoopFlag = wt.find_column("LoopFlag");
    const int c_ExtensionData = wt.find_column("ExtensionData");

    for (uint32_t i = 0; i < rows; ++i) {
        auto& waveform = m_waveforms[i];

        if (c_Streaming >= 0) {
            if (auto value = wt.get<uint8_t>(i, static_cast<uint32_t>(c_Streaming))) {
                waveform.streaming = *value;
            }
        }

        if (c_Id >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_Id))) {
                waveform.id = *value;
                waveform.memory_awb_id = *value;
            }
        }

        if (c_MemoryAwbId >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_MemoryAwbId))) {
                waveform.memory_awb_id = *value;
            }
        }

        if (c_StreamAwbId >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_StreamAwbId))) {
                waveform.stream_awb_id = *value;
            }
        }

        if (c_StreamAwbPortNo >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_StreamAwbPortNo))) {
                waveform.port_no = *value;
            } else {
                waveform.port_no = 0;
            }
        }

        if (c_EncodeType >= 0) {
            if (auto value = wt.get<uint8_t>(i, static_cast<uint32_t>(c_EncodeType))) {
                waveform.encode_type = *value;
            }
        }

        if (c_LoopFlag >= 0) {
            if (auto value = wt.get<uint8_t>(i, static_cast<uint32_t>(c_LoopFlag))) {
                waveform.loop_flag = *value == 2;
            }
        }

        if (c_ExtensionData >= 0) {
            if (auto value = wt.get<uint16_t>(i, static_cast<uint32_t>(c_ExtensionData))) {
                waveform.extension_data = *value == 0xFFFF
                    ? -1
                    : static_cast<int32_t>(*value);
            }
        }

        const bool is_memory_bank = uses_memory_bank_for_associated_awb(waveform);
        waveform.id = waveform_id_for_bank(waveform, is_memory_bank);
    }

    return true;
}

void AcbContainer::resolve_all_names() {
    m_wave_names.clear();
    m_name_map.clear();
    if (m_waveform_cue_views.size() != m_waveforms.size()) {
        return;
    }
    const auto& cue_names = m_cue_graph.cue_names();
    m_waveform_names.assign(m_waveforms.size(), {});
    m_waveform_names_raw.assign(m_waveforms.size(), {});
    const auto append_name = [](std::string& name, const std::string& part) {
        if (part.empty()) return;
        if (!name.empty()) name += "; ";
        name += part;
    };
    for (const auto& view : m_waveform_cue_views) {
        for (const auto name_row : view.preferred_cue_name_rows) {
            if (name_row >= cue_names.size()) continue;
            append_name(m_waveform_names[view.waveform_index], cue_names[name_row].name);
            append_name(m_waveform_names_raw[view.waveform_index], cue_names[name_row].name_raw);
        }
    }

    for (uint32_t waveform_index = 0; waveform_index < m_waveforms.size(); ++waveform_index) {
        const auto& waveform = m_waveforms[waveform_index];
        const bool is_memory_target = uses_memory_bank_for_associated_awb(waveform);
        const uint16_t wave_id = waveform_id_for_bank(waveform, is_memory_target);

        auto resolved = waveform_name(waveform_index);
        if (resolved.empty()) {
            continue;
        }

        auto it = m_name_map.find(wave_id);
        if (it == m_name_map.end()) {
            m_name_map.emplace(wave_id, std::string(resolved));
        } else if (it->second.find(resolved) == std::string::npos) {
            it->second += "; ";
            it->second += resolved;
        }

        m_wave_names.push_back(WaveNameInfo{
            .waveform_index = waveform_index,
            .wave_id = wave_id,
            .port_no = waveform.port_no,
            .name = std::string(resolved),
            .name_raw = std::string(waveform_name_raw(waveform_index)),
            .streaming = waveform.streaming,
            .encode_type = waveform.encode_type,
        });
    }
}

} // namespace cricodecs::acb
