#pragma once

#include "cpk_container.hpp"

#include <QString>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

class QWidget;
class QTableWidget;

namespace cristudio::modules::cpk {

struct EntryProperties {
    std::string full_path;
    std::string dirname;
    std::string filename;
    uint32_t id = 0;
    bool request_compress = false;
    std::string user_string = "<NULL>";
};

struct BuildOptionsSelection {
    cricodecs::cpk::CpkOptions options;
    bool obfuscate_utf = false;
};

[[nodiscard]] std::expected<std::optional<EntryProperties>, QString> choose_entry_properties(
    QWidget* parent,
    const cricodecs::cpk::CpkEntry& entry
);

[[nodiscard]] std::optional<BuildOptionsSelection> choose_build_options(
    QWidget* parent,
    const cricodecs::cpk::Cpk& cpk,
    bool obfuscate_utf
);

void populate_editor_archive_table(QTableWidget* table, const cricodecs::cpk::Cpk& cpk);
void configure_editor_archive_table(QTableWidget* table);

} // namespace cristudio::modules::cpk
