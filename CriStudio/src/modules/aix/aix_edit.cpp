#include "shared/i18n.hpp"
#include <QCoreApplication>
#include "modules/aix/aix_edit.hpp"

#include "modules/adx/adx_common.hpp"
#include "path_text.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace cristudio::modules::aix {
namespace {

void push_log(const BuildLogCallback& log, QString message) {
    if (log) {
        log(std::move(message));
    }
}

} // namespace

std::expected<void, std::string> extract_all(
    std::span<const uint8_t> bytes,
    const std::filesystem::path& output_dir
) {
    cricodecs::aix::Aix aix;
    if (auto result = aix.load(bytes); !result) {
        return std::unexpected(result.error());
    }
    return aix.extract_all(output_dir);
}

std::expected<std::vector<uint8_t>, std::string> rebuild_session_bytes(const cricodecs::aix::Aix& aix) {
    return aix.save();
}

std::expected<void, QString> build_from_adx_segments(BuildConfig config, BuildLogCallback log) {
    if (config.output_path.empty()) {
        return std::unexpected(QCoreApplication::translate("Aix.AixEdit", "Choose an output path."));
    }
    if (config.segments.empty()) {
        return std::unexpected(QCoreApplication::translate("Aix.AixEdit", "Choose at least one ADX source."));
    }

    std::vector<cricodecs::aix::AixBuildSegment> segments;
    segments.reserve(config.segments.size());
    for (size_t segment_index = 0; segment_index < config.segments.size(); ++segment_index) {
        const auto& source_segment = config.segments[segment_index];
        if (source_segment.empty()) {
            return std::unexpected(QCoreApplication::translate("Aix.AixEdit", "AIX segment %1 has no layer files.").arg(segment_index));
        }
        cricodecs::aix::AixBuildSegment segment;
        segment.layer_adx_data.reserve(source_segment.size());
        for (size_t layer_index = 0; layer_index < source_segment.size(); ++layer_index) {
            const auto& path = source_segment[layer_index];
            push_log(log, QCoreApplication::translate("Aix.AixEdit", "Reading AIX segment %1 layer %2: %3")
                .arg(segment_index)
                .arg(layer_index)
                .arg(path_to_qstring(path)));
            auto bytes = ::cristudio::modules::adx::read_adx_source(
                path,
                cristudio::i18n::translate_utf8("Aix.AixEdit", "AIX build failed"));
            if (!bytes) {
                return std::unexpected(bytes.error());
            }
            segment.layer_adx_data.push_back(std::move(*bytes));
        }
        segments.push_back(std::move(segment));
    }

    push_log(log, QCoreApplication::translate(
        "Aix.AixEdit",
        "Building AIX with %n segment(s).",
        nullptr,
        static_cast<int>(segments.size())));
    if (auto result = cricodecs::aix::Aix::build_to_file(segments, config.output_path); !result) {
        return std::unexpected(utf8_to_qstring(result.error()));
    }
    push_log(log, QCoreApplication::translate("Aix.AixEdit", "AIX build wrote %1.").arg(path_to_qstring(config.output_path)));
    return {};
}

std::vector<TransformDetailRow> build_job_detail_rows() {
    return {
        {QStringLiteral("Job"), QCoreApplication::translate("Aix.AixEdit", "Build an AIX layered ADX container")},
        {QStringLiteral("Input"), QCoreApplication::translate("Aix.AixEdit", "one segment per line, semicolon-separated layer files")},
        {QStringLiteral("Validation"), QCoreApplication::translate("Aix.AixEdit", "native AIX build checks segment/layer ADX compatibility")},
        {QStringLiteral("Layering"), QCoreApplication::translate("Aix.AixEdit", "each segment may contain one or more ADX layers")},
        {QStringLiteral("Output"), QStringLiteral(".aix")}
    };
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::aix::Aix& aix) {
    std::vector<TransformDetailRow> rows;
    rows.push_back({QCoreApplication::translate("Aix.AixEdit", "Segments"), QString::number(static_cast<qsizetype>(aix.segments().size()))});
    rows.push_back({QCoreApplication::translate("Aix.AixEdit", "Layers"), QString::number(static_cast<qsizetype>(aix.layers().size()))});
    rows.push_back({QCoreApplication::translate("Aix.AixEdit", "Total samples"), QString::number(static_cast<qulonglong>(aix.total_sample_count()))});
    if (aix.inferred_loop()) {
        const auto& loop = *aix.inferred_loop();
        rows.push_back({
            QCoreApplication::translate("Aix.AixEdit", "Inferred loop"),
            QCoreApplication::translate("Aix.AixEdit", "segments %1-%2, samples %3-%4")
                .arg(static_cast<qulonglong>(loop.start_segment))
                .arg(static_cast<qulonglong>(loop.end_segment))
                .arg(static_cast<qulonglong>(loop.start_sample))
                .arg(static_cast<qulonglong>(loop.end_sample))
        });
    } else {
        rows.push_back({QCoreApplication::translate("Aix.AixEdit", "Inferred loop"), QCoreApplication::translate("Aix.AixEdit", "no")});
    }

    for (size_t index = 0; index < aix.segments().size(); ++index) {
        const auto& segment = aix.segments()[index];
        rows.push_back({
            QCoreApplication::translate("Aix.AixEdit", "Segment %1").arg(static_cast<qulonglong>(index)),
            QCoreApplication::translate("Aix.AixEdit", "offset 0x%1, size %2, samples %3, rate %4")
                .arg(segment.offset, 0, 16)
                .arg(segment.size)
                .arg(segment.sample_count)
                .arg(segment.sample_rate),
            segment_row_kind,
            static_cast<int>(index)
        });
    }
    for (size_t layer = 0; layer < aix.layers().size(); ++layer) {
        const auto& layer_info = aix.layers()[layer];
        rows.push_back({
            QCoreApplication::translate("Aix.AixEdit", "Layer %1").arg(static_cast<qulonglong>(layer)),
            QCoreApplication::translate("Aix.AixEdit", "rate %1, channels %2")
                .arg(layer_info.sample_rate)
                .arg(layer_info.channel_count),
            layer_row_kind,
            static_cast<int>(layer)
        });
        for (size_t segment = 0; segment < aix.segments().size(); ++segment) {
            rows.push_back({
                QCoreApplication::translate("Aix.AixEdit", "Layer %1 segment %2")
                    .arg(static_cast<qulonglong>(layer))
                    .arg(static_cast<qulonglong>(segment)),
                QCoreApplication::translate("Aix.AixEdit", "ADX payload"),
                payload_row_kind,
                static_cast<int>(segment),
                static_cast<int>(layer)
            });
        }
    }
    return rows;
}

std::expected<QString, QString> segment_payload_preview(
    const cricodecs::aix::Aix& aix,
    int index,
    int layer
) {
    if (index < 0 || layer < 0) {
        return std::unexpected(QCoreApplication::translate("Aix.AixEdit", "AIX payload preview failed: index out of range"));
    }
    auto data = aix.segment_bytes(static_cast<size_t>(index), static_cast<size_t>(layer));
    if (!data) {
        return std::unexpected(QCoreApplication::translate("Aix.AixEdit", "AIX payload preview failed: %1").arg(utf8_to_qstring(data.error())));
    }
    return ::cristudio::modules::adx::payload_preview(
        QCoreApplication::translate("Aix.AixEdit", "AIX segment %1 layer %2 ADX payload").arg(index).arg(layer),
        std::span<const uint8_t>(data->data(), data->size())
    );
}

} // namespace cristudio::modules::aix
