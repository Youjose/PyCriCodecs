#include "cli_internal.hpp"

namespace cricodecs::cli {
using namespace detail;

namespace {

struct FormatName {
    std::string_view key;
    std::string_view label;
};

constexpr std::array format_names{
    FormatName{"aax", "AAX"}, FormatName{"acb", "ACB"},
    FormatName{"acx", "ACX"}, FormatName{"adx", "ADX"},
    FormatName{"afs", "AFS"}, FormatName{"ahx", "AHX"},
    FormatName{"aix", "AIX"}, FormatName{"awb", "AWB"},
    FormatName{"cpk", "CPK"}, FormatName{"csb", "CSB"},
    FormatName{"cvm", "CVM"}, FormatName{"hca", "HCA"},
    FormatName{"sfd", "SFD"}, FormatName{"usm", "USM"},
    FormatName{"utf", "UTF"}, FormatName{"wav", "WAV"},
    FormatName{"video", "Video"},
};
static_assert(format_names.size() == static_cast<size_t>(Format::video) + 1u);

void print_build_line(std::ostream& out) {
    out << "build: " << build_identity() << '\n';
}

} // namespace

std::string_view format_key(Format format) noexcept {
    const auto index = static_cast<size_t>(format);
    return index < format_names.size() ? format_names[index].key : "unknown";
}

std::string_view format_label(Format format) noexcept {
    const auto index = static_cast<size_t>(format);
    return index < format_names.size() ? format_names[index].label : "Unknown";
}

std::optional<Format> parse_format_key(std::string_view text) noexcept {
    const std::string lowered = lower_ascii(text);
    const auto found = std::ranges::find(format_names, lowered, &FormatName::key);
    return found == format_names.end()
        ? std::nullopt
        : std::optional{static_cast<Format>(found - format_names.begin())};
}

bool format_supported_in_cli(Format format) noexcept {
    return format != Format::wav && format != Format::video;
}

bool format_supports_default_write(Format format) noexcept {
    return format != Format::utf;
}

int run(std::span<const std::string> args, std::ostream& out, std::ostream& err) {
    auto options = parse_options(args);
    if (!options) {
        err << options.error() << '\n';
        err << "Use `cricodecs --help` for usage.\n";
        return 1;
    }

    if (options->help) {
        print_usage(out, !options->quiet);
        return 0;
    }
    if (options->show_version) {
        out << build_identity() << '\n';
        return 0;
    }

    const auto finish_action = [&](std::expected<void, std::string> action) {
        if (!action) {
            err << action.error() << '\n';
            return 1;
        }
        if (!options->quiet) {
            out << "done\n";
        }
        return 0;
    };

    if (options->recover_key) {
        for (const auto& path : options->input_paths) {
            if (!std::filesystem::exists(path)) {
                err << "input path does not exist: " << path.string() << '\n';
                return 1;
            }
            if (!std::filesystem::is_regular_file(path) && !std::filesystem::is_directory(path)) {
                err << "input path is not a regular file or directory: " << path.string() << '\n';
                return 1;
            }
        }
        const auto report_recovery = [&](auto recovered, auto print_text, auto print_json) {
            if (!recovered) {
                err << recovered.error() << '\n';
                return 1;
            }
            if (options->json) {
                print_json(out, *recovered);
                out << '\n';
            } else {
                print_text(out, *recovered);
            }
            return 0;
        };
        switch (*options->force_type) {
            case Format::acb:
            case Format::awb:
                return report_recovery(
                    perform_aac_key_recovery(options->input_paths, *options->force_type, *options),
                    print_aac_key_recovery_text,
                    print_aac_key_recovery_json
                );
            case Format::usm:
                return report_recovery(
                    perform_usm_key_recovery(options->input_paths, *options),
                    print_usm_key_recovery_text,
                    print_usm_key_recovery_json
                );
            case Format::adx:
                return report_recovery(
                    perform_adx_key_recovery(options->input_paths, *options),
                    print_adx_key_recovery_text,
                    print_adx_key_recovery_json
                );
            case Format::ahx:
                return report_recovery(
                    perform_ahx_key_recovery(options->input_paths, *options),
                    print_ahx_key_recovery_text,
                    print_ahx_key_recovery_json
                );
            default:
                return report_recovery(
                    perform_hca_key_recovery(options->input_paths, *options),
                    print_hca_key_recovery_text,
                    print_hca_key_recovery_json
                );
        }
    }

    const auto& input_path = options->input_paths.front();
    if (!std::filesystem::exists(input_path)) {
        err << "input path does not exist: " << input_path.string() << '\n';
        return 1;
    }
    if (!options->build && !std::filesystem::is_regular_file(input_path)) {
        err << "input path is not a regular file: " << input_path.string() << '\n';
        return 1;
    }

    if (options->build || options->encode) {
        if (!options->quiet) {
            print_build_line(out);
            out << "format: " << format_key(*options->force_type) << '\n';
            out << "output: " << options->output_path->string() << '\n';
        }
        return finish_action(options->build
            ? perform_build_action(input_path, *options->output_path, *options)
            : perform_encode_action(input_path, *options->output_path, *options));
    }

    auto loaded = load_best_effort(input_path, *options);
    if (!loaded) {
        err << loaded.error() << '\n';
        return 1;
    }

    if (!options->mutations.empty()) {
        const auto& output_path = *options->output_path;
        if (!options->quiet) {
            out << "format: " << format_key(loaded->format) << '\n';
            out << "output: " << output_path.string() << '\n';
        }
        return finish_action(save_mutated_document(*loaded, output_path, *options));
    }

    const bool metadata_only = options->metadata_only || loaded->format == Format::utf;

    if (metadata_only && options->output_path.has_value()) {
        err << "`--output` is not valid for metadata-only mode\n";
        return 1;
    }

    if (metadata_only) {
        if (options->json) {
            print_metadata_json(out, loaded->format, loaded->document);
            out << '\n';
        } else {
            print_metadata_text(out, loaded->format, loaded->document);
        }
        return 0;
    }

    if (options->list_only) {
        auto listing = collect_export_items(*loaded, *options);
        if (!listing) {
            err << listing.error() << '\n';
            return 1;
        }
        if (options->json) {
            print_item_list_json(out, *listing);
            out << '\n';
        } else {
            print_item_list(out, *listing);
        }
        return 0;
    }

    const bool top_level_audio_export =
        loaded->format == Format::adx || loaded->format == Format::ahx ||
        loaded->format == Format::hca || loaded->format == Format::aax;

    if (loaded->format == Format::utf) {
        err << "UTF has no export action yet\n";
        return 1;
    }

    if (!options->quiet) {
        print_build_line(out);
        out << "format: " << format_key(loaded->format) << '\n';
    }

    std::expected<void, std::string> action;
    if (options->encrypt || options->decrypt) {
        const auto output_path = options->output_path.value_or(
            crypto_output_path(input_path, options->encrypt ? "_encrypted" : "_decrypted")
        );
        if (output_path.empty()) {
            err << "could not derive an output path\n";
            return 1;
        }
        if (!options->quiet) {
            out << "output: " << output_path.string() << '\n';
        }
        action = perform_crypto_action(*loaded, output_path, *options);
    } else if (top_level_audio_export && (loaded->format != Format::aax || !options->raw || options->indexes.empty())) {
        if (!options->indexes.empty() && !(options->raw && loaded->format != Format::aax)) {
            err << "`--index` is only valid for multi-item exports or raw AAX segment export\n";
            return 1;
        }
        const auto output_path =
            options->output_path.value_or(default_output_path(input_path, loaded->format, options->raw));
        if (output_path.empty()) {
            err << "could not derive an output path\n";
            return 1;
        }
        if (!options->quiet) {
            out << "output: " << output_path.string() << '\n';
        }
        action = perform_audio_export_action(*loaded, output_path, *options);
    } else {
        const auto output_root =
            options->output_path.value_or(default_output_path(input_path, loaded->format, options->raw));
        if (output_root.empty()) {
            err << "could not derive an output path\n";
            return 1;
        }
        if (!options->quiet) {
            out << "output: " << output_root.string() << '\n';
        }
        action = perform_multi_item_export(*loaded, input_path, *options);
    }
    return finish_action(std::move(action));
}


} // namespace cricodecs::cli
