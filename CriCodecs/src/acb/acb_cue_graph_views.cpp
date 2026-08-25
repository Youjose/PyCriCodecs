/**
 * @file acb_cue_graph_views.cpp
 * @brief Derived exact-waveform and flat AWB-asset views of an ACB cue graph.
 */

#include "acb_cue_graph.hpp"

#include "../utilities/flat_unordered_map.hpp"

#include <algorithm>
#include <limits>

namespace cricodecs::acb {

namespace {

uint16_t waveform_id_for_bank(const AcbCueWaveform& waveform, bool memory) noexcept {
    if (memory) {
        if (waveform.memory_awb_id != invalid_acb_index) return waveform.memory_awb_id;
        if (waveform.id != invalid_acb_index) return waveform.id;
        return waveform.stream_awb_id;
    }
    if (waveform.stream_awb_id != invalid_acb_index) return waveform.stream_awb_id;
    if (waveform.id != invalid_acb_index) return waveform.id;
    return waveform.memory_awb_id;
}

bool uses_memory_bank(const AcbCueWaveform& waveform) noexcept {
    return waveform.streaming == 0;
}

bool matches_bank(const AcbCueWaveform& waveform, bool memory) noexcept {
    return memory ? waveform.streaming != 1 : waveform.streaming != 0;
}

using WaveformDistance = std::pair<uint32_t, uint32_t>;

std::vector<WaveformDistance> playback_waveform_distances(const AcbCueAssembly& assembly) {
    const auto node_key = [](AcbCueNode node) {
        return (static_cast<uint64_t>(node.kind) << 32) | node.index;
    };
    util::flat_unordered_map<uint64_t, uint32_t> node_lookup;
    node_lookup.reserve(assembly.nodes.size());
    for (uint32_t position = 0; position < assembly.nodes.size(); ++position) {
        node_lookup.emplace(node_key(assembly.nodes[position]), position);
    }
    const auto node_position = [&](AcbCueNode node) {
        return node_lookup.find(node_key(node))->second;
    };

    std::vector<uint32_t> outgoing_counts(assembly.nodes.size(), 0);
    size_t playback_edge_count = 0;
    for (const auto& edge : assembly.edges) {
        if (edge.kind == AcbCueEdgeKind::action_track) {
            continue;
        }
        ++outgoing_counts[node_position(edge.from)];
        ++playback_edge_count;
    }
    std::vector<uint32_t> offsets(assembly.nodes.size() + 1, 0);
    for (size_t node = 0; node < outgoing_counts.size(); ++node) {
        offsets[node + 1] = offsets[node] + outgoing_counts[node];
    }
    auto write_offsets = offsets;
    std::vector<uint32_t> adjacency(playback_edge_count);
    for (const auto& edge : assembly.edges) {
        if (edge.kind == AcbCueEdgeKind::action_track) {
            continue;
        }
        const auto from = node_position(edge.from);
        adjacency[write_offsets[from]++] = node_position(edge.to);
    }

    constexpr uint32_t unreachable = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> distance(assembly.nodes.size(), unreachable);
    std::vector<uint32_t> pending;
    pending.reserve(assembly.nodes.size());
    const auto cue_position = node_position({AcbCueNodeKind::cue, assembly.cue_index});
    distance[cue_position] = 0;
    pending.push_back(cue_position);
    for (size_t head = 0; head < pending.size(); ++head) {
        const auto from = pending[head];
        const auto next_distance = distance[from] + 1;
        for (uint32_t edge = offsets[from]; edge < offsets[from + 1]; ++edge) {
            const auto to = adjacency[edge];
            if (distance[to] != unreachable) {
                continue;
            }
            distance[to] = next_distance;
            pending.push_back(to);
        }
    }

    std::vector<WaveformDistance> rows;
    for (uint32_t position = 0; position < assembly.nodes.size(); ++position) {
        if (assembly.nodes[position].kind == AcbCueNodeKind::waveform &&
            distance[position] != unreachable) {
            rows.emplace_back(assembly.nodes[position].index, distance[position]);
        }
    }
    std::ranges::sort(rows, {}, &WaveformDistance::first);
    return rows;
}

uint32_t asset_key(bool memory, uint16_t id) noexcept {
    return (static_cast<uint32_t>(memory) << 16) | id;
}

uint64_t port_asset_key(uint32_t key, uint16_t port) noexcept {
    return (static_cast<uint64_t>(key) << 16) | port;
}

void sort_unique(std::vector<uint32_t>& rows) {
    std::ranges::sort(rows);
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
}

const std::vector<uint32_t>& selected_name_rows(
    const AcbWaveformCueView& waveform,
    AcbWaveformNameView view
) noexcept {
    switch (view) {
        case AcbWaveformNameView::preferred:
            return waveform.preferred_cue_name_rows;
        case AcbWaveformNameView::exact_waveform:
            return waveform.exact_cue_name_rows;
        case AcbWaveformNameView::associated_awb_asset:
            return waveform.associated_awb_cue_name_rows;
    }
    return waveform.preferred_cue_name_rows;
}

} // namespace

std::expected<std::vector<AcbWaveformCueView>, std::string>
AcbCueGraph::waveform_cue_views() const {
    std::vector<AcbWaveformCueView> views(m_waveforms.size());
    for (uint32_t index = 0; index < views.size(); ++index) {
        views[index].waveform_index = index;
    }

    std::vector<std::optional<std::vector<WaveformDistance>>> cue_waveforms(m_cues.size());
    std::vector<uint32_t> preferred_distance(
        m_waveforms.size(), std::numeric_limits<uint32_t>::max());
    for (uint32_t name_row = 0; name_row < m_cue_names.size(); ++name_row) {
        const auto cue_index = m_cue_names[name_row].cue_index;
        if (cue_index >= m_cues.size()) {
            continue;
        }
        if (!cue_waveforms[cue_index]) {
            auto assembly = assemble_cue(cue_index);
            if (!assembly) {
                return std::unexpected(assembly.error());
            }
            cue_waveforms[cue_index] = playback_waveform_distances(*assembly);
        }

        for (const auto [waveform_index, distance] : *cue_waveforms[cue_index]) {
            if (waveform_index < views.size()) {
                views[waveform_index].exact_cue_name_rows.push_back(name_row);
                if (distance < preferred_distance[waveform_index]) {
                    preferred_distance[waveform_index] = distance;
                    views[waveform_index].preferred_cue_name_rows.clear();
                }
                if (distance == preferred_distance[waveform_index]) {
                    views[waveform_index].preferred_cue_name_rows.push_back(name_row);
                }
            }
        }
    }

    util::flat_unordered_map<uint32_t, std::vector<uint32_t>> asset_name_rows;
    util::flat_unordered_map<uint64_t, std::vector<uint32_t>> port_asset_name_rows;
    asset_name_rows.reserve(m_waveforms.size() * 2);
    port_asset_name_rows.reserve(m_waveforms.size());
    for (const auto& reached_view : views) {
        const auto& waveform = m_waveforms[reached_view.waveform_index];
        for (const bool memory : {false, true}) {
            if (!matches_bank(waveform, memory)) {
                continue;
            }
            const auto key = asset_key(memory, waveform_id_for_bank(waveform, memory));
            auto& all_rows = asset_name_rows[key];
            all_rows.insert(
                all_rows.end(),
                reached_view.exact_cue_name_rows.begin(),
                reached_view.exact_cue_name_rows.end());
            if (!memory) {
                auto& port_rows =
                    port_asset_name_rows[port_asset_key(key, waveform.stream_awb_port_no)];
                port_rows.insert(
                    port_rows.end(),
                    reached_view.exact_cue_name_rows.begin(),
                    reached_view.exact_cue_name_rows.end());
            }
        }
    }
    for (auto& entry : asset_name_rows) {
        sort_unique(entry.second);
    }
    for (auto& entry : port_asset_name_rows) {
        sort_unique(entry.second);
    }

    for (auto& target_view : views) {
        const auto& target = m_waveforms[target_view.waveform_index];
        const bool memory = uses_memory_bank(target);
        const auto key = asset_key(memory, waveform_id_for_bank(target, memory));
        if (memory || target.stream_awb_port_no == invalid_acb_index) {
            if (const auto rows = asset_name_rows.find(key); rows != asset_name_rows.end()) {
                target_view.associated_awb_cue_name_rows = rows->second;
            }
            continue;
        }

        const auto append_port_rows = [&](uint16_t port) {
            const auto rows = port_asset_name_rows.find(port_asset_key(key, port));
            if (rows != port_asset_name_rows.end()) {
                target_view.associated_awb_cue_name_rows.insert(
                    target_view.associated_awb_cue_name_rows.end(),
                    rows->second.begin(),
                    rows->second.end());
            }
        };
        append_port_rows(invalid_acb_index);
        append_port_rows(target.stream_awb_port_no);
        sort_unique(target_view.associated_awb_cue_name_rows);
    }
    return views;
}

std::expected<std::vector<std::string>, std::string> AcbCueGraph::waveform_names(
    AcbWaveformNameView view,
    bool raw
) const {
    auto views = waveform_cue_views();
    if (!views) {
        return std::unexpected(views.error());
    }

    std::vector<std::string> names;
    names.reserve(views->size());
    for (const auto& waveform : *views) {
        std::string result;
        for (const auto row : selected_name_rows(waveform, view)) {
            if (row >= m_cue_names.size()) {
                continue;
            }
            const auto& name = raw ? m_cue_names[row].name_raw : m_cue_names[row].name;
            if (name.empty()) {
                continue;
            }
            if (!result.empty()) {
                result += "; ";
            }
            result += name;
        }
        names.push_back(std::move(result));
    }
    return names;
}

std::expected<std::string, std::string> AcbCueGraph::waveform_name(
    uint32_t waveform_index,
    AcbWaveformNameView view,
    bool raw
) const {
    if (waveform_index >= m_waveforms.size()) {
        return std::unexpected("ACB waveform name failed: waveform index is out of range");
    }
    auto names = waveform_names(view, raw);
    if (!names) {
        return std::unexpected(names.error());
    }
    return std::move((*names)[waveform_index]);
}

} // namespace cricodecs::acb
