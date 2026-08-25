#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

[[nodiscard]] std::expected<uint16_t, std::string> hca_encrypt_cipher_type(const Options& options) {
    if (options.cipher_type.has_value()) {
        if (*options.cipher_type != 1 && *options.cipher_type != 56) {
            return std::unexpected("HCA `--cipher-type` must be 1 or 56");
        }
        return *options.cipher_type;
    }
    if (options.key.has_value() || options.subkey.value_or(0) != 0) {
        return uint16_t{56};
    }
    return uint16_t{1};
}

[[nodiscard]] std::expected<adx::AdxEncodeConfig, std::string> make_adx_encrypt_config(
    const adx::Adx& audio,
    const Options& options
) {
    adx::AdxEncodeConfig config;
    const auto& header = audio.header();
    config.sample_rate = header.sample_rate;
    config.channels = header.channels;
    config.bit_depth = header.bit_depth;
    config.block_size = header.block_size;
    config.encoding_mode = header.encoding_mode;
    config.highpass_freq = header.highpass_freq;
    config.version = header.version;

    const uint16_t encryption_type = options.cipher_type.value_or(8);
    if (audio.is_ahx()) {
        if (encryption_type != 8 && encryption_type != 9) {
            return std::unexpected("AHX `--cipher-type` must be 8 or 9");
        }
        config.encryption_type = static_cast<uint8_t>(encryption_type);
        if (!options.key.has_value()) {
            return std::unexpected("AHX encrypt requires `--key`");
        }
        if (options.key->find(',') != std::string::npos) {
            auto triplet = parse_key_triplet(*options.key);
            if (!triplet) {
                return std::unexpected(triplet.error());
            }
            config.ahx_key = ahx::AhxKey{
                .start = (*triplet)[0],
                .mult = (*triplet)[1],
                .add = (*triplet)[2],
            };
        } else if (encryption_type == 9) {
            auto numeric = parse_u64(*options.key, "--key");
            if (!numeric) {
                return std::unexpected("AHX type-9 encrypt requires numeric `--key` or a start,mult,add triplet");
            }
            config.key64 = *numeric;
            config.subkey = options.subkey.value_or(0);
        } else {
            config.key_string = *options.key;
        }
        return config;
    }

    if (encryption_type != 8 && encryption_type != 9) {
        return std::unexpected("ADX `--cipher-type` must be 8 or 9");
    }
    if (!options.key.has_value()) {
        return std::unexpected("ADX encrypt requires `--key`");
    }
    if (options.key->find(',') != std::string::npos) {
        return std::unexpected("ADX encrypt does not accept a triplet key; use a type-8 key string");
    }

    auto numeric = parse_u64(*options.key, "--key");
    const bool use_type9 = (encryption_type == 9) || (!options.cipher_type.has_value() && numeric.has_value());
    if (use_type9) {
        if (!numeric) {
            return std::unexpected("ADX type-9 encrypt requires numeric `--key`");
        }
        config.encryption_type = 9;
        config.key64 = *numeric;
        config.subkey = options.subkey.value_or(0);
        return config;
    }

    config.encryption_type = 8;
    config.key_string = *options.key;
    return config;
}

[[nodiscard]] std::string render_index(size_t index) {
    return std::to_string(index);
}

[[nodiscard]] std::filesystem::path render_output_template(
    const std::filesystem::path& output,
    const OutputItem& item,
    const std::filesystem::path& input_path
) {
    std::string rendered = output.generic_string();
    replace_all(rendered, "?i", render_index(item.index));
    replace_all(rendered, "?e", item.entry_name.filename().generic_string());
    replace_all(rendered, "?s", input_path.stem().generic_string());
    return std::filesystem::path(rendered);
}

[[nodiscard]] std::expected<std::vector<OutputItem>, std::string> select_items(
    std::vector<OutputItem> items,
    const Options& options
) {
    if (options.indexes.empty()) {
        return items;
    }

    std::vector<OutputItem> selected;
    selected.reserve(options.indexes.size());
    for (const auto index : options.indexes) {
        const auto it = std::ranges::find_if(items, [&](const OutputItem& item) {
            return item.index == index;
        });
        if (it == items.end()) {
            return std::unexpected("no entry matched index `" + std::to_string(index) + "`");
        }
        if (!std::ranges::any_of(selected, [&](const OutputItem& item) { return item.index == it->index; })) {
            selected.push_back(*it);
        }
    }
    return selected;
}

[[nodiscard]] std::filesystem::path resolve_item_output_path(
    const std::filesystem::path& default_root,
    const std::optional<std::filesystem::path>& output_override,
    const std::filesystem::path& input_path,
    const OutputItem& item,
    bool single_item
) {
    if (!output_override.has_value()) {
        return default_root / item.relative_path;
    }

    const auto& output = *output_override;
    if (contains_placeholder(output.generic_string())) {
        return render_output_template(output, item, input_path);
    }
    if (single_item) {
        return output;
    }
    return output / item.relative_path;
}

[[nodiscard]] std::expected<void, std::string> write_pcm_as_wav(
    const std::filesystem::path& output_path,
    std::span<const int16_t> pcm,
    uint32_t sample_rate,
    uint8_t channels,
    std::span<const wav::SampleLoop> loops = {}
) {
    return wav::WavContainer::write(output_path.string(), pcm, sample_rate, channels, loops);
}

template <typename Bytes>
[[nodiscard]] std::expected<void, std::string> write_bytes_result(
    std::expected<Bytes, std::string> result,
    const std::filesystem::path& output_path
) {
    return std::move(result).and_then([&](const auto& bytes) {
        return write_bytes_file(output_path, bytes);
    });
}

[[nodiscard]] std::expected<void, std::string> decode_adx_like_to_wav(
    adx::Adx& audio,
    const std::filesystem::path& output_path,
    const Options& options
) {
    if (auto applied = apply_adx_key(audio, options); !applied) {
        return std::unexpected(applied.error());
    }
    auto decoded = audio.decode();
    if (!decoded) {
        return std::unexpected(decoded.error());
    }
    std::vector<wav::SampleLoop> loops;
    loops.reserve(decoded->loops.size());
    for (const auto& loop : decoded->loops) {
        loops.push_back(loop.to_sample_loop());
    }
    return write_pcm_as_wav(
        output_path,
        decoded->pcm_data,
        decoded->sample_rate,
        decoded->channels,
        loops
    );
}

[[nodiscard]] std::expected<void, std::string> decode_hca_to_wav(
    const hca::Hca& audio,
    const std::filesystem::path& output_path,
    const Options& options
) {
    auto key = hca_keycode(options);
    if (!key) {
        return std::unexpected(key.error());
    }
    return audio.decode(*key, options.subkey.value_or(0)).and_then([&](const auto& pcm) {
        return write_pcm_as_wav(
            output_path, pcm, audio.header().fmt.sample_rate, audio.header().fmt.channel_count);
    });
}

[[nodiscard]] std::expected<void, std::string> decode_aax_to_wav(
    const aax::AaxContainer& archive,
    const std::filesystem::path& output_path,
    const Options& options
) {
    return archive.adx_data()
        .and_then([](const auto& bytes) { return adx::Adx::load(std::span<const uint8_t>(bytes)); })
        .and_then([&](auto audio) mutable {
            return decode_adx_like_to_wav(audio, output_path, options);
        });
}

[[nodiscard]] std::filesystem::path wav_output_path(const std::filesystem::path& path) {
    auto output = path;
    output.replace_extension(".wav");
    return output;
}

[[nodiscard]] std::optional<Format> detectable_audio_payload_format(std::span<const uint8_t> bytes) {
    for (const auto format : sniff_format_order(bytes, false)) {
        if (format == Format::adx || format == Format::hca || format == Format::aax) {
            return format;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::expected<bool, std::string> decode_audio_payload_to_wav(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_path,
    const Options& options,
    std::optional<uint16_t> fallback_subkey = std::nullopt
) {
    const auto detected = detectable_audio_payload_format(bytes);
    if (!detected.has_value()) {
        return false;
    }

    switch (*detected) {
        case Format::adx: {
            return adx::Adx::load(bytes).and_then([&](auto audio) mutable {
                return decode_adx_like_to_wav(audio, output_path, options).transform([] { return true; });
            });
        }
        case Format::hca: {
            auto audio = hca::Hca::load(bytes);
            if (!audio) {
                return std::unexpected(audio.error());
            }
            auto decode_options = options;
            if (!decode_options.subkey.has_value() && fallback_subkey.has_value()) {
                decode_options.subkey = *fallback_subkey;
            }
            return decode_hca_to_wav(*audio, output_path, decode_options).transform([] { return true; });
        }
        case Format::aax: {
            return aax::AaxContainer::load(bytes).and_then([&](const auto& audio) {
                return decode_aax_to_wav(audio, output_path, options).transform([] { return true; });
            });
        }
        default:
            return false;
    }
}

[[nodiscard]] OutputItem make_output_item(size_t index, const std::filesystem::path& relative_path) {
    return OutputItem{
        .index = index,
        .entry_name = relative_path.filename(),
        .relative_path = relative_path,
        .details = {},
        .cue_plan = std::nullopt,
        .cue_sources = {},
        .detailed = false,
    };
}

void append_cue_plan_details(
    OutputItem& item,
    const acb::AcbCuePlaybackPlan& plan) {
    for (const auto& block : plan.blocks) {
        std::string detail = "    block ";
        detail += block.block_position
            ? std::to_string(*block.block_position)
            : "-";
        if (block.block_index) {
            detail += " [row ";
            detail += std::to_string(*block.block_index);
            detail += ']';
        }
        detail += ": ";
        detail += block.name;
        detail += ", LoopNum=";
        detail += std::to_string(block.authored_loop_count);
        detail += ", loops=";
        detail += std::to_string(block.render_loop_count);
        detail += ", duration=";
        detail += std::to_string(block.duration_us);
        detail += " us";
        item.details.push_back(std::move(detail));
        for (const auto& clip : block.clips) {
            std::string clip_detail = "      clip waveform=";
            clip_detail += std::to_string(clip.waveform_index);
            if (clip.awb_wave_id) {
                clip_detail += ", awb_id=";
                clip_detail += std::to_string(*clip.awb_wave_id);
            }
            if (clip.awb_stream_index) {
                clip_detail += ", awb_stream_index=";
                clip_detail += std::to_string(*clip.awb_stream_index);
            }
            if (clip.awb_bank) {
                clip_detail += ", awb_bank=";
                clip_detail +=
                    *clip.awb_bank == acb::AcbCueAwbBank::stream
                    ? "stream"
                    : "memory";
            }
            if (clip.awb_port_no) {
                clip_detail += ", awb_port_no=";
                clip_detail += std::to_string(*clip.awb_port_no);
            }
            clip_detail += ", start=";
            clip_detail += std::to_string(clip.start_time_us);
            clip_detail += " us";
            item.details.push_back(std::move(clip_detail));
        }
    }
}

[[nodiscard]] std::expected<OutputListing, std::string> collect_export_items(
    LoadedResult& loaded,
    const Options& options
) {
    auto listing = std::visit([&options, format = loaded.format](
        auto& current) -> std::expected<OutputListing, std::string> {
        using T = std::decay_t<decltype(current)>;
        OutputListing listing{
            .format = format,
            .mode = options.cue_based
                ? OutputListingMode::cue_plans
                : OutputListingMode::entries,
            .raw = options.raw,
            .items = {},
            .acb_cues = std::nullopt,
        };
        auto& items = listing.items;

        if constexpr (!std::same_as<T, acb::AcbContainer>) {
            if (options.cue_based) {
                return std::unexpected(
                    "cue-based extraction is only supported for ACB inputs");
            }
        }

        if constexpr (std::same_as<T, afs::AfsContainer>) {
            for (const auto& entry : current.entries()) {
                if (entry.present) {
                    items.push_back(make_output_item(entry.index, entry.suggested_path(true)));
                }
            }
        } else if constexpr (std::same_as<T, acx::AcxContainer>) {
            for (const auto& entry : current.entries()) {
                items.push_back(make_output_item(entry.index, entry.suggested_path(true)));
            }
        } else if constexpr (std::same_as<T, awb::AwbContainer>) {
            for (uint32_t index = 0; index < current.file_count(); ++index) {
                auto raw_path = current.suggested_path(index);
                if (!raw_path) {
                    return std::unexpected(raw_path.error());
                }
                auto relative_path = *raw_path;
                if (!options.raw) {
                    auto payload = current.file_data(index);
                    if (!payload) {
                        return std::unexpected(payload.error());
                    }
                    if (detectable_audio_payload_format(*payload).has_value()) {
                        relative_path = wav_output_path(relative_path);
                    }
                }
                items.push_back(make_output_item(index, relative_path));
            }
        } else if constexpr (std::same_as<T, acb::AcbContainer>) {
            if (options.cue_based) {
                const acb::AcbCueRenderOptions cue_options{
                    .infinite_block_loop_count = options.cue_loop_count,
                    .advance_after_infinite_block = !options.cue_stop_at_loop,
                    .include_empty_infinite_blocks =
                        options.cue_include_empty_holds,
                    .block_loop_overrides = options.cue_block_loop_overrides,
                    .hca_subkey = options.subkey,
                };
                auto resolution = acb::resolve_cue_sheet_playback(current);
                if (!resolution) {
                    return std::unexpected(resolution.error());
                }
                if (resolution->plans.empty()) {
                    return std::unexpected(
                        "ACB contains no statically resolvable cue plans");
                }
                listing.acb_cues = AcbCueListingSummary{
                    .authored_cue_count = current.cue_graph().cues().size(),
                    .non_playable_cues = resolution->non_playable_cues,
                };

                const auto filenames = acb::cue_plan_filenames(*resolution);
                items.reserve(resolution->plans.size());
                for (size_t index = 0;
                     index < resolution->plans.size();
                     ++index) {
                    auto& resolved = resolution->plans[index];
                    auto item = make_output_item(index, filenames[index]);
                    const bool selected_for_override =
                        std::ranges::contains(options.indexes, index);
                    const bool needs_replan =
                        options.cue_stop_at_loop ||
                        options.cue_empty_hold_policy_set ||
                        options.cue_loop_count != 0 ||
                        (!options.cue_block_loop_overrides.empty() &&
                         selected_for_override);
                    if (needs_replan) {
                        const auto source = std::ranges::find_if(
                            resolved.sources,
                            [&](const auto& candidate) {
                                return candidate.source_cue_index ==
                                    resolved.plan.cue_index;
                            });
                        if (source == resolved.sources.end()) {
                            return std::unexpected(
                                "ACB cue export failed: resolved plan has no "
                                "direct terminal-cue path");
                        }
                        const std::span<const acb::AcbCueChoiceSelection> path =
                            source->paths.empty()
                            ? std::span<const acb::AcbCueChoiceSelection>{}
                            : std::span<const acb::AcbCueChoiceSelection>{
                                source->paths.front()};
                        auto replanned = acb::plan_cue_playback(
                            current,
                            resolved.plan.cue_index,
                            path,
                            cue_options);
                        if (!replanned) {
                            return std::unexpected(replanned.error());
                        }
                        item.cue_plan = std::move(*replanned);
                    } else {
                        item.cue_plan = std::move(resolved.plan);
                    }
                    item.cue_sources = std::move(resolved.sources);
                    items.push_back(std::move(item));
                }
                return listing;
            }
            for (uint32_t index = 0; index < current.waveform_count(); ++index) {
                auto relative_path = std::filesystem::path(current.waveform_filename(index, true));
                if (!options.raw) {
                    auto payload = current.extract_waveform_data(index, options.aac_keycode.value_or(0));
                    if (!payload) {
                        return std::unexpected(payload.error());
                    }
                    if (detectable_audio_payload_format(*payload).has_value()) {
                        relative_path = wav_output_path(relative_path);
                    }
                }
                items.push_back(make_output_item(index, relative_path));
            }
        } else if constexpr (std::same_as<T, cpk::Cpk>) {
            for (size_t index = 0; index < current.files().size(); ++index) {
                items.push_back(make_output_item(index, current.files()[index].full_path()));
            }
        } else if constexpr (std::same_as<T, csb::CsbContainer>) {
            for (uint32_t index = 0; index < current.stream_count(); ++index) {
                items.push_back(make_output_item(index, current.stream(index).suggested_path()));
            }
        } else if constexpr (std::same_as<T, cvm::CvmContainer>) {
            for (const auto& entry : current.entries()) {
                items.push_back(make_output_item(entry.index, entry.path));
            }
        } else if constexpr (std::same_as<T, aix::Aix>) {
            for (size_t segment_index = 0; segment_index < current.segments().size(); ++segment_index) {
                for (size_t layer_index = 0; layer_index < current.layers().size(); ++layer_index) {
                    items.push_back(make_output_item(
                        items.size(),
                        "segment_" + std::to_string(segment_index) + "_layer_" + std::to_string(layer_index) + ".adx"
                    ));
                }
            }
        } else if constexpr (std::same_as<T, sfd::SfdContainer>) {
            for (const auto& stream : current.streams()) {
                auto relative_path = stream.suggested_path(true);
                if (!options.raw) {
                    auto payload = current.extract_stream(stream.index);
                    if (!payload) {
                        return std::unexpected(payload.error());
                    }
                    if (detectable_audio_payload_format(*payload).has_value()) {
                        relative_path = wav_output_path(relative_path);
                    }
                }
                items.push_back(make_output_item(stream.index, relative_path));
            }
        } else if constexpr (std::same_as<T, usm::UsmReader>) {
            for (size_t index = 0; index < current.streams().size(); ++index) {
                const auto& stream = current.streams()[index];
                auto relative_path = std::filesystem::path(current.describe_stream(stream.id()));
                if (!options.raw) {
                    auto payload = current.extract_stream_sample(static_cast<uint32_t>(index), 4096);
                    if (!payload) {
                        return std::unexpected(payload.error());
                    }
                    if (detectable_audio_payload_format(*payload).has_value()) {
                        relative_path = wav_output_path(relative_path);
                    }
                }
                items.push_back(make_output_item(index, relative_path));
            }
        } else if constexpr (std::same_as<T, aax::AaxContainer>) {
            for (uint32_t index = 0; index < current.segment_count(); ++index) {
                items.push_back(make_output_item(index, "segment_" + std::to_string(index) + ".adx"));
            }
        } else {
            return std::unexpected("format does not expose exportable entries");
        }

        return listing;
    }, loaded.document);
    if (!listing || !options.list_only) {
        return listing;
    }

    for (auto& item : listing->items) {
        item.detailed =
            options.verbose || std::ranges::contains(options.indexes, item.index);
        if (item.detailed && item.cue_plan) {
            append_cue_plan_details(item, *item.cue_plan);
        }
    }
    if (!options.indexes.empty()) {
        auto selected = select_items(std::move(listing->items), options);
        if (!selected) {
            return std::unexpected(selected.error());
        }
        listing->items = std::move(*selected);
    }
    return listing;
}

[[nodiscard]] std::expected<void, std::string> write_export_item(
    LoadedResult& loaded,
    const OutputItem& item,
    const std::filesystem::path& output_path,
    const Options& options
) {
    return std::visit([&](auto& current) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(current)>;

        if constexpr (std::same_as<T, afs::AfsContainer>) {
            return current.export_stream(static_cast<uint32_t>(item.index), output_path);
        } else if constexpr (std::same_as<T, acx::AcxContainer> || std::same_as<T, csb::CsbContainer>) {
            return current.extract_file(static_cast<uint32_t>(item.index), output_path);
        } else if constexpr (std::same_as<T, awb::AwbContainer>) {
            auto payload = current.file_data(static_cast<uint32_t>(item.index));
            if (!payload) {
                return std::unexpected(payload.error());
            }
            if (!options.raw) {
                auto decoded = decode_audio_payload_to_wav(
                    *payload,
                    output_path,
                    options,
                    current.subkey() == 0 ? std::nullopt : std::optional<uint16_t>(current.subkey())
                );
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                if (*decoded) {
                    return {};
                }
            }
            return write_bytes_file(output_path, *payload);
        } else if constexpr (std::same_as<T, acb::AcbContainer>) {
            if (options.cue_based) {
                auto keycode = hca_keycode(options);
                if (!keycode) return std::unexpected(keycode.error());
                if (!item.cue_plan) {
                    return std::unexpected(
                        "ACB cue export failed: resolved cue plan is missing");
                }
                return acb::extract_cue_plan(
                    current,
                    *item.cue_plan,
                    output_path,
                    acb::AcbCueRenderOptions{
                        .infinite_block_loop_count = options.cue_loop_count,
                        .advance_after_infinite_block =
                            !options.cue_stop_at_loop,
                        .include_empty_infinite_blocks =
                            options.cue_include_empty_holds,
                        .block_loop_overrides =
                            options.cue_block_loop_overrides,
                        .hca_keycode = *keycode,
                        .hca_subkey = options.subkey,
                    });
            }
            auto payload = current.extract_waveform_data(static_cast<uint32_t>(item.index), options.aac_keycode.value_or(0));
            if (!payload) {
                return std::unexpected(payload.error());
            }
            if (!options.raw) {
                auto fallback_subkey = [&]() -> std::optional<uint16_t> {
                    auto subkey = current.awb_subkey();
                    if (!subkey || *subkey == 0) {
                        return std::nullopt;
                    }
                    return *subkey;
                }();
                auto decoded = decode_audio_payload_to_wav(*payload, output_path, options, fallback_subkey);
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                if (*decoded) {
                    return {};
                }
            }
            return write_bytes_file(output_path, *payload);
        } else if constexpr (std::same_as<T, cpk::Cpk>) {
            return write_bytes_result(
                current.extract_to_memory(current.files()[item.index]), output_path);
        } else if constexpr (std::same_as<T, cvm::CvmContainer>) {
            return current.extract_file(static_cast<uint32_t>(item.index), output_path);
        } else if constexpr (std::same_as<T, aix::Aix>) {
            const size_t layer_count = current.layers().size();
            const size_t segment_index = layer_count == 0 ? 0 : item.index / layer_count;
            const size_t layer_index = layer_count == 0 ? 0 : item.index % layer_count;
            return current.extract_file(segment_index, layer_index, output_path);
        } else if constexpr (std::same_as<T, aax::AaxContainer>) {
            return current.extract_file(static_cast<uint32_t>(item.index), output_path);
        } else if constexpr (std::same_as<T, sfd::SfdContainer>) {
            auto payload = current.extract_stream(static_cast<uint32_t>(item.index));
            if (!payload) {
                return std::unexpected(payload.error());
            }
            if (!options.raw) {
                auto decoded = decode_audio_payload_to_wav(*payload, output_path, options);
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                if (*decoded) {
                    return {};
                }
            }
            return write_bytes_file(output_path, *payload);
        } else if constexpr (std::same_as<T, usm::UsmReader>) {
            if (options.raw) {
                return current.extract_file(static_cast<uint32_t>(item.index), output_path);
            }
            auto payload = current.extract_stream(static_cast<uint32_t>(item.index));
            if (!payload) {
                return std::unexpected(payload.error());
            }
            if (!options.raw) {
                auto decoded = decode_audio_payload_to_wav(*payload, output_path, options);
                if (!decoded) {
                    return std::unexpected(decoded.error());
                }
                if (*decoded) {
                    return {};
                }
            }
            return write_bytes_file(output_path, *payload);
        } else {
            return std::unexpected("format does not expose exportable entries");
        }
    }, loaded.document);
}

[[nodiscard]] std::expected<void, std::string> perform_audio_export_action(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
) {
    if (options.raw) {
        return std::visit([&](auto& current) -> std::expected<void, std::string> {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::same_as<T, adx::Adx> || std::same_as<T, hca::Hca>) {
                return write_bytes_result(current.rebuild(), output_path);
            } else if constexpr (std::same_as<T, aax::AaxContainer>) {
                return write_bytes_result(current.adx_data(), output_path);
            } else {
                return std::unexpected("raw export is not supported for this format");
            }
        }, loaded.document);
    }

    return std::visit([&](auto& current) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(current)>;
        if constexpr (std::same_as<T, adx::Adx>) {
            return decode_adx_like_to_wav(current, output_path, options);
        } else if constexpr (std::same_as<T, hca::Hca>) {
            return decode_hca_to_wav(current, output_path, options);
        } else if constexpr (std::same_as<T, aax::AaxContainer>) {
            return decode_aax_to_wav(current, output_path, options);
        } else {
            return std::unexpected("audio export is not supported for this format");
        }
    }, loaded.document);
}

[[nodiscard]] std::expected<void, std::string> perform_crypto_action(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
) {
    return std::visit([&](auto& current) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(current)>;

        if constexpr (std::same_as<T, hca::Hca>) {
            if (options.encrypt) {
                auto key = hca_keycode(options);
                if (!key) {
                    return std::unexpected(key.error());
                }
                auto cipher_type = hca_encrypt_cipher_type(options);
                if (!cipher_type) {
                    return std::unexpected(cipher_type.error());
                }
                return write_bytes_result(
                    current.encrypt(*cipher_type, *key, options.subkey.value_or(0)), output_path);
            }
            auto key = hca_keycode(options);
            if (!key) {
                return std::unexpected(key.error());
            }
            return write_bytes_result(
                current.decrypt(*key, options.subkey.value_or(0)), output_path);
        } else if constexpr (std::same_as<T, adx::Adx>) {
            if (options.encrypt) {
                auto config = make_adx_encrypt_config(current, options);
                if (!config) {
                    return std::unexpected(config.error());
                }
                return write_bytes_result(current.encode(*config), output_path);
            }
            if (auto applied = apply_adx_key(current, options); !applied) {
                return std::unexpected(applied.error());
            }
            return write_bytes_result(current.decrypt(), output_path);
        } else if constexpr (std::same_as<T, cvm::CvmContainer>) {
            if (options.encrypt) {
                if (!options.key.has_value()) {
                    return std::unexpected("CVM encrypt requires `--key`");
                }
                return write_bytes_result(current.save(*options.key), output_path);
            }
            return write_bytes_result(current.save(), output_path);
        } else if constexpr (std::same_as<T, usm::UsmReader>) {
            return write_bytes_result(
                options.encrypt ? current.encrypt() : current.decrypt(), output_path);
        } else if constexpr (std::same_as<T, cpk::Cpk>) {
            return write_bytes_result(
                options.encrypt ? current.encrypt() : current.decrypt(), output_path);
        } else {
            return std::unexpected("`--encrypt`/`--decrypt` is not supported for this format");
        }
    }, loaded.document);
}

[[nodiscard]] std::expected<void, std::string> perform_multi_item_export(
    LoadedResult& loaded,
    const std::filesystem::path& input_path,
    const Options& options
) {
    auto items = collect_export_items(loaded, options);
    if (!items) {
        return std::unexpected(items.error());
    }
    auto selected = select_items(std::move(items->items), options);
    if (!selected) {
        return std::unexpected(selected.error());
    }

    const auto output_root = default_output_path(input_path, loaded.format, options.raw);
    const bool single_item = selected->size() == 1;
    for (const auto& item : *selected) {
        const auto output_path = resolve_item_output_path(output_root, options.output_path, input_path, item, single_item);
        std::error_code filesystem_error;
        if (const auto parent = output_path.parent_path(); !parent.empty()) {
            std::filesystem::create_directories(parent, filesystem_error);
            if (filesystem_error) {
                return std::unexpected(
                    "could not create output directory: " +
                    filesystem_error.message());
            }
        }
        if (auto result = write_export_item(loaded, item, output_path, options); !result) {
            return std::unexpected(result.error());
        }
    }

    return {};
}


} // namespace cricodecs::cli::detail
