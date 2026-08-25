#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

[[nodiscard]] static std::expected<hca::HcaQuality, std::string> parse_hca_quality(std::string_view text) {
    const std::string quality = lower_ascii(trim_ascii(text));
    if (quality == "highest") return hca::HcaQuality::Highest;
    if (quality == "high") return hca::HcaQuality::High;
    if (quality == "middle" || quality == "medium") return hca::HcaQuality::Middle;
    if (quality == "low") return hca::HcaQuality::Low;
    if (quality == "lowest") return hca::HcaQuality::Lowest;
    return std::unexpected("unsupported HCA `--quality`; use highest, high, middle, low, or lowest");
}

[[nodiscard]] static std::expected<uint16_t, std::string> parse_hca_version(std::string_view text) {
    const std::string version = lower_ascii(trim_ascii(text));
    if (version == "1.02" || version == "1.2" || version == "v1.02" || version == "0x0102") {
        return hca::HCA_VERSION_V102;
    }
    if (version == "1.03" || version == "1.3" || version == "v1.03" || version == "0x0103") {
        return hca::HCA_VERSION_V103;
    }
    if (version == "2.00" || version == "2.0" || version == "v2.00" || version == "0x0200") {
        return hca::HCA_VERSION_V200;
    }
    if (version == "3.00" || version == "3.0" || version == "v3.00" || version == "0x0300") {
        return hca::HCA_VERSION_V300;
    }
    return std::unexpected("unsupported HCA `--header-version`; use 1.02, 1.03, 2.00, 3.00, or the matching hexadecimal value");
}

[[nodiscard]] static std::expected<ahx::AhxBitAllocationPattern, std::string> parse_ahx_profile(
    std::string_view text
) {
    const std::string profile = lower_ascii(trim_ascii(text));
    if (profile == "default") return ahx::default_bit_allocation_pattern();
    if (profile == "22050" || profile == "preset-22050" || profile == "preset_22050") {
        return ahx::preset_bit_allocation_pattern(ahx::AhxBitAllocationPreset::preset_22050);
    }
    if (profile == "24000" || profile == "preset-24000" || profile == "preset_24000") {
        return ahx::preset_bit_allocation_pattern(ahx::AhxBitAllocationPreset::preset_24000);
    }
    if (profile == "44100" || profile == "preset-44100" || profile == "preset_44100") {
        return ahx::preset_bit_allocation_pattern(ahx::AhxBitAllocationPreset::preset_44100);
    }
    if (profile == "48000" || profile == "preset-48000" || profile == "preset_48000") {
        return ahx::preset_bit_allocation_pattern(ahx::AhxBitAllocationPreset::preset_48000);
    }
    return std::unexpected("unsupported AHX `--profile`; use default, 22050, 24000, 44100, or 48000");
}

[[nodiscard]] static std::expected<cpk::CpkPreset, std::string> parse_cpk_profile(std::string_view text) {
    std::string profile = lower_ascii(trim_ascii(text));
    std::ranges::replace(profile, '_', '-');
    if (profile == "id") return cpk::CpkPreset::Id;
    if (profile == "filename") return cpk::CpkPreset::Filename;
    if (profile == "filename-id") return cpk::CpkPreset::FilenameId;
    if (profile == "filename-group") return cpk::CpkPreset::FilenameGroup;
    if (profile == "id-group") return cpk::CpkPreset::IdGroup;
    if (profile == "filename-id-group") return cpk::CpkPreset::FilenameIdGroup;
    return std::unexpected(
        "unsupported CPK `--profile`; use id, filename, filename-id, filename-group, id-group, or filename-id-group");
}

[[nodiscard]] std::vector<adx::AdxLoop> wav_loops_as_adx_loops(const wav::WavContainer& wav) {
    std::vector<adx::AdxLoop> loops;
    const auto& sample_loops = wav.sampler().loops;
    loops.reserve(sample_loops.size());
    for (const auto& loop : sample_loops) {
        loops.push_back(adx::AdxLoop{
            .index = static_cast<uint16_t>(loop.cue_point_id),
            .type = static_cast<uint16_t>(loop.type),
            .start_sample = loop.start,
            .start_byte = 0,
            .end_sample = loop.end,
            .end_byte = 0,
        });
    }
    return loops;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_hca_from_wav(
    const wav::WavContainer& source,
    const Options& options
) {
    hca::HcaEncodeConfig config;
    config.sample_rate = source.sample_rate();
    config.channel_count = static_cast<uint8_t>(source.channels());
    config.bitrate = options.bitrate.value_or(0);
    config.ms_stereo = options.ms_stereo;

    if (options.version.has_value()) {
        auto version = parse_hca_version(*options.version);
        if (!version) return std::unexpected(version.error());
        config.version = *version;
    }
    if (options.quality.has_value()) {
        auto quality = parse_hca_quality(*options.quality);
        if (!quality) return std::unexpected(quality.error());
        config.quality = *quality;
    }
    const auto& loops = source.sampler().loops;
    if (!loops.empty() && loops.front().start < loops.front().end) {
        config.loop_enabled = true;
        config.loop_start = loops.front().start;
        config.loop_end = loops.front().end;
    }

    auto encoded = hca::encode(source, config);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }

    const bool wants_encryption =
        options.cipher_type.has_value() || options.key.has_value() || options.subkey.value_or(0) != 0;
    if (!wants_encryption) {
        return *encoded;
    }

    auto key = hca_keycode(options);
    if (!key) {
        return std::unexpected(key.error());
    }
    const uint16_t cipher_type = options.cipher_type.value_or(56);
    if (cipher_type != 1 && cipher_type != 56) {
        return std::unexpected("HCA encode `--cipher-type` must be 1 or 56");
    }
    if (cipher_type == 56 && *key == 0 && options.subkey.value_or(0) == 0) {
        return std::unexpected("HCA encode type 56 requires `--key` or `--subkey`");
    }

    auto encrypted = hca::encrypt(*encoded, cipher_type, *key, options.subkey.value_or(0));
    if (!encrypted) {
        return std::unexpected(encrypted.error());
    }
    return *encrypted;
}

[[nodiscard]] std::expected<std::vector<uint8_t>, std::string> encode_adx_like_from_wav(
    const wav::WavContainer& source,
    Format format,
    const Options& options
) {
    adx::AdxEncodeConfig config;
    config.sample_rate = source.sample_rate();
    config.channels = static_cast<uint8_t>(source.channels());
    config.encoding_mode = static_cast<uint8_t>(options.mode.value_or(format == Format::ahx ? 0x10 : 3));

    if (format == Format::adx) {
        if (config.encoding_mode != 2 && config.encoding_mode != 3 && config.encoding_mode != 4) {
            return std::unexpected("ADX encode `--mode` must be 2, 3, or 4");
        }
        config.highpass_freq = options.highpass.value_or(config.highpass_freq);
        config.delete_samples_after_loop_end = options.trim_after_loop;
        if (options.version.has_value()) {
            auto version = parse_u16(*options.version, "ADX --header-version");
            if (!version) return std::unexpected(version.error());
            if (*version != 3 && *version != 4 && *version != 5) {
                return std::unexpected("ADX encode `--header-version` must be 3, 4, or 5");
            }
            config.version = static_cast<uint8_t>(*version);
        }
    } else {
        if (config.encoding_mode != 0x10 && config.encoding_mode != 0x11) {
            return std::unexpected("AHX encode `--mode` must be 0x10 or 0x11");
        }
        if (options.profile.has_value()) {
            auto profile = parse_ahx_profile(*options.profile);
            if (!profile) return std::unexpected(profile.error());
            config.ahx_bit_allocation_pattern = *profile;
        }
    }

    if (options.key.has_value()) {
        const uint16_t encryption_type = options.cipher_type.value_or(8);
        if (encryption_type != 8 && encryption_type != 9) {
            return std::unexpected(
                std::string(format == Format::ahx ? "AHX" : "ADX") + " encode `--cipher-type` must be 8 or 9"
            );
        }
        config.encryption_type = static_cast<uint8_t>(encryption_type);
        if (format == Format::ahx && options.key->find(',') != std::string::npos) {
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
                return std::unexpected(
                    std::string(format == Format::ahx ? "AHX" : "ADX") + " type-9 encode requires numeric `--key`"
                );
            }
            config.key64 = *numeric;
            config.subkey = options.subkey.value_or(0);
        } else {
            if (format == Format::adx && options.key->find(',') != std::string::npos) {
                return std::unexpected("ADX encode does not accept a triplet key; use a type-8 key string");
            }
            config.key_string = *options.key;
        }
    } else if (options.cipher_type.has_value() || options.subkey.value_or(0) != 0) {
        return std::unexpected(
            std::string(format == Format::ahx ? "AHX" : "ADX") + " encode encryption options require `--key`"
        );
    }

    const auto loops = (format == Format::adx) ? wav_loops_as_adx_loops(source) : std::vector<adx::AdxLoop>{};
    auto encoded = adx::AdxEncoder::encode(source, config, loops);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    return *encoded;
}

[[nodiscard]] std::expected<void, std::string> perform_encode_action(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const Options& options
) {
    if (!options.force_type.has_value()) {
        return std::unexpected("`--encode` requires `-f` with hca, adx, or ahx");
    }
    const Format format = *options.force_type;
    if (format != Format::hca && format != Format::adx && format != Format::ahx) {
        return std::unexpected("`--encode` currently supports hca, adx, and ahx");
    }

    wav::WavContainer source;
    if (auto loaded = source.load(input_path); !loaded) {
        return std::unexpected("WAV load failed: " + loaded.error());
    }

    std::expected<std::vector<uint8_t>, std::string> encoded =
        (format == Format::hca)
            ? encode_hca_from_wav(source, options)
            : encode_adx_like_from_wav(source, format, options);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    return write_bytes_file(output_path, *encoded);
}

[[nodiscard]] std::expected<std::string, std::string> required_second(
    const MutationSpec& mutation,
    std::string_view option
) {
    if (!mutation.second.has_value() || mutation.second->empty()) {
        return std::unexpected(std::string(option) + " expects LEFT=RIGHT");
    }
    return *mutation.second;
}

[[nodiscard]] std::expected<uint32_t, std::string> target_u32_index(
    std::string_view text,
    std::string_view option
) {
    auto index = parse_index_target(text);
    if (!index.has_value()) {
        return std::unexpected(std::string(option) + " target must be an entry index for this format");
    }
    if (*index > std::numeric_limits<uint32_t>::max()) {
        return std::unexpected(std::string(option) + " target index is out of range");
    }
    return static_cast<uint32_t>(*index);
}

[[nodiscard]] std::expected<bool, std::string> mutation_bool(
    std::string_view text,
    std::string_view context
) {
    const auto value = lower_ascii(text);
    if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "loop") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off" || value == "plain") {
        return false;
    }
    return std::unexpected(std::string(context) + " expects true/false, on/off, loop/plain, or 1/0");
}

[[nodiscard]] std::expected<void, std::string> apply_aax_mutation(
    aax::AaxContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto bytes = read_bytes_file(mutation.first);
            if (!bytes) return std::unexpected(bytes.error());
            bool loop = false;
            if (mutation.second.has_value()) {
                auto parsed = mutation_bool(*mutation.second, "AAX `--add` loop state");
                if (!parsed) return std::unexpected(parsed.error());
                loop = *parsed;
            }
            return archive.add_segment(*bytes, loop);
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.replace_segment(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_segment(*index);
        }
        case MutationKind::rename:
            return std::unexpected("AAX does not support `--rename`");
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_segment(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported AAX mutation");
}

enum class AixMutationTargetKind : uint8_t {
    Segment,
    Layer,
    Cell,
};

struct AixMutationTarget {
    AixMutationTargetKind kind = AixMutationTargetKind::Segment;
    size_t first = 0;
    size_t second = 0;
};

[[nodiscard]] std::expected<size_t, std::string> aix_index(
    std::string_view text,
    std::string_view context
) {
    auto value = parse_u64(text, context);
    if (!value) return std::unexpected(value.error());
    if (*value > std::numeric_limits<size_t>::max()) {
        return std::unexpected(std::string(context) + " is out of range");
    }
    return static_cast<size_t>(*value);
}

[[nodiscard]] std::expected<AixMutationTarget, std::string> parse_aix_target(
    std::string_view text,
    std::string_view context
) {
    const auto lowered = lower_ascii(text);
    if (lowered.starts_with("segment:")) {
        auto index = aix_index(text.substr(8), context);
        if (!index) return std::unexpected(index.error());
        return AixMutationTarget{.kind = AixMutationTargetKind::Segment, .first = *index};
    }
    if (lowered.starts_with("layer:")) {
        auto index = aix_index(text.substr(6), context);
        if (!index) return std::unexpected(index.error());
        return AixMutationTarget{.kind = AixMutationTargetKind::Layer, .first = *index};
    }
    const auto split = text.find(':');
    if (split != std::string_view::npos) {
        auto segment = aix_index(text.substr(0, split), context);
        if (!segment) return std::unexpected(segment.error());
        auto layer = aix_index(text.substr(split + 1), context);
        if (!layer) return std::unexpected(layer.error());
        return AixMutationTarget{
            .kind = AixMutationTargetKind::Cell,
            .first = *segment,
            .second = *layer,
        };
    }
    auto index = aix_index(text, context);
    if (!index) return std::unexpected(index.error());
    return AixMutationTarget{.kind = AixMutationTargetKind::Segment, .first = *index};
}

[[nodiscard]] std::expected<std::vector<std::vector<uint8_t>>, std::string> read_aix_group(
    const std::filesystem::path& source,
    size_t expected_count,
    std::string_view context
) {
    std::error_code error;
    std::vector<std::vector<uint8_t>> result;
    if (std::filesystem::is_directory(source, error) && !error) {
        auto files = collect_directory_files(source);
        if (!files) return std::unexpected(files.error());
        result.reserve(files->size());
        for (const auto& [path, relative] : *files) {
            static_cast<void>(relative);
            auto bytes = read_bytes_file(path);
            if (!bytes) return std::unexpected(bytes.error());
            result.push_back(std::move(*bytes));
        }
    } else {
        auto bytes = read_bytes_file(source);
        if (!bytes) return std::unexpected(bytes.error());
        result.push_back(std::move(*bytes));
    }
    if (result.size() != expected_count) {
        return std::unexpected(std::string(context) + " requires " + std::to_string(expected_count) +
            " ordered ADX file(s), but the source provides " + std::to_string(result.size()));
    }
    return result;
}

[[nodiscard]] std::expected<void, std::string> apply_aix_mutation(
    aix::Aix& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            const auto mode = mutation.second.has_value() ? lower_ascii(*mutation.second) : "segment";
            if (mode == "segment") {
                auto layers = read_aix_group(mutation.first, archive.layers().size(), "AIX segment add");
                if (!layers) return std::unexpected(layers.error());
                return archive.add_segment(aix::AixBuildSegment{.layer_adx_data = std::move(*layers)});
            }
            if (mode == "layer") {
                auto segments = read_aix_group(mutation.first, archive.segments().size(), "AIX layer add");
                if (!segments) return std::unexpected(segments.error());
                return archive.add_layer(std::move(*segments));
            }
            return std::unexpected("AIX `--add` expects SRC=segment or SRC=layer");
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto target = parse_aix_target(mutation.first, "AIX `--replace` target");
            if (!target) return std::unexpected(target.error());
            if (target->kind == AixMutationTargetKind::Cell) {
                auto bytes = read_bytes_file(*source);
                if (!bytes) return std::unexpected(bytes.error());
                return archive.replace_layer(target->first, target->second, *bytes);
            }
            if (target->kind == AixMutationTargetKind::Segment) {
                auto layers = read_aix_group(*source, archive.layers().size(), "AIX segment replacement");
                if (!layers) return std::unexpected(layers.error());
                return archive.replace_segment(target->first, aix::AixBuildSegment{.layer_adx_data = std::move(*layers)});
            }
            return std::unexpected("AIX whole-layer replacement is not supported; replace cells as SEGMENT:LAYER=SRC");
        }
        case MutationKind::remove: {
            auto target = parse_aix_target(mutation.first, "AIX `--remove` target");
            if (!target) return std::unexpected(target.error());
            if (target->kind == AixMutationTargetKind::Segment) return archive.remove_segment(target->first);
            if (target->kind == AixMutationTargetKind::Layer) return archive.remove_layer(target->first);
            return std::unexpected("AIX `--remove` expects segment:N or layer:N");
        }
        case MutationKind::rename:
            return std::unexpected("AIX does not support `--rename`");
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_target = parse_aix_target(mutation.first, "AIX `--move` source");
            if (!from_target) return std::unexpected(from_target.error());
            auto to_target = parse_aix_target(*to, "AIX `--move` destination");
            if (!to_target) return std::unexpected(to_target.error());
            if (from_target->kind != to_target->kind || from_target->kind == AixMutationTargetKind::Cell) {
                return std::unexpected("AIX `--move` must use two segment:N targets or two layer:N targets");
            }
            return from_target->kind == AixMutationTargetKind::Segment
                ? archive.move_segment(from_target->first, to_target->first)
                : archive.move_layer(from_target->first, to_target->first);
        }
    }
    return std::unexpected("unsupported AIX mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_csb_mutation(
    csb::CsbContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto destination = required_second(mutation, "--add");
            if (!destination) return std::unexpected(destination.error());
            auto bytes = read_bytes_file(mutation.first);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.add_file(*bytes, *destination);
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.replace_file(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename: {
            auto destination = required_second(mutation, "--rename");
            if (!destination) return std::unexpected(destination.error());
            auto index = target_u32_index(mutation.first, "--rename");
            if (!index) return std::unexpected(index.error());
            return archive.rename_file(*index, *destination);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported CSB mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_afs_mutation(
    afs::AfsContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            const std::filesystem::path source = mutation.first;
            auto bytes = read_bytes_file(source);
            if (!bytes) return std::unexpected(bytes.error());
            archive.add_file(*bytes, mutation.second);
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.replace_file(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename: {
            auto name = required_second(mutation, "--rename");
            if (!name) return std::unexpected(name.error());
            auto index = target_u32_index(mutation.first, "--rename");
            if (!index) return std::unexpected(index.error());
            return archive.set_name(*index, *name);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported AFS mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_awb_mutation(
    awb::AwbContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto bytes = read_bytes_file(mutation.first);
            if (!bytes) return std::unexpected(bytes.error());
            if (mutation.second.has_value()) {
                auto wave_id = parse_u64(*mutation.second, "--add wave ID");
                if (!wave_id) return std::unexpected(wave_id.error());
                archive.add_file(*bytes, *wave_id);
            } else {
                archive.add_file(*bytes);
            }
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.replace_file(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename:
            return std::unexpected("AWB does not support `--rename`; use `--add SRC=WAVE_ID` for explicit new wave IDs");
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported AWB mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_cpk_mutation(
    cpk::Cpk& archive,
    const MutationSpec& mutation,
    const Options& options
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto dest = required_second(mutation, "--add");
            if (!dest) return std::unexpected(dest.error());
            archive.add_file(mutation.first, *dest, options.compress);
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = parse_index_target(mutation.first);
            if (!index.has_value()) {
                return std::unexpected("CPK `--replace` target must be an entry index");
            }
            return archive.replace_file(*index, *source, options.compress);
        }
        case MutationKind::remove: {
            auto index = parse_index_target(mutation.first);
            if (!index.has_value()) {
                return std::unexpected("CPK `--remove` target must be an entry index");
            }
            return archive.remove(*index);
        }
        case MutationKind::rename: {
            auto dest = required_second(mutation, "--rename");
            if (!dest) return std::unexpected(dest.error());
            auto index = parse_index_target(mutation.first);
            if (!index.has_value()) {
                return std::unexpected("CPK `--rename` target must be an entry index");
            }
            return archive.rename(*index, *dest);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = parse_index_target(mutation.first);
            auto to_index = parse_index_target(*to);
            if (!from_index.has_value() || !to_index.has_value()) {
                return std::unexpected("CPK `--move` expects FROM=TO entry indexes");
            }
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported CPK mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_cvm_mutation(
    cvm::CvmContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto dest = required_second(mutation, "--add");
            if (!dest) return std::unexpected(dest.error());
            auto added = archive.add_file(mutation.first, *dest);
            if (!added) return std::unexpected(added.error());
            return {};
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            if (auto index = parse_index_target(mutation.first)) {
                return archive.replace_file(static_cast<uint32_t>(*index), *source);
            }
            return archive.replace_file(mutation.first, *source);
        }
        case MutationKind::remove: {
            if (auto index = parse_index_target(mutation.first)) {
                return archive.remove(static_cast<uint32_t>(*index));
            }
            return archive.remove(mutation.first);
        }
        case MutationKind::rename: {
            auto dest = required_second(mutation, "--rename");
            if (!dest) return std::unexpected(dest.error());
            if (auto index = parse_index_target(mutation.first)) {
                return archive.rename(static_cast<uint32_t>(*index), *dest);
            }
            return archive.rename(mutation.first, *dest);
        }
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported CVM mutation");
}

[[nodiscard]] std::expected<void, std::string> apply_acx_mutation(
    acx::AcxContainer& archive,
    const MutationSpec& mutation
) {
    switch (mutation.kind) {
        case MutationKind::add: {
            auto bytes = read_bytes_file(mutation.first);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.add_file(*bytes);
        }
        case MutationKind::replace: {
            auto source = required_second(mutation, "--replace");
            if (!source) return std::unexpected(source.error());
            auto index = target_u32_index(mutation.first, "--replace");
            if (!index) return std::unexpected(index.error());
            auto bytes = read_bytes_file(*source);
            if (!bytes) return std::unexpected(bytes.error());
            return archive.set_file_data(*index, *bytes);
        }
        case MutationKind::remove: {
            auto index = target_u32_index(mutation.first, "--remove");
            if (!index) return std::unexpected(index.error());
            return archive.remove_file(*index);
        }
        case MutationKind::rename:
            return std::unexpected("ACX does not support `--rename`");
        case MutationKind::move: {
            auto to = required_second(mutation, "--move");
            if (!to) return std::unexpected(to.error());
            auto from_index = target_u32_index(mutation.first, "--move");
            if (!from_index) return std::unexpected(from_index.error());
            auto to_index = target_u32_index(*to, "--move");
            if (!to_index) return std::unexpected(to_index.error());
            return archive.move_file(*from_index, *to_index);
        }
    }
    return std::unexpected("unsupported ACX mutation");
}

template <class Archive, class Apply>
[[nodiscard]] std::expected<void, std::string> apply_mutations(
    Archive& archive,
    std::span<const MutationSpec> mutations,
    Apply apply
) {
    for (const auto& mutation : mutations) {
        if (auto result = apply(archive, mutation); !result) {
            return result;
        }
    }
    return {};
}

template <class Archive, class Apply, class Save>
[[nodiscard]] std::expected<void, std::string> mutate_and_save(
    Archive& archive,
    std::span<const MutationSpec> mutations,
    Apply apply,
    Save save
) {
    if (auto result = apply_mutations(archive, mutations, apply); !result) {
        return result;
    }
    return save(archive);
}

[[nodiscard]] std::expected<void, std::string> save_mutated_document(
    LoadedResult& loaded,
    const std::filesystem::path& output_path,
    const Options& options
) {
    return std::visit([&](auto& current) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(current)>;
        const auto save = [&](auto& archive) { return archive.save_to_file(output_path); };
        if constexpr (std::is_same_v<T, afs::AfsContainer>) {
            return mutate_and_save(current, options.mutations, apply_afs_mutation,
                [&](auto& archive) { return archive.build_to_file(output_path); });
        } else if constexpr (std::is_same_v<T, aax::AaxContainer>) {
            return mutate_and_save(current, options.mutations, apply_aax_mutation, save);
        } else if constexpr (std::is_same_v<T, aix::Aix>) {
            return mutate_and_save(current, options.mutations, apply_aix_mutation, save);
        } else if constexpr (std::is_same_v<T, awb::AwbContainer>) {
            return mutate_and_save(current, options.mutations, apply_awb_mutation, save);
        } else if constexpr (std::is_same_v<T, cpk::Cpk>) {
            return mutate_and_save(current, options.mutations,
                [&](auto& archive, const auto& mutation) { return apply_cpk_mutation(archive, mutation, options); },
                save);
        } else if constexpr (std::is_same_v<T, csb::CsbContainer>) {
            return mutate_and_save(current, options.mutations, apply_csb_mutation, save);
        } else if constexpr (std::is_same_v<T, cvm::CvmContainer>) {
            return mutate_and_save(current, options.mutations, apply_cvm_mutation,
                [&](auto& archive) { return archive.save_to_file(output_path, options.key.value_or("")); });
        } else if constexpr (std::is_same_v<T, acx::AcxContainer>) {
            return mutate_and_save(current, options.mutations, apply_acx_mutation, save);
        } else {
            return std::unexpected(std::string(format_label(loaded.format)) + " does not support CLI mutation yet");
        }
    }, loaded.document);
}

[[nodiscard]] std::expected<void, std::string> perform_build_action(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const Options& options
) {
    if (!options.force_type.has_value()) {
        return std::unexpected("`--build` requires `-f`");
    }

    const auto parse_sfd_profile = [](std::string_view text) -> std::expected<sfd::SfdBuildProfile, std::string> {
        const std::string key = lower_ascii(text);
        if (key == "sofdec" || key == "sofdec-stream" || key == "sofdec_stream") {
            return sfd::SfdBuildProfile::sofdec_stream_standard_fixed_2048;
        }
        if (key == "sofdec2-v23249" || key == "sofdec-stream2-v23249" || key == "stream2-v23249") {
            return sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23249;
        }
        if (key == "sofdec2-v23310" || key == "sofdec-stream2-v23310" || key == "stream2-v23310") {
            return sfd::SfdBuildProfile::sofdec_stream2_fixed_2048_v23310;
        }
        return std::unexpected("unsupported SFD `--profile`; use sofdec, sofdec2-v23249, or sofdec2-v23310");
    };

    switch (*options.force_type) {
        case Format::afs: {
            const uint32_t alignment = options.alignment.value_or(afs::AfsContainer::DEFAULT_ALIGNMENT);
            if (lower_ascii(input_path.extension().string()) == ".als") {
                auto archive = afs::AfsContainer::create_from_als(input_path, alignment, true);
                if (!archive) return std::unexpected(archive.error());
                return archive->build_to_file(output_path);
            }
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            auto archive = afs::AfsContainer::create(alignment, true);
            for (const auto& [source, relative] : *files) {
                auto bytes = read_bytes_file(source);
                if (!bytes) return std::unexpected(bytes.error());
                archive.add_file(*bytes, relative.generic_string());
            }
            return archive.build_to_file(output_path);
        }
        case Format::awb: {
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            const uint32_t raw_alignment = options.alignment.value_or(awb::AwbContainer::DEFAULT_ALIGNMENT);
            if (raw_alignment > std::numeric_limits<uint16_t>::max()) {
                return std::unexpected("AWB `--alignment` is out of range for uint16");
            }
            uint16_t raw_version = awb::AwbContainer::DEFAULT_VERSION;
            if (options.version.has_value()) {
                auto version = parse_u16(*options.version, "AWB --header-version");
                if (!version) return std::unexpected(version.error());
                if (*version > std::numeric_limits<uint8_t>::max()) {
                    return std::unexpected("AWB `--header-version` must be in the range 0..255");
                }
                raw_version = *version;
            }
            auto archive = awb::AwbContainer::create(
                static_cast<uint8_t>(raw_version),
                static_cast<uint16_t>(raw_alignment),
                options.subkey.value_or(0));
            for (const auto& [source, _] : *files) {
                auto bytes = read_bytes_file(source);
                if (!bytes) return std::unexpected(bytes.error());
                archive.add_file(*bytes);
            }
            return archive.save_to_file(output_path);
        }
        case Format::cpk: {
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            cpk::CpkBuilder builder;
            for (const auto& [source, relative] : *files) {
                builder.add_file(source, relative.generic_string(), options.compress);
            }
            cpk::CpkBuilderOptions build_options;
            build_options.encoding = encoding_options(options);
            if (options.alignment.has_value()) {
                if (*options.alignment > std::numeric_limits<uint16_t>::max()) {
                    return std::unexpected("CPK `--alignment` is out of range for uint16");
                }
                build_options.align = static_cast<uint16_t>(*options.alignment);
            }
            if (options.profile.has_value()) {
                auto profile = parse_cpk_profile(*options.profile);
                if (!profile) return std::unexpected(profile.error());
                build_options.preset = *profile;
            }
            return builder.build_to_file(output_path, build_options);
        }
        case Format::acx: {
            acx::AcxBuilder builder;
            const uint32_t alignment = options.alignment.value_or(4);
            if (lower_ascii(input_path.extension().string()) == ".fls") {
                return builder.build_file_list_to_file(input_path, output_path, alignment);
            }
            auto files = collect_directory_files(input_path);
            if (!files) return std::unexpected(files.error());
            acx::AcxBuildInput input;
            input.alignment = alignment;
            input.entries.reserve(files->size());
            for (const auto& [source, _] : *files) {
                input.entries.push_back({.source_path = source, .data = std::nullopt});
            }
            return builder.build_to_file(output_path, input);
        }
        case Format::csb:
            return csb::CsbContainer::build_to_file(input_path, output_path, encoding_options(options));
        case Format::cvm: {
            cvm::CvmBuilder builder;
            if (lower_ascii(input_path.extension().string()) == ".cvs") {
                auto script = cvm::CvmBuildScript::load(input_path);
                if (!script) return std::unexpected(script.error());
                return builder.build_to_file(output_path, *script, options.key.value_or(""));
            }
            auto input = cvm::CvmBuildInput::from_directory(input_path);
            if (!input) return std::unexpected(input.error());
            return builder.build_to_file(output_path, *input, options.key.value_or(""));
        }
        case Format::usm: {
            usm::UsmBuildInput input;
            input.video_path = input_path;
            input.alpha_path = options.alpha_path;
            input.encoding = encoding_options(options);
            if (options.key.has_value()) {
                auto key = parse_u64(*options.key, "--key");
                if (!key) return std::unexpected(key.error());
                input.key = *key;
            }
            for (size_t index = 0; index < options.audio_paths.size(); ++index) {
                input.audio_tracks.push_back({
                    .path = options.audio_paths[index],
                    .encrypt = std::nullopt,
                    .channel_no = options.audio_channels.empty()
                        ? std::nullopt
                        : std::optional<uint8_t>{options.audio_channels[index]},
                });
            }
            usm::UsmBuilder builder;
            return builder.build_to_file(output_path, input);
        }
        case Format::sfd: {
            if (options.audio_paths.size() > 1) {
                return std::unexpected("SFD build currently supports at most one `--audio` input");
            }
            sfd::SfdBuildInput input;
            input.video_path = input_path;
            if (!options.audio_paths.empty()) {
                input.audio_path = options.audio_paths.front();
            }
            input.output_name = output_path.filename().string();
            if (options.profile.has_value()) {
                auto profile = parse_sfd_profile(*options.profile);
                if (!profile) return std::unexpected(profile.error());
                input.build_profile = *profile;
            }
            if (options.version.has_value()) {
                input.header_builder_version = *options.version;
            }
            sfd::SfdBuilder builder;
            return builder.build_to_file(output_path, input);
        }
        default:
            return std::unexpected("`--build` currently supports afs, awb, cpk, acx, csb, cvm, usm, and sfd");
    }
}


} // namespace cricodecs::cli::detail
