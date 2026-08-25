#include "shared/i18n.hpp"
#include <QCoreApplication>
#include "modules/aax/aax_edit.hpp"

#include "aax_container.hpp"
#include "modules/adx/adx_common.hpp"
#include "path_text.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace cristudio::modules::aax {
namespace {

void push_log(const BuildLogCallback& log, QString message) {
    if (log) {
        log(std::move(message));
    }
}

} // namespace

std::expected<void, QString> build_from_adx_segments(BuildConfig config, BuildLogCallback log) {
    if (config.output_path.empty()) {
        return std::unexpected(QCoreApplication::translate("Aax.AaxEdit", "Choose an output path."));
    }
    if (config.segments.empty()) {
        return std::unexpected(QCoreApplication::translate("Aax.AaxEdit", "Choose at least one ADX source."));
    }

    std::vector<cricodecs::aax::AaxBuildEntry> entries;
    entries.reserve(config.segments.size());
    for (size_t i = 0; i < config.segments.size(); ++i) {
        const auto& segment_path = config.segments[i];
        push_log(log, QCoreApplication::translate("Aax.AaxEdit", "Reading AAX segment %1: %2")
            .arg(i)
            .arg(path_to_qstring(segment_path)));
        auto bytes = ::cristudio::modules::adx::read_adx_source(
            segment_path,
            cristudio::i18n::translate_utf8("Aax.AaxEdit", "AAX build failed"));
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        entries.push_back(cricodecs::aax::AaxBuildEntry{
            .adx_data = std::move(*bytes),
            .loop_segment = config.mark_last_segment_as_loop && i + 1 == config.segments.size()
        });
    }

    push_log(log, QCoreApplication::translate(
        "Aax.AaxEdit",
        "Building AAX wrapper with %n ADX segment(s).",
        nullptr,
        static_cast<int>(entries.size())));
    if (auto result = cricodecs::aax::AaxContainer::build_to_file(entries, config.output_path); !result) {
        return std::unexpected(utf8_to_qstring(result.error()));
    }
    push_log(log, QCoreApplication::translate("Aax.AaxEdit", "AAX build wrote %1.").arg(path_to_qstring(config.output_path)));
    return {};
}

std::expected<std::vector<uint8_t>, std::string> build_session_bytes(cricodecs::aax::AaxContainer& aax) {
    return aax.save();
}

std::vector<TransformDetailRow> build_job_detail_rows() {
    return {
        {QStringLiteral("Job"), QCoreApplication::translate("Aax.AaxEdit", "Build an AAX UTF wrapper from ADX/AHX segments")},
        {QStringLiteral("Input"), QCoreApplication::translate("Aax.AaxEdit", "one ADX/AHX file per segment line")},
        {QStringLiteral("Validation"), QCoreApplication::translate("Aax.AaxEdit", "native AAX build checks ADX headers and shared channel/rate")},
        {QCoreApplication::translate("Aax.AaxEdit", "Loop option"), QCoreApplication::translate("Aax.AaxEdit", "mark the last segment with lpflg")},
        {QStringLiteral("Output"), QStringLiteral(".aax")}
    };
}

std::vector<TransformDetailRow> detail_rows(const cricodecs::aax::AaxContainer& aax) {
    std::vector<TransformDetailRow> rows;
    rows.push_back({QCoreApplication::translate("Aax.AaxEdit", "UTF table"), utf8_to_qstring(std::string(aax.name()))});
    rows.push_back({QCoreApplication::translate("Aax.AaxEdit", "Segments"), QString::number(aax.segment_count())});
    rows.push_back({QCoreApplication::translate("Aax.AaxEdit", "Channels"), QString::number(aax.channels())});
    rows.push_back({QCoreApplication::translate("Aax.AaxEdit", "Sample rate"), QString::number(aax.sample_rate())});
    rows.push_back({QCoreApplication::translate("Aax.AaxEdit", "Sample count"), QString::number(aax.sample_count())});
    rows.push_back({QCoreApplication::translate("Aax.AaxEdit", "Loop segments"), aax.has_loop_segments() ? QCoreApplication::translate("Aax.AaxEdit", "yes") : QCoreApplication::translate("Aax.AaxEdit", "no")});
    for (uint32_t index = 0; index < aax.segment_count(); ++index) {
        const auto& segment = aax.segment(index);
        rows.push_back({
            QCoreApplication::translate("Aax.AaxEdit", "Segment %1").arg(index),
            QCoreApplication::translate("Aax.AaxEdit", "row %1, size %2, samples %3, loop %4")
                .arg(segment.row_index)
                .arg(segment.data_size)
                .arg(segment.sample_count)
                .arg(segment.loop_segment ? QCoreApplication::translate("Aax.AaxEdit", "yes") : QCoreApplication::translate("Aax.AaxEdit", "no")),
            1,
            static_cast<int>(index)
        });
    }
    return rows;
}

std::expected<QString, QString> segment_payload_preview(
    const cricodecs::aax::AaxContainer& aax,
    int index
) {
    if (index < 0) {
        return std::unexpected(QCoreApplication::translate("Aax.AaxEdit", "AAX segment preview failed: index out of range"));
    }
    auto data = aax.segment_data(static_cast<uint32_t>(index));
    if (!data) {
        return std::unexpected(QCoreApplication::translate("Aax.AaxEdit", "AAX segment preview failed: %1").arg(utf8_to_qstring(data.error())));
    }
    return ::cristudio::modules::adx::payload_preview(QCoreApplication::translate("Aax.AaxEdit", "AAX segment %1 ADX payload").arg(index), *data);
}

} // namespace cristudio::modules::aax
