#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

[[nodiscard]] std::expected<Options, std::string> parse_options(std::span<const std::string> args) {
    Options options;

    for (size_t index = 0; index < args.size(); ++index) {
        const auto& arg = args[index];
        const auto require_value = [&](std::string_view option) -> std::expected<std::string, std::string> {
            if (index + 1 >= args.size()) {
                return std::unexpected(std::string("missing value for option `") + std::string(option) + "`");
            }
            ++index;
            return args[index];
        };

        if (arg == "-h" || arg == "--help") {
            options.help = true;
            continue;
        }
        if (arg == "-e" || arg == "--export") {
            continue;
        }
        if (arg == "--encode") {
            options.encode = true;
            continue;
        }
        if (arg == "--build") {
            options.build = true;
            continue;
        }
        if (arg == "--compress") {
            options.compress = true;
            continue;
        }
        if (arg == "--ms-stereo") {
            options.ms_stereo = true;
            continue;
        }
        if (arg == "--trim-after-loop") {
            options.trim_after_loop = true;
            continue;
        }
        if (arg == "--cue") {
            options.cue_based = true;
            continue;
        }
        if (arg == "--cue-stop-at-loop" || arg == "--cue-stop-at-infinite") {
            options.cue_stop_at_loop = true;
            continue;
        }
        if (arg == "--cue-include-empty-holds") {
            options.cue_include_empty_holds = true;
            options.cue_empty_hold_policy_set = true;
            continue;
        }
        if (arg == "--cue-skip-empty-holds") {
            options.cue_include_empty_holds = false;
            options.cue_empty_hold_policy_set = true;
            continue;
        }
        if (arg == "--cue-loop-count") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto parsed = parse_u32(*value, "--cue-loop-count");
            if (!parsed) return std::unexpected(parsed.error());
            options.cue_loop_count = *parsed;
            continue;
        }
        if (arg == "--cue-infinite-plays") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto parsed = parse_u32(*value, "--cue-infinite-plays");
            if (!parsed) return std::unexpected(parsed.error());
            if (*parsed == 0) {
                return std::unexpected(
                    "`--cue-infinite-plays` must be at least one");
            }
            options.cue_loop_count = *parsed - 1;
            continue;
        }
        if (arg == "--cue-block-loop-count" || arg == "--cue-block-plays") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto pair = parse_pair_value(*value, arg);
            if (!pair) return std::unexpected(pair.error());
            auto block_position =
                parse_u32(pair->first, std::string(arg) + " block position");
            if (!block_position) {
                return std::unexpected(block_position.error());
            }
            auto loop_count =
                parse_u32(pair->second, std::string(arg) + " loop count");
            if (!loop_count) return std::unexpected(loop_count.error());
            if (arg == "--cue-block-plays" && *loop_count == 0) {
                return std::unexpected(
                    "`--cue-block-plays` play count must be at least one");
            }
            options.cue_block_loop_overrides.push_back({
                .block_position = *block_position,
                .loop_count = arg == "--cue-block-plays"
                    ? *loop_count - 1
                    : *loop_count,
            });
            continue;
        }
        if (arg == "--encrypt") {
            options.encrypt = true;
            continue;
        }
        if (arg == "--decrypt") {
            options.decrypt = true;
            continue;
        }
        if (arg == "--recover-key") {
            options.recover_key = true;
            continue;
        }
        if (arg == "--independent") {
            options.independent_key_recovery = true;
            continue;
        }
        if (arg == "--raw") {
            options.raw = true;
            continue;
        }
        if (arg == "--list") {
            options.list_only = true;
            continue;
        }
        if (arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "-m") {
            options.metadata_only = true;
            continue;
        }
        if (arg == "--json") {
            options.json = true;
            continue;
        }
        if (arg == "-q" || arg == "--quiet") {
            options.quiet = true;
            continue;
        }
        if (arg == "-f" || arg == "--force-type") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_format_key(*value);
            if (!parsed.has_value()) {
                return std::unexpected("unsupported forced type `" + *value + "`");
            }
            options.force_type = *parsed;
            continue;
        }
        if (arg == "-o" || arg == "--output") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.output_path = std::filesystem::path(*value);
            continue;
        }
        if (arg == "--index") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u64(*value, "--index");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.indexes.push_back(static_cast<size_t>(*parsed));
            continue;
        }
        if (arg == "--key") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.key = *value;
            continue;
        }
        if (arg == "--subkey") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u16(*value, "--subkey");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.subkey = *parsed;
            continue;
        }
        if (arg == "--cipher-type") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u16(*value, "--cipher-type");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.cipher_type = *parsed;
            continue;
        }
        if (arg == "--aac-keycode") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u64(*value, "--aac-keycode");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            options.aac_keycode = *parsed;
            continue;
        }
        if (arg == "--alignment") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto parsed = parse_u32(*value, "--alignment");
            if (!parsed) return std::unexpected(parsed.error());
            if (*parsed == 0) return std::unexpected("`--alignment` must be non-zero");
            options.alignment = *parsed;
            continue;
        }
        if (arg == "--bitrate") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto parsed = parse_u32(*value, "--bitrate");
            if (!parsed) return std::unexpected(parsed.error());
            options.bitrate = *parsed;
            continue;
        }
        if (arg == "--highpass") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto parsed = parse_u16(*value, "--highpass");
            if (!parsed) return std::unexpected(parsed.error());
            options.highpass = *parsed;
            continue;
        }
        if (arg == "--mode") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            auto parsed = parse_u16(*value, "--mode");
            if (!parsed) return std::unexpected(parsed.error());
            if (*parsed > std::numeric_limits<uint8_t>::max()) {
                return std::unexpected("`--mode` must be in the range 0..255");
            }
            options.mode = *parsed;
            continue;
        }
        if (arg == "--quality") {
            auto value = require_value(arg);
            if (!value) return std::unexpected(value.error());
            options.quality = *value;
            continue;
        }
        if (arg == "--encoding") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.encoding = *value;
            continue;
        }
        if (arg == "--audio") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.audio_paths.emplace_back(*value);
            continue;
        }
        if (arg == "--alpha") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.alpha_path = std::filesystem::path(*value);
            continue;
        }
        if (arg == "--audio-channel") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed = parse_u16(*value, "--audio-channel");
            if (!parsed) {
                return std::unexpected(parsed.error());
            }
            if (*parsed > std::numeric_limits<uint8_t>::max()) {
                return std::unexpected("`--audio-channel` must be in the range 0..255");
            }
            options.audio_channels.push_back(static_cast<uint8_t>(*parsed));
            continue;
        }
        if (arg == "--profile") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.profile = *value;
            continue;
        }
        if (arg == "--version") {
            options.show_version = true;
            continue;
        }
        if (arg == "--header-version") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.version = *value;
            continue;
        }
        if (arg == "--add") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            const size_t split = value->find('=');
            if (split == std::string::npos) {
                options.mutations.push_back(MutationSpec{MutationKind::add, trim_ascii(*value), std::nullopt});
            } else {
                auto pair = parse_pair_value(*value, "--add");
                if (!pair) {
                    return std::unexpected(pair.error());
                }
                options.mutations.push_back(MutationSpec{MutationKind::add, pair->first, pair->second});
            }
            continue;
        }
        if (arg == "--replace") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto pair = parse_pair_value(*value, "--replace");
            if (!pair) {
                return std::unexpected(pair.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::replace, pair->first, pair->second});
            continue;
        }
        if (arg == "--remove") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::remove, trim_ascii(*value), std::nullopt});
            continue;
        }
        if (arg == "--rename") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto pair = parse_pair_value(*value, "--rename");
            if (!pair) {
                return std::unexpected(pair.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::rename, pair->first, pair->second});
            continue;
        }
        if (arg == "--move") {
            auto value = require_value(arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto pair = parse_pair_value(*value, "--move");
            if (!pair) {
                return std::unexpected(pair.error());
            }
            options.mutations.push_back(MutationSpec{MutationKind::move, pair->first, pair->second});
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            return std::unexpected("unknown option `" + arg + "`");
        }
        options.input_paths.emplace_back(arg);
    }

    if (options.help || options.show_version) {
        return options;
    }
    if (options.input_paths.empty()) {
        options.help = true;
        return options;
    }
    if (!options.recover_key && options.input_paths.size() != 1) {
        return std::unexpected("only one input path is supported");
    }
    if (options.recover_key && !options.force_type.has_value()) {
        return std::unexpected("`--recover-key` requires `-f hca`, `-f usm`, `-f adx`, `-f ahx`, `-f awb`, or `-f acb`");
    }
    if (options.independent_key_recovery && !options.recover_key) {
        return std::unexpected("`--independent` is only valid with `--recover-key`");
    }
    if (options.recover_key && *options.force_type != Format::hca &&
        *options.force_type != Format::usm && *options.force_type != Format::adx &&
        *options.force_type != Format::ahx && *options.force_type != Format::awb &&
        *options.force_type != Format::acb) {
        return std::unexpected("`--recover-key` only supports `-f hca`, `-f usm`, `-f adx`, `-f ahx`, `-f awb`, or `-f acb`");
    }
    if (options.recover_key
        && (options.encode || options.build || options.metadata_only || options.raw
            || options.list_only || options.encrypt || options.decrypt
            || options.output_path.has_value()
            || options.encoding.has_value() || options.profile.has_value()
            || options.version.has_value() || options.key.has_value()
            || options.subkey.has_value() || options.cipher_type.has_value()
            || options.aac_keycode.has_value() || !options.indexes.empty()
            || options.alpha_path.has_value() || !options.audio_paths.empty()
            || !options.audio_channels.empty() || !options.mutations.empty()
            || options.alignment.has_value() || options.bitrate.has_value()
            || options.highpass.has_value() || options.mode.has_value() || options.quality.has_value()
            || options.ms_stereo || options.trim_after_loop || options.compress)) {
        return std::unexpected(
            "`--recover-key` cannot be combined with metadata/export/build/encode/crypto/mutation options");
    }
    if (options.encode && !options.output_path.has_value()) {
        return std::unexpected("`--encode` requires `-o`/`--output`");
    }
    if (options.encode && !options.force_type.has_value()) {
        return std::unexpected("`--encode` requires `-f` with hca, adx, or ahx");
    }
    if (options.build && !options.output_path.has_value()) {
        return std::unexpected("`--build` requires `-o`/`--output`");
    }
    if (options.build && !options.force_type.has_value()) {
        return std::unexpected("`--build` requires `-f`");
    }
    if (options.encode && (options.metadata_only || options.json || options.raw || options.list_only || options.encrypt || options.decrypt || !options.indexes.empty())) {
        return std::unexpected("`--encode` cannot be combined with export/list/metadata/crypto selection flags");
    }
    if (options.build && (options.encode || options.metadata_only || options.json || options.raw || options.list_only || options.encrypt || options.decrypt || !options.indexes.empty())) {
        return std::unexpected("`--build` cannot be combined with encode/export/list/metadata/crypto selection flags");
    }
    if (!options.build && !options.encode &&
        (options.alpha_path.has_value() || !options.audio_paths.empty()
         || !options.audio_channels.empty() || options.profile.has_value()
         || options.version.has_value() || options.alignment.has_value() || options.bitrate.has_value()
         || options.highpass.has_value() || options.mode.has_value() || options.quality.has_value()
         || options.ms_stereo || options.trim_after_loop)) {
        return std::unexpected("encoder and builder configuration options require `--encode` or `--build`");
    }
    if (!options.audio_channels.empty() && options.audio_channels.size() != options.audio_paths.size()) {
        return std::unexpected("repeat `--audio-channel` once per `--audio`, or omit it for automatic channels");
    }
    if (!options.audio_channels.empty() && options.force_type != Format::usm) {
        return std::unexpected("`--audio-channel` is only supported for USM builds");
    }
    if (options.alpha_path.has_value() &&
        (!options.build || options.force_type != Format::usm)) {
        return std::unexpected("`--alpha` is only supported for USM builds");
    }
    if (options.encode) {
        const Format format = *options.force_type;
        if (options.alignment.has_value() || options.alpha_path.has_value() ||
            !options.audio_paths.empty() || !options.audio_channels.empty()) {
            return std::unexpected("`--alignment`, `--alpha`, `--audio`, and `--audio-channel` are builder options");
        }
        if ((options.quality.has_value() || options.bitrate.has_value() || options.ms_stereo) && format != Format::hca) {
            return std::unexpected("`--quality`, `--bitrate`, and `--ms-stereo` are only supported for HCA encoding");
        }
        if (options.version.has_value() && format != Format::hca && format != Format::adx) {
            return std::unexpected("`--header-version` is only supported for HCA and ADX encoding");
        }
        if (options.mode.has_value() && format != Format::adx && format != Format::ahx) {
            return std::unexpected("`--mode` is only supported for ADX and AHX encoding");
        }
        if ((options.highpass.has_value() || options.trim_after_loop) && format != Format::adx) {
            return std::unexpected("`--highpass` and `--trim-after-loop` are only supported for ADX encoding");
        }
        if (options.profile.has_value() && format != Format::ahx) {
            return std::unexpected("`--profile` is only supported for AHX encoding");
        }
        if (options.encoding.has_value() || options.aac_keycode.has_value()) {
            return std::unexpected("`--encoding` and `--aac-keycode` are not codec encoder options");
        }
    }
    if (options.build) {
        const Format format = *options.force_type;
        if (options.quality.has_value() || options.bitrate.has_value() || options.highpass.has_value()
            || options.mode.has_value() || options.ms_stereo || options.trim_after_loop) {
            return std::unexpected("codec encoder options cannot be combined with `--build`");
        }
        if (options.alignment.has_value() && format != Format::afs && format != Format::awb
            && format != Format::cpk && format != Format::acx) {
            return std::unexpected("`--alignment` is only supported for AFS, AWB, CPK, and ACX builds");
        }
        if (options.profile.has_value() && format != Format::cpk && format != Format::sfd) {
            return std::unexpected("`--profile` is only supported for CPK and SFD builds");
        }
        if (options.version.has_value() && format != Format::awb && format != Format::sfd) {
            return std::unexpected("`--header-version` is only supported for AWB and SFD builds");
        }
        if (options.subkey.has_value() && format != Format::awb) {
            return std::unexpected("`--subkey` is only supported for AWB builds");
        }
        if (options.cipher_type.has_value() || options.aac_keycode.has_value()) {
            return std::unexpected("`--cipher-type` and `--aac-keycode` are not builder options");
        }
        if (options.key.has_value() && format != Format::cvm && format != Format::usm) {
            return std::unexpected("`--key` is only supported for CVM and USM builds");
        }
        if (options.compress && format != Format::cpk) {
            return std::unexpected("`--compress` is only supported for CPK builds");
        }
        if (!options.audio_paths.empty() && format != Format::usm && format != Format::sfd) {
            return std::unexpected("`--audio` is only supported for USM and SFD builds");
        }
        if (options.encoding.has_value() && format != Format::cpk && format != Format::csb && format != Format::usm) {
            return std::unexpected("`--encoding` is only supported for CPK, CSB, and USM builds");
        }
    }
    if (!options.mutations.empty() && !options.output_path.has_value()) {
        return std::unexpected("mutation commands require `-o`/`--output`");
    }
    if (!options.mutations.empty() && (options.build || options.encode || options.metadata_only || options.json || options.raw || options.list_only || options.encrypt || options.decrypt || !options.indexes.empty())) {
        return std::unexpected("mutation commands cannot be combined with encode/export/list/metadata/crypto selection flags");
    }
    if (options.compress && options.mutations.empty() && !options.build) {
        return std::unexpected("`--compress` is only valid with mutation/build commands");
    }
    if (options.encrypt && options.decrypt) {
        return std::unexpected("`--encrypt` and `--decrypt` cannot be combined");
    }
    if ((options.encrypt || options.decrypt) && options.raw) {
        return std::unexpected("`--raw` cannot be combined with `--encrypt` or `--decrypt`");
    }
    if (options.raw && options.cue_based) {
        return std::unexpected("`--raw` cannot be combined with cue-based extraction");
    }
    if ((options.cue_stop_at_loop ||
         options.cue_empty_hold_policy_set ||
         options.cue_loop_count != 0 ||
         !options.cue_block_loop_overrides.empty()) &&
        !options.cue_based) {
        return std::unexpected("cue loop policy options require `--cue`");
    }
    if (!options.cue_block_loop_overrides.empty() && options.indexes.empty()) {
        return std::unexpected(
            "cue block loop overrides require at least one selected cue `--index`");
    }
    if (options.json && !options.metadata_only && !options.list_only &&
        !options.recover_key) {
        return std::unexpected(
            "`--json` requires `-m`, `--list`, or `--recover-key`");
    }
    if (options.metadata_only && (options.raw || options.list_only || !options.indexes.empty() || options.encrypt || options.decrypt)) {
        return std::unexpected("`-m` cannot be combined with export/list selection flags");
    }
    if (options.list_only && options.output_path.has_value()) {
        return std::unexpected("`--list` cannot be combined with `--output`");
    }
    if (options.list_only && options.metadata_only) {
        return std::unexpected("`--list` cannot be combined with `-m`");
    }
    if (options.list_only && (options.encrypt || options.decrypt)) {
        return std::unexpected("`--list` cannot be combined with `--encrypt` or `--decrypt`");
    }
    if (options.verbose && !options.list_only) {
        return std::unexpected("`--verbose` requires `--list`");
    }
    if ((options.encrypt || options.decrypt) && !options.indexes.empty()) {
        return std::unexpected("`--index` is not valid with `--encrypt` or `--decrypt`");
    }
    return options;
}

void print_usage(std::ostream& out, bool show_identity) {
    if (show_identity) {
        out << "CriCodecs " << build_identity() << '\n';
    }
    out <<
        "Usage: cricodecs <input> [-e] [--encode|--build] [--raw|--cue] [--list] [--encrypt|--decrypt] [-m] [--json] [-q] [-f TYPE] [-o PATH]\n"
        "       cricodecs --recover-key -f hca|usm|adx|ahx|awb|acb <input> [input ...] [--json] [-q]\n"
        "                 [--index N] [--key VALUE] [--subkey VALUE] [--cipher-type VALUE] [--aac-keycode VALUE]\n"
        "                 [--encoding NAME] [--alpha PATH] [--audio PATH] [--audio-channel 0..255] [--profile NAME] [--header-version VALUE]\n"
        "                 [--quality NAME] [--bitrate BPS] [--ms-stereo] [--mode VALUE] [--highpass HZ]\n"
        "                 [--trim-after-loop] [--alignment BYTES]\n"
        "\n"
        "  -e, --export         explicit export; same as default behavior\n"
        "      --encode         encode WAV input as hca/adx/ahx; requires -f and -o\n"
        "      --build          build afs/awb/cpk/acx/csb/cvm/usm/sfd inputs\n"
        "      --alpha PATH     add an alpha-video stream to a USM build\n"
        "      --audio PATH     add ADX/HCA audio for usm, or ADX for sfd; repeatable for usm\n"
        "      --audio-channel  assign explicit USM channel per --audio; repeat for every audio input\n"
        "      --profile NAME   AHX allocation profile, or CPK/SFD build profile\n"
        "      --header-version VALUE  HCA/ADX encode version, or AWB/SFD build version\n"
        "      --quality NAME   HCA quality: highest, high, middle, low, or lowest\n"
        "      --bitrate BPS    explicit HCA bitrate; 0 keeps automatic selection\n"
        "      --ms-stereo      enable HCA mid/side stereo coding\n"
        "      --mode VALUE     ADX mode 2/3/4 or AHX mode 0x10/0x11\n"
        "      --highpass HZ    ADX high-pass frequency\n"
        "      --trim-after-loop  discard ADX samples after the final loop end\n"
        "      --alignment BYTES  AFS/AWB/CPK/ACX build alignment\n"
        "      --add SRC=DEST   add file to archive; DEST is archive path/name, AWB wave ID, or AIX segment/layer\n"
        "      --replace T=SRC  replace archive entry T with source file SRC\n"
        "      --remove T       remove archive entry T\n"
        "      --rename T=DEST  rename archive entry T where supported\n"
        "      --move FROM=TO   reorder archive entries by index\n"
        "                         AIX targets: segment:N, layer:N, or SEGMENT:LAYER for replacement\n"
        "      --compress       compress added/replaced CPK payloads\n"
        "      --raw            export raw contained/original payloads without audio decode\n"
        "      --list           list exportable items and exit\n"
        "      --verbose        include details for every listed item\n"
        "  -m                   print metadata only\n"
        "      --json           emit metadata, listing, or recovered-key output as JSON\n"
        "      --encrypt        write encrypted output; encrypts CPK UTF tables and scrambles CVM metadata\n"
        "      --decrypt        write decrypted output; restores plain CPK UTF tables or CVM metadata\n"
        "      --recover-key    recover keys; requires -f hca, usm, adx, ahx, awb, or acb\n"
        "      --independent    recover multiple inputs independently instead of asserting one shared base key\n"
        "  -o, --output         override output path/root\n"
        "                        ?i = selected entry index, ?e = entry filename, ?s = input filename\n"
        "      --index          export or list only a specific item index; repeatable\n"
        "  -q, --quiet          suppress non-error console output\n"
        "  -f, --force-type     force input parsing; selects hca/usm/adx/ahx/awb/acb recovery domain with --recover-key\n"
        "      --key            format key string or numeric keycode; CVM uses it as the scramble key\n"
        "      --subkey         numeric subkey for HCA/ADX type 9 where applicable\n"
        "      --cipher-type    target cipher/encryption type where applicable\n"
        "      --aac-keycode    AAC keycode for ACB waveform extraction\n"
        "      --encoding       text encoding override where applicable\n"
        "      --version        show version/build information\n"
        "  -h, --help           show this help text\n"
        "\n"
        "ACB cue extraction:\n"
        "      --cue            resolve, deduplicate, and render static cue paths instead of flat AWB waveforms\n"
        "      --cue --list     list unique cue-plan indexes; add --index N to show its blocks\n"
        "                        selector names/values distinguish different plans sharing one cue name\n"
        "      --cue-loop-count N  loop each infinite audio block N times (default 0)\n"
        "      --cue-block-loop-count P=N  set loops for block position P; requires --index\n"
        "      --cue-stop-at-loop  stop after the first rendered infinite block instead of advancing\n"
        "      --cue-include-empty-holds  include waveform-less infinite holds (omitted by default)\n"
        "\n"
        "Valid force types:\n"
        "  aax acb acx adx afs ahx aix awb cpk csb cvm hca sfd usm utf\n";
}


} // namespace cricodecs::cli::detail
