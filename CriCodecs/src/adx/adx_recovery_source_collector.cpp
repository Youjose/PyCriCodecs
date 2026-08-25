#include "adx_recovery_source_collector.hpp"

#include "../aax/aax_container.hpp"
#include "../acb/acb_container.hpp"
#include "../awb/awb_container.hpp"
#include "../cpk/cpk_container.hpp"
#include "../csb/csb_container.hpp"

#include <algorithm>
#include <string_view>

namespace cricodecs::adx {
namespace {

constexpr size_t MaxContainerDepth = 6;

[[nodiscard]] bool has_magic(std::span<const uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), bytes.begin());
}

[[nodiscard]] std::expected<void, std::string> append_payload(
    std::span<const uint8_t> bytes,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources);

template <typename ReadEntry>
[[nodiscard]] std::expected<void, std::string> append_entries(
    size_t count,
    std::string_view entry_name,
    ReadEntry read_entry,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources
) {
    for (size_t index = 0; index < count; ++index) {
        auto payload = read_entry(index);
        if (!payload) {
            return std::unexpected(std::string(entry_name) + " " + std::to_string(index) +
                " could not be read: " + payload.error());
        }
        if (auto appended = append_payload(*payload, kind, depth + 1u, sources); !appended) {
            return appended;
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_awb(
    const awb::AwbContainer& archive,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources
) {
    return append_entries(archive.file_count(), "AWB entry",
        [&](size_t index) { return archive.file_data(static_cast<uint32_t>(index)); },
        kind, depth, sources);
}

[[nodiscard]] std::expected<void, std::string> append_csb(
    const csb::CsbContainer& archive,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources
) {
    return append_entries(archive.stream_count(), "CSB stream",
        [&](size_t index) { return archive.stream_data(static_cast<uint32_t>(index)); },
        kind, depth, sources);
}

[[nodiscard]] std::expected<void, std::string> append_aax(
    const aax::AaxContainer& wrapper,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources
) {
    return append_entries(wrapper.segment_count(), "AAX segment",
        [&](size_t index) { return wrapper.segment_data(static_cast<uint32_t>(index)); },
        kind, depth, sources);
}

[[nodiscard]] std::expected<void, std::string> append_cpk(
    const cpk::Cpk& archive,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources
) {
    return append_entries(archive.file_count(), "CPK entry",
        [&](size_t index) { return archive.file_bytes(index); }, kind, depth, sources);
}

[[nodiscard]] std::expected<void, std::string> append_payload(
    std::span<const uint8_t> bytes,
    RecoveryStreamKind kind,
    size_t depth,
    std::vector<std::vector<uint8_t>>& sources
) {
    if (depth > MaxContainerDepth) {
        return std::unexpected("ADX-family recovery exceeded the supported container nesting depth");
    }

    const auto codec = awb::probe_entry_codec(bytes);
    const auto requested = kind == RecoveryStreamKind::Ahx
        ? awb::EntryCodec::Ahx
        : awb::EntryCodec::Adx;
    if (codec == requested) {
        if (bytes.size() > 19u && (bytes[19] == 8u || bytes[19] == 9u)) {
            sources.emplace_back(bytes.begin(), bytes.end());
        }
        return {};
    }
    if (codec == awb::EntryCodec::Adx || codec == awb::EntryCodec::Ahx) {
        return {};
    }

    if (has_magic(bytes, "AFS2")) {
        auto archive = awb::AwbContainer::load(bytes);
        if (!archive) {
            return std::unexpected(archive.error());
        }
        return append_awb(*archive, kind, depth, sources);
    }
    if (has_magic(bytes, "CPK ")) {
        auto archive = cpk::Cpk::load(bytes);
        if (!archive) {
            return std::unexpected(archive.error());
        }
        return append_cpk(*archive, kind, depth, sources);
    }
    if (has_magic(bytes, "@UTF")) {
        if (auto wrapper = aax::AaxContainer::load(bytes);
            wrapper && wrapper->segment_count() != 0u) {
            return append_aax(*wrapper, kind, depth, sources);
        }
        if (auto archive = csb::CsbContainer::load(bytes);
            archive && archive->stream_count() != 0u) {
            return append_csb(*archive, kind, depth, sources);
        }
        if (auto cue_sheet = acb::AcbContainer::load(bytes);
            cue_sheet && cue_sheet->has_embedded_awb()) {
            auto archive = cue_sheet->load_awb();
            if (!archive) {
                return std::unexpected(archive.error());
            }
            return append_awb(*archive, kind, depth, sources);
        }
    }
    return {};
}

} // namespace

std::expected<std::vector<std::vector<uint8_t>>, std::string>
collect_recovery_streams(
    const std::filesystem::path& path,
    RecoveryStreamKind kind
) {
    std::vector<std::vector<uint8_t>> sources;

    if (auto wrapper = aax::AaxContainer::load(path);
        wrapper && wrapper->segment_count() != 0u) {
        if (auto appended = append_aax(*wrapper, kind, 0, sources); !appended) {
            return std::unexpected(appended.error());
        }
        return sources;
    }
    if (auto archive = awb::AwbContainer::load(path); archive) {
        if (auto appended = append_awb(*archive, kind, 0, sources); !appended) {
            return std::unexpected(appended.error());
        }
        return sources;
    }
    if (auto cue_sheet = acb::AcbContainer::load(path); cue_sheet) {
        if (auto archive = cue_sheet->load_awb(); archive) {
            if (auto appended = append_awb(*archive, kind, 0, sources); !appended) {
                return std::unexpected(appended.error());
            }
            return sources;
        }
    }
    if (auto archive = csb::CsbContainer::load(path);
        archive && archive->stream_count() != 0u) {
        if (auto appended = append_csb(*archive, kind, 0, sources); !appended) {
            return std::unexpected(appended.error());
        }
        return sources;
    }
    if (auto archive = cpk::Cpk::load(path); archive) {
        if (auto appended = append_cpk(*archive, kind, 0, sources); !appended) {
            return std::unexpected(appended.error());
        }
        return sources;
    }

    auto bytes = io::read_file_bytes(path, "ADX-family key recovery failed");
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (auto appended = append_payload(*bytes, kind, 0, sources); !appended) {
        return std::unexpected(appended.error());
    }
    return sources;
}

std::expected<std::vector<std::vector<uint8_t>>, std::string>
collect_recovery_streams(
    std::span<const uint8_t> bytes,
    RecoveryStreamKind kind
) {
    std::vector<std::vector<uint8_t>> sources;
    if (auto appended = append_payload(bytes, kind, 0, sources); !appended) {
        return std::unexpected(appended.error());
    }
    return sources;
}

} // namespace cricodecs::adx
