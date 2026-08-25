#include "binding_helpers.hpp"

#include <filesystem>
#include <map>
#include <set>

#include <nanobind/stl/map.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/acb/acb_container.hpp"
#include "../../CriCodecs/src/acb/acb_cue_renderer.hpp"
#include "../../CriCodecs/src/acb/acb_cue_resolver.hpp"
#include "../../CriCodecs/src/wav/wav_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] const cricodecs::acb::AcbContainer& checked_container(
    const cricodecs::acb::AcbContainer& self,
    uint32_t index) {
    if (index >= self.waveform_count()) {
        raise_value_error("ACB waveform index is out of range");
    }
    return self;
}

[[nodiscard]] cricodecs::acb::AcbContainer load_acb_any(const nb::object& source, nb::object encoding) {
    if (auto path = python_text_path(source)) {
        return unwrap_expected(cricodecs::acb::AcbContainer::load(
            std::filesystem::path(*path),
            python_encoding_options_from_object(encoding)
        ));
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    return unwrap_expected(cricodecs::acb::AcbContainer::load(
        std::move(owned),
        python_encoding_options_from_object(encoding)
    ));
}

[[nodiscard]] nb::object waveform_info_object(
    const cricodecs::acb::AcbContainer& self,
    uint32_t index) {
    const auto& waveform_info = self.waveform(index);
    nb::object waveform = simple_namespace();
    waveform.attr("index") = index;
    waveform.attr("name") = std::string(self.waveform_name(index));
    waveform.attr("name_raw") = string_to_python_bytes(self.waveform_name_raw(index));
    waveform.attr("filename") = self.waveform_filename(index, true);
    waveform.attr("id") = waveform_info.id;
    waveform.attr("memory_awb_id") = waveform_info.memory_awb_id;
    waveform.attr("stream_awb_id") = waveform_info.stream_awb_id;
    waveform.attr("port_no") = waveform_info.port_no;
    waveform.attr("streaming") = waveform_info.streaming;
    waveform.attr("encode_type") = waveform_info.encode_type;
    waveform.attr("loop_flag") = waveform_info.loop_flag;
    waveform.attr("extension_data") = waveform_info.extension_data;
    return waveform;
}

[[nodiscard]] cricodecs::acb::AcbCueRenderOptions cue_options(
    uint32_t loop_count,
    bool advance_after_infinite,
    uint64_t hca_keycode,
    const nb::object& hca_subkey,
    bool include_empty_holds,
    const nb::object& block_loop_counts) {
    cricodecs::acb::AcbCueRenderOptions options{
        .infinite_block_loop_count = loop_count,
        .advance_after_infinite_block = advance_after_infinite,
        .include_empty_infinite_blocks = include_empty_holds,
        .hca_keycode = hca_keycode,
        .hca_subkey = hca_subkey.is_none()
            ? std::nullopt
            : std::optional<uint16_t>(nb::cast<uint16_t>(hca_subkey)),
    };
    if (!block_loop_counts.is_none()) {
        const auto values =
            nb::cast<std::map<uint32_t, uint32_t>>(block_loop_counts);
        options.block_loop_overrides.reserve(values.size());
        for (const auto& [block_position, block_loop_count] : values) {
            options.block_loop_overrides.push_back({
                .block_position = block_position,
                .loop_count = block_loop_count,
            });
        }
    }
    return options;
}

[[nodiscard]] std::vector<cricodecs::acb::AcbCueSelectorValue>
selector_values_from_object(const nb::object& selectors) {
    if (selectors.is_none()) {
        return {};
    }
    const auto values =
        nb::cast<std::map<std::string, std::string>>(selectors);
    std::vector<cricodecs::acb::AcbCueSelectorValue> result;
    result.reserve(values.size());
    for (const auto& [name, value] : values) {
        result.push_back({.name = name, .value = value});
    }
    return result;
}

[[nodiscard]] cricodecs::acb::AcbCueSheetResolveOptions resolve_options(
    uint32_t loop_count,
    bool advance_after_infinite,
    bool include_empty_holds,
    const nb::object& block_loop_counts,
    const nb::object& selectors,
    uint64_t max_paths,
    uint32_t max_action_depth) {
    return {
        .enumeration = {
            .render = cue_options(
                loop_count,
                advance_after_infinite,
                0,
                nb::none(),
                include_empty_holds,
                block_loop_counts),
            .max_paths = max_paths,
        },
        .selector_values = selector_values_from_object(selectors),
        .max_action_depth = max_action_depth,
    };
}

[[nodiscard]] uint32_t cue_index_from_id(
    const cricodecs::acb::AcbContainer& self,
    uint32_t cue_id) {
    const auto* cue = self.cue_graph().cue_by_id(cue_id);
    if (cue == nullptr) {
        raise_value_error("ACB cue ID was not found");
    }
    return static_cast<uint32_t>(cue - self.cue_graph().cues().data());
}

[[nodiscard]] cricodecs::acb::AcbCuePlaybackPlan selected_cue_plan(
    const cricodecs::acb::AcbContainer& self,
    uint32_t cue_index,
    const cricodecs::acb::AcbCueSheetResolveOptions& options,
    const nb::object& variant) {
    auto resolution = unwrap_expected(
        cricodecs::acb::resolve_cue_playback_paths(self, cue_index, options));
    if (resolution.plans.empty()) {
        raise_value_error("ACB cue has no statically playable plan");
    }

    if (variant.is_none()) {
        if (resolution.plans.size() != 1) {
            raise_value_error(
                "ACB cue resolves to " +
                std::to_string(resolution.plans.size()) +
                " distinct plans; pass selectors={...} or variant=N");
        }
        return std::move(resolution.plans.front().plan);
    }

    const auto selected = nb::cast<size_t>(variant);
    if (selected >= resolution.plans.size()) {
        raise_value_error(
            "ACB cue plan variant is out of range (available: 0.." +
            std::to_string(resolution.plans.size() - 1) + ")");
    }
    return std::move(resolution.plans[selected].plan);
}

[[nodiscard]] nb::dict selector_options_for_resolution(
    const cricodecs::acb::AcbCueSheetResolution& resolution) {
    std::map<std::string, std::set<std::string>> values;
    for (const auto& resolved : resolution.plans) {
        for (const auto& source : resolved.sources) {
            for (const auto& selector : source.selector_values) {
                if (!selector.name.empty() && !selector.value.empty()) {
                    values[selector.name].insert(selector.value);
                }
            }
            for (const auto& path : source.paths) {
                for (const auto& choice : path) {
                    if (!choice.selector_name.empty() &&
                        !choice.selector_value.empty()) {
                        values[choice.selector_name].insert(
                            choice.selector_value);
                    }
                }
            }
        }
    }

    nb::dict result;
    for (const auto& [name, options] : values) {
        nb::list option_list;
        for (const auto& option : options) {
            option_list.append(option);
        }
        result[nb::str(name.c_str())] = option_list;
    }
    return result;
}

} // namespace

void bind_acb_module(nb::module_& module) {
    bind_acb_cue_types(module);

    nb::class_<cricodecs::acb::WaveformAwbEntry>(module, "WaveformAwbEntry")
        .def_ro("waveform_index", &cricodecs::acb::WaveformAwbEntry::waveform_index)
        .def_ro("wave_id", &cricodecs::acb::WaveformAwbEntry::wave_id)
        .def_ro("awb_index", &cricodecs::acb::WaveformAwbEntry::awb_index)
        .def_ro("stream_bank", &cricodecs::acb::WaveformAwbEntry::stream_bank);

    nb::class_<cricodecs::acb::AcbContainer>(module, "Acb")
        .def_static(
            "load",
            &load_acb_any,
            nb::arg("source"),
            nb::arg("encoding") = nb::none()
        )
        .def_static(
            "load_bytes",
            [](const nb::bytes& data, nb::object encoding) {
                auto owned = copy_python_bytes(data);
                return unwrap_expected(cricodecs::acb::AcbContainer::load(
                    std::move(owned),
                    python_encoding_options_from_object(encoding)
                ));
            },
            nb::arg("data"),
            nb::arg("encoding") = nb::none(),
            "Load an ACB from raw bytes."
        )
        .def_prop_ro("source_path", [](const cricodecs::acb::AcbContainer& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro("name", [](const cricodecs::acb::AcbContainer& self) {
            return std::string(self.name());
        })
        .def_prop_ro("waveform_count", &cricodecs::acb::AcbContainer::waveform_count)
        .def_prop_ro("cue_count", [](const cricodecs::acb::AcbContainer& self) {
            return self.cue_graph().cues().size();
        })
        .def_prop_ro(
            "graph",
            &cricodecs::acb::AcbContainer::cue_graph,
            nb::rv_policy::reference_internal)
        .def_prop_ro("has_embedded_awb", &cricodecs::acb::AcbContainer::has_embedded_awb)
        .def_prop_ro("companion_awb_path", [](const cricodecs::acb::AcbContainer& self) -> nb::object {
            const auto path = self.companion_awb_path();
            return path ? nb::cast(path->generic_string()) : nb::none();
        })
        .def_prop_ro("has_aac_waveforms", &cricodecs::acb::AcbContainer::has_aac_waveforms)
        .def("info", [](const cricodecs::acb::AcbContainer& self) {
            nb::object info = simple_namespace();
            info.attr("table_name") = std::string(self.header_table().table_name());
            info.attr("row_count") = self.header_table().row_count();
            info.attr("column_count") = self.header_table().column_count();
            info.attr("source_path") = path_or_none(self.source_path());
            info.attr("name") = std::string(self.name());
            info.attr("waveform_count") = self.waveform_count();
            info.attr("cue_count") = self.cue_graph().cues().size();
            nb::list waveforms;
            for (uint32_t index = 0; index < self.waveform_count(); ++index) {
                waveforms.append(waveform_info_object(self, index));
            }
            info.attr("waveforms") = waveforms;
            info.attr("has_embedded_awb") = self.has_embedded_awb();
            const auto companion = self.companion_awb_path();
            info.attr("companion_awb_path") = companion.has_value() ? nb::cast(companion->generic_string()) : nb::none();
            info.attr("has_aac_waveforms") = self.has_aac_waveforms();
            if (self.has_embedded_awb() || companion) {
                auto awb = unwrap_expected(self.load_awb());
                nb::object awb_info = simple_namespace();
                awb_info.attr("source_path") = path_or_none(awb.source_path());
                awb_info.attr("file_count") = awb.file_count();
                awb_info.attr("version") = awb.version();
                awb_info.attr("offset_size") = awb.offset_size();
                awb_info.attr("id_size") = awb.id_size();
                awb_info.attr("alignment") = awb.alignment();
                awb_info.attr("subkey") = awb.subkey();
                info.attr("awb_info") = awb_info;
            } else {
                info.attr("awb_info") = nb::none();
            }
            return info;
        })
        .def("waveform", [](const cricodecs::acb::AcbContainer& self, uint32_t index) {
            static_cast<void>(checked_container(self, index));
            return waveform_info_object(self, index);
        }, nb::arg("index"))
        .def(
            "waveform_awb_entry",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               const nb::object& bank,
               bool prefer_stream_bank) {
                if (bank.is_none()) {
                    return unwrap_expected(self.waveform_awb_entry(index, prefer_stream_bank));
                }
                auto& awb = nb::cast<cricodecs::awb::AwbContainer&>(bank);
                return unwrap_expected(self.waveform_awb_entry(index, awb, prefer_stream_bank));
            },
            nb::arg("index"),
            nb::arg("awb") = nb::none(),
            nb::arg("prefer_stream_bank") = false
        )
        .def(
            "replace_waveform_bytes",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               cricodecs::awb::AwbContainer& awb,
               const nb::bytes& data,
               bool prefer_stream_bank) {
                const auto bytes = copy_python_bytes(data);
                return unwrap_expected(self.replace_waveform_data(index, awb, bytes, prefer_stream_bank));
            },
            nb::arg("index"),
            nb::arg("awb"),
            nb::arg("data"),
            nb::arg("prefer_stream_bank") = false
        )
        .def(
            "replace_waveform_file",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               cricodecs::awb::AwbContainer& awb,
               const nb::object& input_path,
               bool prefer_stream_bank) {
                return unwrap_expected(self.replace_waveform_file(
                    index,
                    awb,
                    require_python_path(input_path, "input_path"),
                    prefer_stream_bank));
            },
            nb::arg("index"),
            nb::arg("awb"),
            nb::arg("input_path"),
            nb::arg("prefer_stream_bank") = false
        )
        .def(
            "waveform_name",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index) {
                const auto& container = checked_container(self, index);
                return std::string(container.waveform_name(index));
            },
            nb::arg("index")
        )
        .def(
            "waveform_name_raw",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index) {
                const auto& container = checked_container(self, index);
                return string_to_python_bytes(container.waveform_name_raw(index));
            },
            nb::arg("index")
        )
        .def(
            "waveform_filename",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, bool include_index_prefix) {
                const auto& container = checked_container(self, index);
                return container.waveform_filename(index, include_index_prefix);
            },
            nb::arg("index"),
            nb::arg("include_index_prefix") = true
        )
        .def(
            "cue_filename",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               bool include_index_prefix) {
                return cricodecs::acb::cue_filename(
                    self, index, include_index_prefix);
            },
            nb::arg("index"),
            nb::arg("include_index_prefix") = true
        )
        .def(
            "selector_options",
            [](const cricodecs::acb::AcbContainer& self,
               const nb::object& index,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                const auto options = resolve_options(
                    0,
                    true,
                    true,
                    nb::none(),
                    nb::none(),
                    max_paths,
                    max_action_depth);
                if (index.is_none()) {
                    return selector_options_for_resolution(unwrap_expected(
                        cricodecs::acb::resolve_cue_sheet_playback(
                            self, options)));
                }
                return selector_options_for_resolution(unwrap_expected(
                    cricodecs::acb::resolve_cue_playback_paths(
                        self, nb::cast<uint32_t>(index), options)));
            },
            nb::arg("index") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64)
        .def(
            "resolve_cue",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               const nb::object& selectors,
               uint32_t loop_count,
               bool advance_after_infinite,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                return unwrap_expected(
                    cricodecs::acb::resolve_cue_playback_paths(
                        self,
                        index,
                        resolve_options(
                            loop_count,
                            advance_after_infinite,
                            include_empty_holds,
                            block_loop_counts,
                            selectors,
                            max_paths,
                            max_action_depth)));
            },
            nb::arg("index"),
            nb::arg("selectors") = nb::none(),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64)
        .def(
            "resolve_cue_by_id",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t cue_id,
               const nb::object& selectors,
               uint32_t loop_count,
               bool advance_after_infinite,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                return unwrap_expected(
                    cricodecs::acb::resolve_cue_playback_paths(
                        self,
                        cue_index_from_id(self, cue_id),
                        resolve_options(
                            loop_count,
                            advance_after_infinite,
                            include_empty_holds,
                            block_loop_counts,
                            selectors,
                            max_paths,
                            max_action_depth)));
            },
            nb::arg("cue_id"),
            nb::arg("selectors") = nb::none(),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64)
        .def(
            "resolve_cues",
            [](const cricodecs::acb::AcbContainer& self,
               const nb::object& selectors,
               uint32_t loop_count,
               bool advance_after_infinite,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                return unwrap_expected(
                    cricodecs::acb::resolve_cue_sheet_playback(
                        self,
                        resolve_options(
                            loop_count,
                            advance_after_infinite,
                            include_empty_holds,
                            block_loop_counts,
                            selectors,
                            max_paths,
                            max_action_depth)));
            },
            nb::arg("selectors") = nb::none(),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64)
        .def(
            "cue_plan",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               uint32_t loop_count,
               bool advance_after_infinite,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               const nb::object& selectors,
               const nb::object& variant,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                return selected_cue_plan(
                    self,
                    index,
                    resolve_options(
                        loop_count,
                        advance_after_infinite,
                        include_empty_holds,
                        block_loop_counts,
                        selectors,
                        max_paths,
                        max_action_depth),
                    variant);
            },
            nb::arg("index"),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("selectors") = nb::none(),
            nb::arg("variant") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64
        )
        .def(
            "cue_plan_by_id",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t cue_id,
               uint32_t loop_count,
               bool advance_after_infinite,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               const nb::object& selectors,
               const nb::object& variant,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                return selected_cue_plan(
                    self,
                    cue_index_from_id(self, cue_id),
                    resolve_options(
                        loop_count,
                        advance_after_infinite,
                        include_empty_holds,
                        block_loop_counts,
                        selectors,
                        max_paths,
                        max_action_depth),
                    variant);
            },
            nb::arg("cue_id"),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("selectors") = nb::none(),
            nb::arg("variant") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64
        )
        .def(
            "cue_wav_bytes",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               uint32_t loop_count,
               bool advance_after_infinite,
               uint64_t hca_keycode,
               const nb::object& hca_subkey,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               const nb::object& selectors,
               const nb::object& variant,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                auto plan = selected_cue_plan(
                    self,
                    index,
                    resolve_options(
                        loop_count,
                        advance_after_infinite,
                        include_empty_holds,
                        block_loop_counts,
                        selectors,
                        max_paths,
                        max_action_depth),
                    variant);
                auto rendered = unwrap_expected(
                    cricodecs::acb::render_cue_plan(
                        self,
                        std::move(plan),
                        cue_options(
                            loop_count,
                            advance_after_infinite,
                            hca_keycode,
                            hca_subkey,
                            include_empty_holds,
                            block_loop_counts)));
                auto wav = unwrap_expected(cricodecs::wav::WavContainer::build_bytes(
                    rendered.pcm,
                    rendered.sample_rate,
                    rendered.channels));
                return to_python_bytes(wav);
            },
            nb::arg("index"),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("hca_keycode") = 0,
            nb::arg("hca_subkey") = nb::none(),
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("selectors") = nb::none(),
            nb::arg("variant") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64
        )
        .def(
            "extract_cue",
            [](const cricodecs::acb::AcbContainer& self,
               uint32_t index,
               const nb::object& output_path,
               uint32_t loop_count,
               bool advance_after_infinite,
               uint64_t hca_keycode,
               const nb::object& hca_subkey,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               const nb::object& selectors,
               const nb::object& variant,
               uint64_t max_paths,
               uint32_t max_action_depth) {
                auto plan = selected_cue_plan(
                    self,
                    index,
                    resolve_options(
                        loop_count,
                        advance_after_infinite,
                        include_empty_holds,
                        block_loop_counts,
                        selectors,
                        max_paths,
                        max_action_depth),
                    variant);
                unwrap_expected(cricodecs::acb::extract_cue_plan(
                    self,
                    std::move(plan),
                    require_python_path(output_path, "output_path"),
                    cue_options(
                        loop_count,
                        advance_after_infinite,
                        hca_keycode,
                        hca_subkey,
                        include_empty_holds,
                        block_loop_counts)));
            },
            nb::arg("index"),
            nb::arg("output_path"),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("hca_keycode") = 0,
            nb::arg("hca_subkey") = nb::none(),
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("selectors") = nb::none(),
            nb::arg("variant") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64
        )
        .def(
            "extract_cues",
            [](const cricodecs::acb::AcbContainer& self,
               const nb::object& output_dir,
               uint32_t loop_count,
               bool advance_after_infinite,
               uint64_t hca_keycode,
               const nb::object& hca_subkey,
               bool include_empty_holds,
               const nb::object& block_loop_counts,
               const nb::object& selectors,
               uint64_t max_paths,
               uint32_t max_action_depth,
               bool include_index_prefix) {
                const auto root = require_python_path(output_dir, "output_dir");
                std::error_code filesystem_error;
                std::filesystem::create_directories(root, filesystem_error);
                if (filesystem_error) {
                    raise_value_error(
                        "ACB cue extract failed: could not create output directory: " +
                        filesystem_error.message());
                }
                const auto audio_options = cue_options(
                    loop_count,
                    advance_after_infinite,
                    hca_keycode,
                    hca_subkey,
                    include_empty_holds,
                    block_loop_counts);
                auto resolution = unwrap_expected(
                    cricodecs::acb::resolve_cue_sheet_playback(
                        self,
                        resolve_options(
                            loop_count,
                            advance_after_infinite,
                            include_empty_holds,
                            block_loop_counts,
                            selectors,
                            max_paths,
                            max_action_depth)));
                const auto filenames = cricodecs::acb::cue_plan_filenames(
                    resolution, include_index_prefix);
                nb::list paths;
                for (size_t index = 0; index < resolution.plans.size(); ++index) {
                    const auto path = root / filenames[index];
                    unwrap_expected(cricodecs::acb::extract_cue_plan(
                        self,
                        std::move(resolution.plans[index].plan),
                        path,
                        audio_options));
                    paths.append(path.generic_string());
                }
                return paths;
            },
            nb::arg("output_dir"),
            nb::arg("loop_count") = 0,
            nb::arg("advance_after_infinite") = true,
            nb::arg("hca_keycode") = 0,
            nb::arg("hca_subkey") = nb::none(),
            nb::arg("include_empty_holds") = false,
            nb::arg("block_loop_counts") = nb::none(),
            nb::arg("selectors") = nb::none(),
            nb::arg("max_paths") = 65'536,
            nb::arg("max_action_depth") = 64,
            nb::arg("include_index_prefix") = true
        )
        .def(
            "extract_waveform_data",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t aac_keycode) {
                return to_python_bytes(unwrap_expected(self.extract_waveform_data(index, aac_keycode)));
            },
            nb::arg("index"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "waveform_bytes",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t aac_keycode) {
                return to_python_bytes(unwrap_expected(self.extract_waveform_data(index, aac_keycode)));
            },
            nb::arg("index"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "extract_waveform_stream_data",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t aac_keycode) {
                return to_python_bytes(unwrap_expected(self.extract_waveform_stream_data(index, aac_keycode)));
            },
            nb::arg("index"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "probe_waveform_aac_encryption",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, uint64_t keycode) {
                return unwrap_expected(self.probe_waveform_aac_encryption(index, keycode));
            },
            nb::arg("index"),
            nb::arg("keycode")
        )
        .def("recover_aac_key", [](const cricodecs::acb::AcbContainer& self) {
            return unwrap_expected(self.recover_aac_key());
        })
        .def("embedded_awb_bytes", [](const cricodecs::acb::AcbContainer& self) -> nb::object {
            const auto bytes = self.embedded_awb();
            if (!bytes) {
                return nb::none();
            }
            return to_python_bytes(*bytes);
        })
        .def("load_awb", [](const cricodecs::acb::AcbContainer& self) -> nb::object {
            if (!self.has_embedded_awb() && !self.companion_awb_path()) {
                return nb::none();
            }
            return nb::cast(unwrap_expected(self.load_awb()));
        })
        .def(
            "extract_file",
            [](const cricodecs::acb::AcbContainer& self, uint32_t index, const nb::object& output_path, uint64_t aac_keycode) {
                unwrap_expected(self.extract_file(index, require_python_path(output_path, "output_path"), aac_keycode));
            },
            nb::arg("index"),
            nb::arg("output_path"),
            nb::arg("aac_keycode") = 0
        )
        .def(
            "extract",
            [](const cricodecs::acb::AcbContainer& self, const nb::object& output_dir, uint64_t aac_keycode) {
                unwrap_expected(self.extract(require_python_path(output_dir, "output_dir"), aac_keycode));
            },
            nb::arg("output_dir"),
            nb::arg("aac_keycode") = 0
        );

    install_attr_repr(module, "Acb", {"source_path", "name", "waveform_count", "cue_count", "has_embedded_awb", "companion_awb_path", "has_aac_waveforms"});
    install_attr_repr(module, "WaveformAwbEntry", {"waveform_index", "wave_id", "awb_index", "stream_bank"});

    module.def(
        "load",
        &load_acb_any,
        nb::arg("source"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "extract",
        [](const cricodecs::acb::AcbContainer& acb, const nb::object& output_dir, uint64_t aac_keycode) {
            unwrap_expected(acb.extract(require_python_path(output_dir, "output_dir"), aac_keycode));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("aac_keycode") = static_cast<uint64_t>(0)
    );
    module.def(
        "extract",
        [](const nb::object& source, const nb::object& output_dir, uint64_t aac_keycode, nb::object encoding) {
            auto acb = load_acb_any(source, encoding);
            unwrap_expected(acb.extract(require_python_path(output_dir, "output_dir"), aac_keycode));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("aac_keycode") = static_cast<uint64_t>(0),
        nb::arg("encoding") = nb::none()
    );
}

} // namespace cricodecs::python
