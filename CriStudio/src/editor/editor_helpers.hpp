#pragma once

#include "document/document_types.hpp"
#include "shared/embedded_preview_helpers.hpp"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class QLabel;
class QWidget;

namespace cricodecs::cpk {
enum class CpkMode;
enum class CpkPreset : int32_t;
}

namespace cristudio {

struct EditorOpenRequest;

enum class ArchiveKind {
    None,
    Afs,
    Awb,
    Acx,
    Cpk,
    Cvm
};

enum class TransformKind {
    None,
    AudioEncode,
    MediaBuild,
    Adx,
    Hca,
    Aax,
    Aix,
    Usm,
    Sfd,
    Csb,
    Acb
};

struct BuildJobLog {
    std::mutex mutex;
    QStringList pending;
};

struct EditorBuildResult {
    bool handled = false;
    std::vector<uint8_t> bytes;
    QString log_message;
    QString warning_title;
    QString error;
};

void push_job_log(const std::shared_ptr<BuildJobLog>& log, QString message);
[[nodiscard]] QStringList take_job_logs(const std::shared_ptr<BuildJobLog>& log);

[[nodiscard]] QString editor_label(std::string_view text, QString fallback = QStringLiteral("Untitled"));
[[nodiscard]] QString editor_format_id(const EditorOpenRequest& request);
[[nodiscard]] QString editor_format_label(const EditorOpenRequest& request);
[[nodiscard]] QString safe_output_name(QString name, QString fallback_suffix);
[[nodiscard]] QString ensure_output_suffix(QString text, QString suffix);
[[nodiscard]] QString build_output_base_name(const QString& title);
[[nodiscard]] QString bytes_to_hex(std::span<const uint8_t> bytes);
[[nodiscard]] QString compact_hex_preview(
    std::span<const uint8_t> bytes,
    const char* translation_context,
    size_t max_bytes = 4096);
[[nodiscard]] EditorBuildResult finish_editor_build(
    std::expected<std::vector<uint8_t>, std::string> built,
    const char* translation_context,
    const char* failure_message,
    const char* success_message);

[[nodiscard]] std::expected<std::vector<uint8_t>, QString> read_file_bytes(
    const std::filesystem::path& path
);
[[nodiscard]] std::expected<void, QString> write_file_bytes(
    const std::filesystem::path& path,
    std::span<const uint8_t> bytes
);

[[nodiscard]] QString archive_kind_name(ArchiveKind kind);
[[nodiscard]] QString transform_kind_name(TransformKind kind);
[[nodiscard]] QString cpk_preset_name(cricodecs::cpk::CpkPreset preset);
[[nodiscard]] QString cpk_mode_name(cricodecs::cpk::CpkMode mode);

[[nodiscard]] std::expected<void, QString> extract_archive_bytes(
    ArchiveKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_dir
);
[[nodiscard]] std::expected<void, QString> transform_decode_to_wav(
    TransformKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_path,
    DecryptionKeys keys
);
[[nodiscard]] std::expected<void, QString> transform_write_bytes(
    TransformKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_path,
    DecryptionKeys keys,
    QString action
);
[[nodiscard]] std::expected<void, QString> transform_extract_all(
    TransformKind kind,
    std::vector<uint8_t> bytes,
    const std::filesystem::path& output_dir
);

} // namespace cristudio
