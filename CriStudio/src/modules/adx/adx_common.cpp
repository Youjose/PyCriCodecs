#include <QCoreApplication>
#include "modules/adx/adx_common.hpp"

#include "editor/editor_helpers.hpp"
#include "io_reader.hpp"
#include "path_text.hpp"

#include <utility>

namespace cristudio::modules::adx {

std::expected<std::vector<uint8_t>, QString> read_adx_source(
    const std::filesystem::path& path,
    std::string error_prefix
) {
    auto bytes = cricodecs::io::read_file_bytes(path, std::move(error_prefix));
    if (!bytes) {
        return std::unexpected(utf8_to_qstring(bytes.error()));
    }
    return std::move(*bytes);
}

void apply_keys(cricodecs::adx::Adx& adx, const DecryptionKeys& keys) {
    switch (keys.adx_mode) {
    case DecryptionKeys::AdxMode::Type8String:
        adx.set_key_type8(keys.adx_type8_key);
        break;
    case DecryptionKeys::AdxMode::Type9Number:
        adx.set_key_type9(keys.adx_type9_key, keys.adx_subkey);
        break;
    case DecryptionKeys::AdxMode::AhxTriplet:
        if (adx.is_ahx()) {
            adx.set_ahx_key(keys.ahx_start, keys.ahx_mult, keys.ahx_add);
        } else {
            adx.set_key_triplet(keys.ahx_start, keys.ahx_mult, keys.ahx_add);
        }
        break;
    case DecryptionKeys::AdxMode::None:
        break;
    }
}

bool has_compatible_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys) {
    if (!adx.is_encrypted()) {
        return true;
    }
    if (keys.adx_mode == DecryptionKeys::AdxMode::AhxTriplet) {
        return true;
    }
    if (adx.header().flags == 8) {
        return keys.adx_mode == DecryptionKeys::AdxMode::Type8String;
    }
    if (adx.header().flags == 9) {
        return keys.adx_mode == DecryptionKeys::AdxMode::Type9Number;
    }
    return false;
}

bool has_applicable_raw_key(const cricodecs::adx::Adx& adx, const DecryptionKeys& keys) {
    return adx.is_encrypted() && has_compatible_key(adx, keys);
}

QString payload_preview(QString label, std::span<const uint8_t> bytes) {
    QString out;
    out += QStringLiteral("%1\n").arg(std::move(label));
    out += QCoreApplication::translate("Adx.AdxCommon", "Payload bytes: %1\n").arg(static_cast<qulonglong>(bytes.size()));

    auto adx = cricodecs::adx::Adx::load(bytes);
    if (!adx) {
        out += QCoreApplication::translate("Adx.AdxCommon", "ADX parse: %1\n\n").arg(utf8_to_qstring(adx.error()));
        out += hex_preview(bytes);
        return out;
    }

    const auto& header = adx->header();
    const auto signature_text = QStringLiteral("%1").arg(header.signature, 4, 16, QLatin1Char('0')).toUpper();
    const auto flags_text = QStringLiteral("%1").arg(header.flags, 2, 16, QLatin1Char('0')).toUpper();
    out += QCoreApplication::translate("Adx.AdxCommon", "Signature: 0x%1\n").arg(signature_text);
    out += QCoreApplication::translate("Adx.AdxCommon", "Data offset: %1\n").arg(header.data_offset);
    out += QCoreApplication::translate("Adx.AdxCommon", "Encoding mode: %1%2\n")
        .arg(header.encoding_mode)
        .arg(adx->is_ahx() ? QStringLiteral(" (AHX)") : QString{});
    out += QCoreApplication::translate("Adx.AdxCommon", "Block size / bit depth: %1 / %2\n").arg(header.block_size).arg(header.bit_depth);
    out += QCoreApplication::translate("Adx.AdxCommon", "Channels: %1\n").arg(header.channels);
    out += QCoreApplication::translate("Adx.AdxCommon", "Sample rate: %1\n").arg(header.sample_rate);
    out += QCoreApplication::translate("Adx.AdxCommon", "Sample count: %1\n").arg(header.sample_count);
    out += QCoreApplication::translate("Adx.AdxCommon", "Highpass frequency: %1\n").arg(header.highpass_freq);
    out += QCoreApplication::translate("Adx.AdxCommon", "Version: %1\n").arg(header.version);
    out += QCoreApplication::translate("Adx.AdxCommon", "Flags: 0x%1\n").arg(flags_text);
    out += QCoreApplication::translate("Adx.AdxCommon", "Encrypted: %1\n").arg(adx->is_encrypted() ? QStringLiteral("yes") : QStringLiteral("no"));
    out += QCoreApplication::translate("Adx.AdxCommon", "Loop count: %1\n").arg(static_cast<qsizetype>(adx->loops().size()));
    for (const auto& loop : adx->loops()) {
        out += QCoreApplication::translate("Adx.AdxCommon", "  Loop %1: type %2, samples %3-%4, bytes %5-%6\n")
            .arg(loop.index)
            .arg(loop.type)
            .arg(loop.start_sample)
            .arg(loop.end_sample)
            .arg(loop.start_byte)
            .arg(loop.end_byte);
    }
    out += QCoreApplication::translate("Adx.AdxCommon", "\nHex preview\n");
    out += hex_preview(bytes);
    return out;
}

} // namespace cristudio::modules::adx
