#pragma once
/**
 * @file cvm_build_script.hpp
 * @brief Parser for the `.cvs` / ROFSBLD build-script subset.
 *
 * Supports the `.cvs` directives used by the CVM builder.
 */

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cricodecs::cvm {

struct CvmBuildScriptFile {
    uint32_t index = 0;
    std::filesystem::path archive_path;
    std::filesystem::path source_path;
};

struct CvmBuildScriptExportFile {
    std::filesystem::path archive_path;
    std::filesystem::path source_path;
};

struct CvmBuildScriptExport {
    std::string disc_name;
    std::string recording_date;
    std::string media = "DVD";
    std::string system_identifier = "CRI ROFS";
    std::string volume_identifier;
    std::string volume_set_identifier;
    std::string publisher_identifier;
    std::string data_preparer_identifier;
    std::string application_identifier;
    std::optional<std::pair<std::string, std::filesystem::path>> define_root;
    std::vector<CvmBuildScriptExportFile> files;
};

[[nodiscard]] std::string format_cvm_build_script(const CvmBuildScriptExport& script);

class CvmBuildScript {
public:
    CvmBuildScript() = default;

    [[nodiscard]] static std::expected<CvmBuildScript, std::string> parse(
        std::string_view script_text,
        const std::filesystem::path& script_directory = {}
    );
    [[nodiscard]] static std::expected<CvmBuildScript, std::string> load(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& source_path() const noexcept { return m_source_path; }
    [[nodiscard]] const std::string& disc_name() const noexcept { return m_disc_name; }
    [[nodiscard]] const std::string& recording_date() const noexcept { return m_recording_date; }
    [[nodiscard]] const std::string& media() const noexcept { return m_media; }
    [[nodiscard]] const std::string& system_identifier() const noexcept { return m_system_identifier; }
    [[nodiscard]] const std::string& volume_identifier() const noexcept { return m_volume_identifier; }
    [[nodiscard]] const std::string& volume_set_identifier() const noexcept { return m_volume_set_identifier; }
    [[nodiscard]] const std::string& publisher_identifier() const noexcept { return m_publisher_identifier; }
    [[nodiscard]] const std::string& data_preparer_identifier() const noexcept { return m_data_preparer_identifier; }
    [[nodiscard]] const std::string& application_identifier() const noexcept { return m_application_identifier; }
    [[nodiscard]] const std::vector<CvmBuildScriptFile>& files() const noexcept { return m_files; }
    [[nodiscard]] std::string to_text() const;

private:
    std::filesystem::path m_source_path;
    std::string m_disc_name;
    std::string m_recording_date;
    std::string m_media;
    std::string m_system_identifier;
    std::string m_volume_identifier;
    std::string m_volume_set_identifier;
    std::string m_publisher_identifier;
    std::string m_data_preparer_identifier;
    std::string m_application_identifier;
    std::vector<CvmBuildScriptFile> m_files;
};

} // namespace cricodecs::cvm
