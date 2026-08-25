#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

namespace {

constexpr io::FourCC CpkMagic{"CPK "};
constexpr io::FourCC CriDataMagic{"CRID"};
constexpr io::FourCC SofdecHeaderMagic{"SFSH"};
constexpr io::FourCC AwbMagic{"AFS2"};
constexpr io::FourCC AfsMagic{"AFS\0"};
constexpr io::FourCC AixMagic{"AIXF"};
constexpr io::FourCC UtfMagic{"@UTF"};
constexpr io::FourCC MpegPackMagic{0x00u, 0x00u, 0x01u, 0xBAu};
constexpr io::FourCC HcaMagic{"HCA\0"};
constexpr io::FourCC RiffMagic{"RIFF"};
constexpr io::FourCC WaveMagic{"WAVE"};

} // namespace

template <typename T>
[[nodiscard]] std::expected<T, std::string> wrap_expected(std::expected<T, std::string>&& result) {
    if (!result) {
        return std::unexpected(result.error());
    }
    return std::move(result).value();
}

[[nodiscard]] std::expected<LoadedDocument, std::string> load_document(
    const std::filesystem::path& path,
    Format format,
    const Options& options
) {
    const auto encoding = encoding_options(options);
    switch (format) {
        case Format::aax:
            return wrap_expected(aax::AaxContainer::load(path));
        case Format::acb:
            return wrap_expected(acb::AcbContainer::load(path, encoding));
        case Format::acx:
            return wrap_expected(acx::AcxContainer::load(path));
        case Format::adx: {
            auto loaded = adx::Adx::load(path);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            if (loaded->is_ahx()) {
                return std::unexpected("ADX load failed: source is AHX; force type `ahx` if that is intended");
            }
            return LoadedDocument(std::move(*loaded));
        }
        case Format::ahx: {
            auto loaded = adx::Adx::load(path);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            if (!loaded->is_ahx()) {
                return std::unexpected("AHX load failed: source is not AHX");
            }
            return LoadedDocument(std::move(*loaded));
        }
        case Format::afs:
            return wrap_expected(afs::AfsContainer::load(path));
        case Format::aix: {
            aix::Aix archive;
            if (auto result = archive.load(path); !result) {
                return std::unexpected(result.error());
            }
            return LoadedDocument(std::move(archive));
        }
        case Format::awb:
            return wrap_expected(awb::AwbContainer::load(path));
        case Format::cpk:
            return wrap_expected(cpk::Cpk::load(path, encoding));
        case Format::csb:
            return wrap_expected(csb::CsbContainer::load(path, encoding));
        case Format::cvm:
            return wrap_expected(cvm::CvmContainer::load(path, options.key.value_or("")));
        case Format::hca:
            return wrap_expected(hca::Hca::load(path));
        case Format::sfd:
            return wrap_expected(sfd::SfdContainer::load(path));
        case Format::usm: {
            usm::UsmReader reader;
            if (options.key.has_value()) {
                auto key = parse_u64(*options.key, "--key");
                if (!key) {
                    return std::unexpected(key.error());
                }
                reader.set_key(*key);
            }
            reader.set_encoding(encoding);
            if (auto result = reader.load(path); !result) {
                return std::unexpected(result.error());
            }
            return LoadedDocument(std::move(reader));
        }
        case Format::utf: {
            auto loaded = utf::UtfTable::load(path);
            if (!loaded) {
                return std::unexpected(loaded.error());
            }
            if (options.encoding.has_value()) {
                loaded->set_text_encoding(*options.encoding);
            }
            return LoadedDocument(std::move(*loaded));
        }
        case Format::wav:
            return std::unexpected("WAV is not a valid CLI input format");
        case Format::video:
            return std::unexpected("raw video streams are not valid CLI input formats");
    }
    return std::unexpected("unsupported format");
}

[[nodiscard]] std::vector<Format> probe_order_for_path(
    const std::filesystem::path& path,
    bool include_riff_wave = false
) {
    auto detected = sniff_format_order(path, include_riff_wave);
    if (!detected.empty()) {
        return detected;
    }
    return fallback_probe_order(include_riff_wave);
}

[[nodiscard]] std::expected<LoadedResult, std::string> load_best_effort(
    const std::filesystem::path& path,
    const Options& options
) {
    if (options.force_type.has_value()) {
        auto forced = *options.force_type;
        if (!format_supported_in_cli(forced)) {
            return std::unexpected(
                std::string("forced type `") + std::string(format_key(forced)) + "` is not a valid CLI input type"
            );
        }
        auto loaded = load_document(path, forced, options);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        return LoadedResult{
            .format = forced,
            .document = std::move(*loaded),
        };
    }

    std::error_code file_size_error;
    const uintmax_t file_size = std::filesystem::file_size(path, file_size_error);
    const bool oversized_unknown = file_size_error || file_size > kUnknownPathProbeLimit;

    auto candidates = probe_order_for_path(path, false);
    if (candidates.empty()) {
        if (oversized_unknown) {
            return std::unexpected("could not detect supported format from file header");
        }
        candidates = fallback_probe_order(false);
    }

    std::optional<std::pair<Format, std::string>> best_failure;
    int best_score = std::numeric_limits<int>::min();

    for (const auto format : candidates) {
        if (!format_supported_in_cli(format)) {
            continue;
        }
        auto loaded = load_document(path, format, options);
        if (loaded) {
            return LoadedResult{
                .format = format,
                .document = std::move(*loaded),
            };
        }
        const int score = error_suspicion(loaded.error());
        if (score > best_score) {
            best_failure = std::pair{format, loaded.error()};
            best_score = score;
        }
    }

    if (!best_failure) {
        return std::unexpected("could not detect supported format");
    }

    std::string message = "could not detect supported format";
    if (best_score >= 2) {
        message += "; most suspicious failure from ";
        message += format_key(best_failure->first);
        message += ": ";
        message += best_failure->second;
    }
    return std::unexpected(std::move(message));
}


} // namespace cricodecs::cli::detail

namespace cricodecs::cli {
using namespace detail;

int error_suspicion(std::string_view message) {
    const std::string lowered = lower_ascii(message);
    const auto contains = [&](std::string_view text) {
        return lowered.find(text) != std::string::npos;
    };
    const auto contains_any = [&](std::initializer_list<std::string_view> texts) {
        return std::ranges::any_of(texts, contains);
    };

    if (contains("invalid ") && contains_any({"magic", "signature", "header marker"})) {
        return 0;
    }

    if (contains_any({
            "too small",
            "too short",
            "invalid magic",
            "invalid signature",
            "invalid riff/wave header",
            "expected utf table name",
            "expected root table name",
            "expected sound_element table name",
            "expected table name",
            "invalid table name",
            "unexpected chunk magic",
            "expected chunk magic",
            "chunk declares an invalid size",
            "chunk declares an invalid payload offset",
            "not a valid utf",
            "invalid utf",
            "missing adx signature",
            "not an acb header",
            "expected aax",
            "expected acb",
            "does not start with",
            "could not find mpeg pack header",
            "source is not",
        })) {
        return 0;
    }

    const bool high_action = contains_any({
        "parse failed:",
        "frame read failed:",
        "decompress failed:",
        "decrypt failed:",
        "read failed:",
        "decode failed:",
        "demux failed:",
    });
    const bool deep_parse = contains_any({
        "chunk",
        "payload",
        "section",
        "subtable",
        "waveformtable",
        "table has no columns",
        "missing or invalid",
        "out of bounds",
        "out of range",
        "truncated",
        "extends past",
        "checksum",
        "malformed",
        "unexpected end",
        "unsupported header",
        "unsupported version",
        "unsupported filesystem",
        "unsupported logical block",
        "vbr",
        "fmt",
        "comp",
        "dec",
        "loop",
        "cipher",
    });
    if (high_action && deep_parse) {
        return 3;
    }
    if (contains("load failed:") && contains("could not parse") && deep_parse) {
        return 3;
    }
    if (contains_any({
            "index is out of range",
            "file is not open",
            "could not open input",
            "could not open memory buffer",
            "could not read input file",
        })) {
        return 0;
    }
    if (deep_parse || contains("could not inspect") || contains("could not map")) {
        return 2;
    }
    return 1;
}

std::vector<Format> fallback_probe_order(bool include_riff_wave) {
    std::vector<Format> order{
        Format::cvm,
        Format::cpk,
        Format::usm,
        Format::awb,
        Format::afs,
        Format::aix,
        Format::csb,
        Format::acb,
        Format::aax,
        Format::sfd,
        Format::acx,
        Format::adx,
        Format::hca,
        Format::utf,
    };
    if (include_riff_wave) {
        order.push_back(Format::wav);
    }
    return order;
}

std::vector<Format> sniff_format_order(std::span<const uint8_t> bytes, bool include_riff_wave) {
    std::vector<Format> order;
    if (has_cvm_header(bytes)) {
        push_unique(order, Format::cvm);
    }
    if (has_magic_at(bytes, 0, CpkMagic)) {
        push_unique(order, Format::cpk);
    }
    if (has_magic_at(bytes, 0, CriDataMagic) || has_magic_at(bytes, 0, SofdecHeaderMagic)) {
        push_unique(order, Format::usm);
    }
    if (has_magic_at(bytes, 0, AwbMagic)) {
        push_unique(order, Format::awb);
    }
    if (has_magic_at(bytes, 0, AfsMagic)) {
        push_unique(order, Format::afs);
    }
    if (has_magic_at(bytes, 0, AixMagic)) {
        push_unique(order, Format::aix);
    }
    if (has_magic_at(bytes, 0, UtfMagic)) {
        push_unique(order, Format::csb);
        push_unique(order, Format::acb);
        push_unique(order, Format::aax);
        push_unique(order, Format::utf);
    }
    if (has_magic_at(bytes, 0, MpegPackMagic)) {
        push_unique(order, Format::sfd);
    }
    if (bytes.size() >= 4 && bytes[0] == 0x80 && bytes[1] == 0x00) {
        push_unique(order, Format::adx);
    }
    if (bytes.size() >= 4 &&
        (io::read_be<uint32_t>(bytes.data()) & 0x7F7F7F7Fu) == HcaMagic.be_value()) {
        push_unique(order, Format::hca);
    }
    if (include_riff_wave &&
        has_magic_at(bytes, 0, RiffMagic) && has_magic_at(bytes, 8, WaveMagic)) {
        push_unique(order, Format::wav);
    }
    if (looks_like_acx(bytes)) {
        push_unique(order, Format::acx);
    }
    return order;
}

std::vector<Format> sniff_format_order(const std::filesystem::path& path, bool include_riff_wave) {
    std::array<uint8_t, 0x804> header{};
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    const auto read_size = static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    return sniff_format_order(std::span<const uint8_t>(header.data(), read_size), include_riff_wave);
}


} // namespace cricodecs::cli
