#include "binding_helpers.hpp"

#include <filesystem>
#include <vector>

#include <nanobind/stl/array.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/vector.h>

#include "../../CriCodecs/src/cvm/cvm_build_script.hpp"
#include "../../CriCodecs/src/cvm/cvm_builder.hpp"
#include "../../CriCodecs/src/cvm/cvm_container.hpp"
#include "../../CriCodecs/src/cvm/cvm_volume_set.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] const cricodecs::cvm::CvmEntry& entry_at(const cricodecs::cvm::CvmContainer& self, uint32_t index) {
    const auto& entries = self.entries();
    if (index >= entries.size()) {
        raise_value_error("CVM entry index is out of range");
    }
    return entries[index];
}

[[nodiscard]] const cricodecs::cvm::CvmEntry& entry_at_for_update(
    cricodecs::cvm::CvmContainer& self,
    uint32_t index
) {
    return entry_at(self, index);
}

cricodecs::cvm::CvmBuildDirectoryOptions make_directory_options(
    const std::string& disc_name,
    const std::string& recording_date,
    const std::string& media,
    const std::string& system_identifier,
    const std::string& volume_identifier,
    const std::string& volume_set_identifier,
    const std::string& publisher_identifier,
    const std::string& data_preparer_identifier,
    const std::string& application_identifier
) {
    return {
        .disc_name = disc_name,
        .recording_date = recording_date,
        .media = media,
        .system_identifier = system_identifier,
        .volume_identifier = volume_identifier,
        .volume_set_identifier = volume_set_identifier,
        .publisher_identifier = publisher_identifier,
        .data_preparer_identifier = data_preparer_identifier,
        .application_identifier = application_identifier,
    };
}

[[nodiscard]] cricodecs::cvm::CvmContainer load_cvm_any(const nb::object& source, const std::string& key) {
    if (auto path = python_text_path(source)) {
        return unwrap_expected(cricodecs::cvm::CvmContainer::load(std::filesystem::path(*path), key));
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    return unwrap_expected(cricodecs::cvm::CvmContainer::load(std::move(owned), key));
}

} // namespace

void bind_cvm_module(nb::module_& module) {
    nb::class_<cricodecs::cvm::CvmBuildFile>(module, "CvmBuildFile")
        .def(nb::init<>())
        .def("__init__", [](cricodecs::cvm::CvmBuildFile* self, const nb::object& archive_path, const nb::object& source_path) {
            cricodecs::cvm::CvmBuildFile file;
            file.archive_path = require_python_path(archive_path, "archive_path");
            file.source_path = require_python_path(source_path, "source_path");
            new (self) cricodecs::cvm::CvmBuildFile(std::move(file));
        }, nb::arg("archive_path"), nb::arg("source_path"))
        .def_prop_rw(
            "archive_path",
            [](const cricodecs::cvm::CvmBuildFile& self) { return self.archive_path.generic_string(); },
            [](cricodecs::cvm::CvmBuildFile& self, const std::string& archive_path) {
                self.archive_path = std::filesystem::path(archive_path);
            }
        )
        .def_prop_rw(
            "source_path",
            [](const cricodecs::cvm::CvmBuildFile& self) { return self.source_path.generic_string(); },
            [](cricodecs::cvm::CvmBuildFile& self, const std::string& source_path) {
                self.source_path = std::filesystem::path(source_path);
            }
        );

    nb::class_<cricodecs::cvm::CvmBuildInput>(module, "CvmBuildInput")
        .def(nb::init<>())
        .def("__init__", [](
            cricodecs::cvm::CvmBuildInput* self,
            const std::string& disc_name,
            const std::vector<cricodecs::cvm::CvmBuildFile>& files,
            const std::string& recording_date,
            const std::string& media,
            const std::string& system_identifier,
            const std::string& volume_identifier,
            const std::string& volume_set_identifier,
            const std::string& publisher_identifier,
            const std::string& data_preparer_identifier,
            const std::string& application_identifier
        ) {
            cricodecs::cvm::CvmBuildInput input;
            input.disc_name = disc_name;
            input.files = files;
            input.recording_date = recording_date;
            input.media = media;
            input.system_identifier = system_identifier;
            input.volume_identifier = volume_identifier;
            input.volume_set_identifier = volume_set_identifier;
            input.publisher_identifier = publisher_identifier;
            input.data_preparer_identifier = data_preparer_identifier;
            input.application_identifier = application_identifier;
            new (self) cricodecs::cvm::CvmBuildInput(std::move(input));
        },
            nb::arg("disc_name") = "",
            nb::arg("files") = std::vector<cricodecs::cvm::CvmBuildFile>{},
            nb::arg("recording_date") = "",
            nb::arg("media") = "DVD",
            nb::arg("system_identifier") = "CRI ROFS",
            nb::arg("volume_identifier") = "",
            nb::arg("volume_set_identifier") = "",
            nb::arg("publisher_identifier") = "",
            nb::arg("data_preparer_identifier") = "",
            nb::arg("application_identifier") = ""
        )
        .def_static("from_script", &cricodecs::cvm::CvmBuildInput::from_script, nb::arg("script"))
        .def_static(
            "from_directory",
            [](const std::string& input_dir,
               const std::string& disc_name,
               const std::string& recording_date,
               const std::string& media,
               const std::string& system_identifier,
               const std::string& volume_identifier,
               const std::string& volume_set_identifier,
               const std::string& publisher_identifier,
               const std::string& data_preparer_identifier,
               const std::string& application_identifier) {
                return unwrap_expected(cricodecs::cvm::CvmBuildInput::from_directory(
                    std::filesystem::path(input_dir),
                    make_directory_options(
                        disc_name,
                        recording_date,
                        media,
                        system_identifier,
                        volume_identifier,
                        volume_set_identifier,
                        publisher_identifier,
                        data_preparer_identifier,
                        application_identifier
                    )
                ));
            },
            nb::arg("input_dir"),
            nb::arg("disc_name") = "",
            nb::arg("recording_date") = "",
            nb::arg("media") = "DVD",
            nb::arg("system_identifier") = "CRI ROFS",
            nb::arg("volume_identifier") = "",
            nb::arg("volume_set_identifier") = "",
            nb::arg("publisher_identifier") = "",
            nb::arg("data_preparer_identifier") = "",
            nb::arg("application_identifier") = ""
        )
        .def_rw("disc_name", &cricodecs::cvm::CvmBuildInput::disc_name)
        .def_rw("recording_date", &cricodecs::cvm::CvmBuildInput::recording_date)
        .def_rw("media", &cricodecs::cvm::CvmBuildInput::media)
        .def_rw("system_identifier", &cricodecs::cvm::CvmBuildInput::system_identifier)
        .def_rw("volume_identifier", &cricodecs::cvm::CvmBuildInput::volume_identifier)
        .def_rw("volume_set_identifier", &cricodecs::cvm::CvmBuildInput::volume_set_identifier)
        .def_rw("publisher_identifier", &cricodecs::cvm::CvmBuildInput::publisher_identifier)
        .def_rw("data_preparer_identifier", &cricodecs::cvm::CvmBuildInput::data_preparer_identifier)
        .def_rw("application_identifier", &cricodecs::cvm::CvmBuildInput::application_identifier)
        .def_rw("files", &cricodecs::cvm::CvmBuildInput::files);
    module.attr("CvmBuildConfig") = module.attr("CvmBuildInput");

    nb::class_<cricodecs::cvm::CvmBuildScriptFile>(module, "CvmBuildScriptFile")
        .def_ro("index", &cricodecs::cvm::CvmBuildScriptFile::index)
        .def_prop_ro("archive_path", [](const cricodecs::cvm::CvmBuildScriptFile& self) {
            return self.archive_path.generic_string();
        })
        .def_prop_ro("source_path", [](const cricodecs::cvm::CvmBuildScriptFile& self) {
            return self.source_path.generic_string();
        });

    nb::class_<cricodecs::cvm::CvmBuildScript>(module, "CvmBuildScript")
        .def_static("load", [](const std::string& path) {
            return unwrap_expected(cricodecs::cvm::CvmBuildScript::load(std::filesystem::path(path)));
        }, nb::arg("path"))
        .def_static("parse", [](const std::string& script_text, const std::string& script_directory) {
            return unwrap_expected(cricodecs::cvm::CvmBuildScript::parse(script_text, std::filesystem::path(script_directory)));
        }, nb::arg("script_text"), nb::arg("script_directory") = "")
        .def_prop_ro("source_path", [](const cricodecs::cvm::CvmBuildScript& self) {
            return self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
        })
        .def_prop_ro("disc_name", &cricodecs::cvm::CvmBuildScript::disc_name)
        .def_prop_ro("recording_date", &cricodecs::cvm::CvmBuildScript::recording_date)
        .def_prop_ro("media", &cricodecs::cvm::CvmBuildScript::media)
        .def_prop_ro("system_identifier", &cricodecs::cvm::CvmBuildScript::system_identifier)
        .def_prop_ro("volume_identifier", &cricodecs::cvm::CvmBuildScript::volume_identifier)
        .def_prop_ro("volume_set_identifier", &cricodecs::cvm::CvmBuildScript::volume_set_identifier)
        .def_prop_ro("publisher_identifier", &cricodecs::cvm::CvmBuildScript::publisher_identifier)
        .def_prop_ro("data_preparer_identifier", &cricodecs::cvm::CvmBuildScript::data_preparer_identifier)
        .def_prop_ro("application_identifier", &cricodecs::cvm::CvmBuildScript::application_identifier)
        .def_prop_ro("files", [](const cricodecs::cvm::CvmBuildScript& self) {
            nb::list files;
            for (const auto& file : self.files()) {
                files.append(file);
            }
            return files;
        })
        .def("info", [](const cricodecs::cvm::CvmBuildScript& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("disc_name") = self.disc_name();
            info.attr("recording_date") = self.recording_date();
            info.attr("media") = self.media();
            info.attr("system_identifier") = self.system_identifier();
            info.attr("volume_identifier") = self.volume_identifier();
            info.attr("volume_set_identifier") = self.volume_set_identifier();
            info.attr("publisher_identifier") = self.publisher_identifier();
            info.attr("data_preparer_identifier") = self.data_preparer_identifier();
            info.attr("application_identifier") = self.application_identifier();
            nb::list files;
            for (const auto& file : self.files()) {
                files.append(file);
            }
            info.attr("files") = files;
            return info;
        })
        .def("to_text", &cricodecs::cvm::CvmBuildScript::to_text);

    nb::class_<cricodecs::cvm::CvmRofsFileInfo>(module, "CvmRofsFileInfo")
        .def_ro("name", &cricodecs::cvm::CvmRofsFileInfo::name)
        .def_ro("size", &cricodecs::cvm::CvmRofsFileInfo::size)
        .def_ro("is_directory", &cricodecs::cvm::CvmRofsFileInfo::is_directory);

    nb::class_<cricodecs::cvm::CvmRofsVolumeInfo>(module, "CvmRofsVolumeInfo")
        .def_ro("name", &cricodecs::cvm::CvmRofsVolumeInfo::name)
        .def_prop_ro("source_path", [](const cricodecs::cvm::CvmRofsVolumeInfo& self) {
            return self.source_path.generic_string();
        })
        .def_prop_ro("current_directory", [](const cricodecs::cvm::CvmRofsVolumeInfo& self) {
            return self.current_directory.generic_string();
        })
        .def_ro("is_default", &cricodecs::cvm::CvmRofsVolumeInfo::is_default)
        .def_ro("is_scrambled", &cricodecs::cvm::CvmRofsVolumeInfo::is_scrambled);

    nb::class_<cricodecs::cvm::CvmRofsScrambleInfo>(module, "CvmRofsScrambleInfo")
        .def(nb::init<>())
        .def_rw("volume_name", &cricodecs::cvm::CvmRofsScrambleInfo::volume_name)
        .def_rw("volume_token", &cricodecs::cvm::CvmRofsScrambleInfo::volume_token)
        .def_rw("initial_sector", &cricodecs::cvm::CvmRofsScrambleInfo::initial_sector)
        .def_rw("current_sector", &cricodecs::cvm::CvmRofsScrambleInfo::current_sector)
        .def_rw("is_scrambled", &cricodecs::cvm::CvmRofsScrambleInfo::is_scrambled)
        .def_rw("raw_words", &cricodecs::cvm::CvmRofsScrambleInfo::raw_words);

    nb::enum_<cricodecs::cvm::CvmRofsTransferStatus>(module, "CvmRofsTransferStatus")
        .value("IDLE", cricodecs::cvm::CvmRofsTransferStatus::idle)
        .value("COMPLETE", cricodecs::cvm::CvmRofsTransferStatus::complete)
        .value("TRANSFERRING", cricodecs::cvm::CvmRofsTransferStatus::transferring)
        .value("ERROR", cricodecs::cvm::CvmRofsTransferStatus::error);

    nb::enum_<cricodecs::cvm::CvmRofsSeekMode>(module, "CvmRofsSeekMode")
        .value("SET", cricodecs::cvm::CvmRofsSeekMode::set)
        .value("CURRENT", cricodecs::cvm::CvmRofsSeekMode::current)
        .value("END", cricodecs::cvm::CvmRofsSeekMode::end);

    nb::class_<cricodecs::cvm::CvmRofsRangeHandle>(module, "CvmRofsRangeHandle")
        .def(nb::init<>())
        .def_rw("volume_name", &cricodecs::cvm::CvmRofsRangeHandle::volume_name)
        .def_rw("start_sector", &cricodecs::cvm::CvmRofsRangeHandle::start_sector)
        .def_rw("sector_count", &cricodecs::cvm::CvmRofsRangeHandle::sector_count)
        .def_rw("current_sector", &cricodecs::cvm::CvmRofsRangeHandle::current_sector)
        .def_rw("byte_size", &cricodecs::cvm::CvmRofsRangeHandle::byte_size)
        .def_rw("last_transfer_sector_count", &cricodecs::cvm::CvmRofsRangeHandle::last_transfer_sector_count)
        .def_rw("last_transfer_status", &cricodecs::cvm::CvmRofsRangeHandle::last_transfer_status);

    nb::class_<cricodecs::cvm::CvmHeader>(module, "CvmHeader")
        .def_ro("chunk_length", &cricodecs::cvm::CvmHeader::chunk_length)
        .def_ro("total_size", &cricodecs::cvm::CvmHeader::total_size)
        .def_prop_ro("recording_date", [](const cricodecs::cvm::CvmHeader& self) { return to_python_bytes(self.recording_date); })
        .def_ro("flags", &cricodecs::cvm::CvmHeader::flags)
        .def_ro("filesystem_id", &cricodecs::cvm::CvmHeader::filesystem_id)
        .def_ro("maker_id", &cricodecs::cvm::CvmHeader::maker_id)
        .def_ro("zone_sector_index", &cricodecs::cvm::CvmHeader::zone_sector_index)
        .def_ro("iso_start_sector", &cricodecs::cvm::CvmHeader::iso_start_sector);

    nb::class_<cricodecs::cvm::CvmZoneLayout>(module, "CvmZoneLayout")
        .def_ro("chunk_length", &cricodecs::cvm::CvmZoneLayout::chunk_length)
        .def_ro("zone_sector", &cricodecs::cvm::CvmZoneLayout::zone_sector)
        .def_ro("sector_length_1", &cricodecs::cvm::CvmZoneLayout::sector_length_1)
        .def_ro("sector_length_2", &cricodecs::cvm::CvmZoneLayout::sector_length_2)
        .def_ro("data_sector", &cricodecs::cvm::CvmZoneLayout::data_sector)
        .def_ro("data_length", &cricodecs::cvm::CvmZoneLayout::data_length)
        .def_ro("iso_sector", &cricodecs::cvm::CvmZoneLayout::iso_sector)
        .def_ro("iso_length", &cricodecs::cvm::CvmZoneLayout::iso_length);

    nb::class_<cricodecs::cvm::CvmPrimaryVolume>(module, "CvmPrimaryVolume")
        .def_ro("system_identifier", &cricodecs::cvm::CvmPrimaryVolume::system_identifier)
        .def_ro("volume_identifier", &cricodecs::cvm::CvmPrimaryVolume::volume_identifier)
        .def_ro("volume_set_identifier", &cricodecs::cvm::CvmPrimaryVolume::volume_set_identifier)
        .def_ro("publisher_identifier", &cricodecs::cvm::CvmPrimaryVolume::publisher_identifier)
        .def_ro("data_preparer_identifier", &cricodecs::cvm::CvmPrimaryVolume::data_preparer_identifier)
        .def_ro("application_identifier", &cricodecs::cvm::CvmPrimaryVolume::application_identifier)
        .def_ro("volume_space_size", &cricodecs::cvm::CvmPrimaryVolume::volume_space_size)
        .def_ro("logical_block_size", &cricodecs::cvm::CvmPrimaryVolume::logical_block_size);

    nb::class_<cricodecs::cvm::CvmEntry>(module, "CvmEntry")
        .def_ro("index", &cricodecs::cvm::CvmEntry::index)
        .def_prop_ro("path", [](const cricodecs::cvm::CvmEntry& self) { return self.path.generic_string(); })
        .def_ro("extent_sector", &cricodecs::cvm::CvmEntry::extent_sector)
        .def_ro("size", &cricodecs::cvm::CvmEntry::size);

    nb::class_<cricodecs::cvm::CvmDirectoryEntry>(module, "CvmDirectoryEntry")
        .def_ro("name", &cricodecs::cvm::CvmDirectoryEntry::name)
        .def_prop_ro("archive_path", [](const cricodecs::cvm::CvmDirectoryEntry& self) {
            return self.archive_path.generic_string();
        })
        .def_ro("is_directory", &cricodecs::cvm::CvmDirectoryEntry::is_directory)
        .def_ro("size", &cricodecs::cvm::CvmDirectoryEntry::size);

    nb::class_<cricodecs::cvm::CvmDirectoryRecord>(module, "CvmDirectoryRecord")
        .def_prop_ro("directory_path", [](const cricodecs::cvm::CvmDirectoryRecord& self) {
            return self.directory_path.generic_string();
        })
        .def_ro("extent_sector", &cricodecs::cvm::CvmDirectoryRecord::extent_sector)
        .def_ro("byte_size", &cricodecs::cvm::CvmDirectoryRecord::byte_size)
        .def_ro("entries", &cricodecs::cvm::CvmDirectoryRecord::entries);

    nb::class_<cricodecs::cvm::CvmContainer>(module, "Cvm")
        .def_static("load", &load_cvm_any, nb::arg("source"), nb::arg("key") = "")
        .def_static("load_bytes", [](const nb::bytes& data, const std::string& key) {
            auto owned = copy_python_bytes(data);
            return unwrap_expected(cricodecs::cvm::CvmContainer::load(std::move(owned), key));
        }, nb::arg("data"), nb::arg("key") = "")
        .def_prop_ro("source_path", [](const cricodecs::cvm::CvmContainer& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_rw("disc_name", &cricodecs::cvm::CvmContainer::disc_name, &cricodecs::cvm::CvmContainer::set_disc_name)
        .def_prop_rw("recording_date", &cricodecs::cvm::CvmContainer::recording_date_text, &cricodecs::cvm::CvmContainer::set_recording_date)
        .def_prop_rw("system_identifier", [](const cricodecs::cvm::CvmContainer& self) {
            return self.primary_volume().system_identifier;
        }, &cricodecs::cvm::CvmContainer::set_system_identifier)
        .def_prop_rw("volume_identifier", [](const cricodecs::cvm::CvmContainer& self) {
            return self.primary_volume().volume_identifier;
        }, &cricodecs::cvm::CvmContainer::set_volume_identifier)
        .def_prop_rw("volume_set_identifier", [](const cricodecs::cvm::CvmContainer& self) {
            return self.primary_volume().volume_set_identifier;
        }, &cricodecs::cvm::CvmContainer::set_volume_set_identifier)
        .def_prop_rw("publisher_identifier", [](const cricodecs::cvm::CvmContainer& self) {
            return self.primary_volume().publisher_identifier;
        }, &cricodecs::cvm::CvmContainer::set_publisher_identifier)
        .def_prop_rw("data_preparer_identifier", [](const cricodecs::cvm::CvmContainer& self) {
            return self.primary_volume().data_preparer_identifier;
        }, &cricodecs::cvm::CvmContainer::set_data_preparer_identifier)
        .def_prop_rw("application_identifier", [](const cricodecs::cvm::CvmContainer& self) {
            return self.primary_volume().application_identifier;
        }, &cricodecs::cvm::CvmContainer::set_application_identifier)
        .def_prop_ro("header", &cricodecs::cvm::CvmContainer::header)
        .def_prop_ro("sector_table", &cricodecs::cvm::CvmContainer::sector_table)
        .def_prop_ro("zone", &cricodecs::cvm::CvmContainer::zone)
        .def_prop_ro("primary_volume", &cricodecs::cvm::CvmContainer::primary_volume)
        .def_prop_ro("is_scrambled", &cricodecs::cvm::CvmContainer::is_scrambled)
        .def_prop_ro("has_accessible_contents", &cricodecs::cvm::CvmContainer::has_accessible_contents)
        .def_prop_ro("embedded_iso_offset", &cricodecs::cvm::CvmContainer::embedded_iso_offset)
        .def_prop_ro("embedded_iso_size", &cricodecs::cvm::CvmContainer::embedded_iso_size)
        .def_prop_ro("embedded_iso_sector_count", &cricodecs::cvm::CvmContainer::embedded_iso_sector_count)
        .def_prop_ro("entry_count", &cricodecs::cvm::CvmContainer::entry_count)
        .def_prop_ro("entries", [](const cricodecs::cvm::CvmContainer& self) {
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            return entries;
        })
        .def("info", [](const cricodecs::cvm::CvmContainer& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = self.source_path().empty() ? nb::none() : nb::cast(self.source_path().generic_string());
            info.attr("disc_name") = self.disc_name();
            info.attr("recording_date") = self.recording_date_text();
            info.attr("header") = self.header();
            info.attr("zone") = self.zone();
            info.attr("primary_volume") = self.primary_volume();
            info.attr("is_scrambled") = self.is_scrambled();
            info.attr("has_accessible_contents") = self.has_accessible_contents();
            info.attr("embedded_iso_offset") = self.embedded_iso_offset();
            info.attr("embedded_iso_size") = self.embedded_iso_size();
            info.attr("embedded_iso_sector_count") = self.embedded_iso_sector_count();
            info.attr("entry_count") = self.entry_count();
            nb::list entries;
            for (const auto& entry : self.entries()) {
                entries.append(entry);
            }
            info.attr("entries") = entries;
            return info;
        })
        .def("entry", &entry_at, nb::rv_policy::reference_internal, nb::arg("index"))
        .def("find_entry", [](const cricodecs::cvm::CvmContainer& self, const std::string& archive_path) -> nb::object {
            const auto* entry = self.find_entry(std::filesystem::path(archive_path));
            if (entry == nullptr) {
                return nb::none();
            }
            return nb::cast(*entry);
        }, nb::arg("archive_path"))
        .def("directory_record", [](const cricodecs::cvm::CvmContainer& self, const std::string& archive_directory) {
            return unwrap_expected(self.directory_record(std::filesystem::path(archive_directory)));
        }, nb::arg("archive_directory") = "")
        .def("file_bytes", [](const cricodecs::cvm::CvmContainer& self, uint32_t index) {
            return to_python_bytes(unwrap_expected(self.file_data(entry_at(self, index).index)));
        }, nb::arg("index"))
        .def("file_bytes_at", [](const cricodecs::cvm::CvmContainer& self, const std::string& archive_path) {
            return to_python_bytes(unwrap_expected(self.file_data(std::filesystem::path(archive_path))));
        }, nb::arg("archive_path"))
        .def("extract_file", [](const cricodecs::cvm::CvmContainer& self, uint32_t index, const nb::object& output_path) {
            unwrap_expected(self.extract_file(entry_at(self, index).index, require_python_path(output_path, "output_path")));
        }, nb::arg("index"), nb::arg("output_path"))
        .def("extract", [](const cricodecs::cvm::CvmContainer& self, const nb::object& output_dir) {
            unwrap_expected(self.extract(require_python_path(output_dir, "output_dir")));
        }, nb::arg("output_dir"))
        .def("save_bytes", [](const cricodecs::cvm::CvmContainer& self, const std::string& key) {
            return to_python_bytes(unwrap_expected(self.save(key)));
        }, nb::arg("key") = "")
        .def("save", [](const cricodecs::cvm::CvmContainer& self, const std::string& key) {
            return to_python_bytes(unwrap_expected(self.save(key)));
        }, nb::arg("key") = "")
        .def("save", [](const cricodecs::cvm::CvmContainer& self, const nb::object& output_path, const std::string& key) {
            unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path"), key));
        }, nb::arg("output_path"), nb::arg("key") = "")
        .def("script_text", [](const cricodecs::cvm::CvmContainer& self) {
            return unwrap_expected(self.export_script_text());
        })
        .def("export_script", [](const cricodecs::cvm::CvmContainer& self, const nb::object& output_path) {
            unwrap_expected(self.export_script_file(require_python_path(output_path, "output_path")));
        }, nb::arg("output_path"))
        .def("add_file", [](cricodecs::cvm::CvmContainer& self, const nb::object& source_path, const std::string& archive_path) {
            return unwrap_expected(self.add_file(require_python_path(source_path, "source_path"), std::filesystem::path(archive_path)));
        }, nb::arg("source_path"), nb::arg("archive_path"))
        .def("add_bytes", [](cricodecs::cvm::CvmContainer& self, const nb::bytes& data, const std::string& archive_path) {
            const auto bytes = copy_python_bytes(data);
            return unwrap_expected(self.add_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()), std::filesystem::path(archive_path)));
        }, nb::arg("data"), nb::arg("archive_path"))
        .def("replace_file", [](cricodecs::cvm::CvmContainer& self, uint32_t index, const nb::object& source_path) {
            unwrap_expected(self.replace_file(entry_at_for_update(self, index).index, require_python_path(source_path, "source_path")));
        }, nb::arg("index"), nb::arg("source_path"))
        .def("replace_file_at", [](cricodecs::cvm::CvmContainer& self, const std::string& archive_path, const nb::object& source_path) {
            unwrap_expected(self.replace_file(std::filesystem::path(archive_path), require_python_path(source_path, "source_path")));
        }, nb::arg("archive_path"), nb::arg("source_path"))
        .def("replace_bytes", [](cricodecs::cvm::CvmContainer& self, uint32_t index, const nb::bytes& data) {
            const auto bytes = copy_python_bytes(data);
            unwrap_expected(self.replace_bytes(entry_at_for_update(self, index).index, std::span<const uint8_t>(bytes.data(), bytes.size())));
        }, nb::arg("index"), nb::arg("data"))
        .def("replace_bytes", [](cricodecs::cvm::CvmContainer& self, const std::string& archive_path, const nb::bytes& data) {
            const auto bytes = copy_python_bytes(data);
            unwrap_expected(self.replace_bytes(std::filesystem::path(archive_path), std::span<const uint8_t>(bytes.data(), bytes.size())));
        }, nb::arg("archive_path"), nb::arg("data"))
        .def("replace_bytes_at", [](cricodecs::cvm::CvmContainer& self, const std::string& archive_path, const nb::bytes& data) {
            const auto bytes = copy_python_bytes(data);
            unwrap_expected(self.replace_bytes(std::filesystem::path(archive_path), std::span<const uint8_t>(bytes.data(), bytes.size())));
        }, nb::arg("archive_path"), nb::arg("data"))
        .def("remove", [](cricodecs::cvm::CvmContainer& self, uint32_t index) {
            unwrap_expected(self.remove(entry_at_for_update(self, index).index));
        }, nb::arg("index"))
        .def("remove", [](cricodecs::cvm::CvmContainer& self, const std::string& archive_path) {
            unwrap_expected(self.remove(std::filesystem::path(archive_path)));
        }, nb::arg("archive_path"))
        .def("remove_at", [](cricodecs::cvm::CvmContainer& self, const std::string& archive_path) {
            unwrap_expected(self.remove(std::filesystem::path(archive_path)));
        }, nb::arg("archive_path"))
        .def("move_file", [](cricodecs::cvm::CvmContainer& self, uint32_t from_index, uint32_t to_index) {
            unwrap_expected(self.move_file(from_index, to_index));
        }, nb::arg("from_index"), nb::arg("to_index"))
        .def("rename", [](cricodecs::cvm::CvmContainer& self, uint32_t index, const std::string& archive_path) {
            unwrap_expected(self.rename(entry_at_for_update(self, index).index, std::filesystem::path(archive_path)));
        }, nb::arg("index"), nb::arg("archive_path"))
        .def("rename_at", [](cricodecs::cvm::CvmContainer& self, const std::string& existing_archive_path, const std::string& archive_path) {
            unwrap_expected(self.rename(std::filesystem::path(existing_archive_path), std::filesystem::path(archive_path)));
        }, nb::arg("existing_archive_path"), nb::arg("archive_path"));

    nb::class_<cricodecs::cvm::CvmVolumeSet>(module, "CvmVolumeSet")
        .def(nb::init<>())
        .def_prop_ro("volume_count", &cricodecs::cvm::CvmVolumeSet::volume_count)
        .def_static("rofs_sector_length", &cricodecs::cvm::CvmVolumeSet::rofs_sector_length)
        .def_prop_ro("default_volume_name", [](const cricodecs::cvm::CvmVolumeSet& self) -> nb::object {
            const auto value = self.default_volume_name();
            return value.has_value() ? nb::cast(std::string(*value)) : nb::none();
        })
        .def("default_volume", [](const cricodecs::cvm::CvmVolumeSet& self) { return unwrap_expected(self.default_volume()); })
        .def("mount_path", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const std::string& path) {
            unwrap_expected(self.mount(volume_name, unwrap_expected(cricodecs::cvm::CvmContainer::load(std::filesystem::path(path)))));
        }, nb::arg("volume_name"), nb::arg("path"))
        .def("mount_bytes", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::bytes& data) {
            auto owned = copy_python_bytes(data);
            unwrap_expected(self.mount(
                volume_name,
                unwrap_expected(cricodecs::cvm::CvmContainer::load(std::move(owned)))
            ));
        }, nb::arg("volume_name"), nb::arg("data"))
        .def("mount", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::object& source) {
            if (auto path = python_text_path(source)) {
                unwrap_expected(self.mount(volume_name, unwrap_expected(cricodecs::cvm::CvmContainer::load(std::filesystem::path(*path)))));
                return;
            }
            auto borrowed = borrow_python_source(source);
            auto view = borrowed.as_span();
            unwrap_expected(self.mount(volume_name, unwrap_expected(cricodecs::cvm::CvmContainer::load(view))));
        }, nb::arg("volume_name"), nb::arg("source"))
        .def("switch_image_path", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const std::string& path) {
            unwrap_expected(self.switch_image(volume_name, unwrap_expected(cricodecs::cvm::CvmContainer::load(std::filesystem::path(path)))));
        }, nb::arg("volume_name"), nb::arg("path"))
        .def("switch_image_bytes", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::bytes& data) {
            auto owned = copy_python_bytes(data);
            unwrap_expected(self.switch_image(
                volume_name,
                unwrap_expected(cricodecs::cvm::CvmContainer::load(std::move(owned)))
            ));
        }, nb::arg("volume_name"), nb::arg("data"))
        .def("unmount", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) {
            unwrap_expected(self.unmount(volume_name));
        }, nb::arg("volume_name"))
        .def("set_default_volume", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) {
            unwrap_expected(self.set_default_volume(volume_name));
        }, nb::arg("volume_name"))
        .def("change_directory", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            unwrap_expected(self.change_directory(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path"))
        .def("set_current_directory_path", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            unwrap_expected(self.set_current_directory(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path"))
        .def("set_current_directory_record", [](cricodecs::cvm::CvmVolumeSet& self, const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            unwrap_expected(self.set_current_directory(as_byte_span(view)));
        }, nb::arg("rofs_directory_record"))
        .def("set_current_directory", [](cricodecs::cvm::CvmVolumeSet& self, const nb::object& source) {
            if (PyUnicode_Check(source.ptr())) {
                unwrap_expected(self.set_current_directory(std::filesystem::path(nb::cast<std::string>(source))));
                return;
            }
            if (auto path = python_text_path(source)) {
                unwrap_expected(self.set_current_directory(std::filesystem::path(*path)));
                return;
            }
            auto borrowed = borrow_python_source(source);
            unwrap_expected(self.set_current_directory(borrowed.as_span()));
        }, nb::arg("source"))
        .def("set_current_directory_iso", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::bytes& iso_directory_record) {
            const auto view = borrow_python_bytes(iso_directory_record);
            unwrap_expected(self.set_current_directory_iso(volume_name, as_byte_span(view)));
        }, nb::arg("volume_name"), nb::arg("iso_directory_record"))
        .def("set_current_directory_iso_count", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::bytes& iso_directory_record, uint32_t iso_directory_sector_count) {
            const auto view = borrow_python_bytes(iso_directory_record);
            unwrap_expected(self.set_current_directory_iso(volume_name, as_byte_span(view), iso_directory_sector_count));
        }, nb::arg("volume_name"), nb::arg("iso_directory_record"), nb::arg("iso_directory_sector_count"))
        .def("set_current_directory_iso_scramble", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::bytes& iso_directory_record, cricodecs::cvm::CvmRofsScrambleInfo& scramble_info) {
            auto bytes = copy_python_bytes(iso_directory_record);
            unwrap_expected(self.set_current_directory_iso(volume_name, std::span<uint8_t>(bytes.data(), bytes.size()), scramble_info));
        }, nb::arg("volume_name"), nb::arg("iso_directory_record"), nb::arg("scramble_info"))
        .def("set_current_directory_iso_count_scramble", [](cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, const nb::bytes& iso_directory_record, uint32_t iso_directory_sector_count, cricodecs::cvm::CvmRofsScrambleInfo& scramble_info) {
            auto bytes = copy_python_bytes(iso_directory_record);
            unwrap_expected(self.set_current_directory_iso(volume_name, std::span<uint8_t>(bytes.data(), bytes.size()), iso_directory_sector_count, scramble_info));
        }, nb::arg("volume_name"), nb::arg("iso_directory_record"), nb::arg("iso_directory_sector_count"), nb::arg("scramble_info"))
        .def("open_range", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name, uint32_t start_sector, uint32_t sector_count) {
            return unwrap_expected(self.open_range(volume_name, start_sector, sector_count));
        }, nb::arg("volume_name"), nb::arg("start_sector"), nb::arg("sector_count"))
        .def("open_file", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.open_file(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path"))
        .def("open_file_relative", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& relative_path, const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(self.open_file(std::filesystem::path(relative_path), as_byte_span(view)));
        }, nb::arg("relative_path"), nb::arg("rofs_directory_record"))
        .def("seek", [](const cricodecs::cvm::CvmVolumeSet& self, cricodecs::cvm::CvmRofsRangeHandle& handle, int32_t sector_offset, cricodecs::cvm::CvmRofsSeekMode seek_mode) {
            return unwrap_expected(self.seek(handle, sector_offset, seek_mode));
        }, nb::arg("handle"), nb::arg("sector_offset"), nb::arg("seek_mode"))
        .def("tell", [](const cricodecs::cvm::CvmVolumeSet& self, const cricodecs::cvm::CvmRofsRangeHandle& handle) {
            return unwrap_expected(self.tell(handle));
        }, nb::arg("handle"))
        .def("status", [](const cricodecs::cvm::CvmVolumeSet& self, const cricodecs::cvm::CvmRofsRangeHandle& handle) {
            return unwrap_expected(self.status(handle));
        }, nb::arg("handle"))
        .def("transferred_bytes", [](const cricodecs::cvm::CvmVolumeSet& self, const cricodecs::cvm::CvmRofsRangeHandle& handle) {
            return unwrap_expected(self.transferred_bytes(handle));
        }, nb::arg("handle"))
        .def("transferred_bytes64", [](const cricodecs::cvm::CvmVolumeSet& self, const cricodecs::cvm::CvmRofsRangeHandle& handle) {
            return unwrap_expected(self.transferred_bytes64(handle));
        }, nb::arg("handle"))
        .def("close", [](const cricodecs::cvm::CvmVolumeSet& self, cricodecs::cvm::CvmRofsRangeHandle& handle) {
            unwrap_expected(self.close(handle));
        }, nb::arg("handle"))
        .def("stop_transfer", [](const cricodecs::cvm::CvmVolumeSet& self, cricodecs::cvm::CvmRofsRangeHandle& handle) {
            unwrap_expected(self.stop_transfer(handle));
        }, nb::arg("handle"))
        .def("read_sectors", [](const cricodecs::cvm::CvmVolumeSet& self, cricodecs::cvm::CvmRofsRangeHandle& handle, uint32_t sector_count) {
            return to_python_bytes(unwrap_expected(self.read_sectors(handle, sector_count)));
        }, nb::arg("handle"), nb::arg("sector_count"))
        .def("volume_info", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) {
            return unwrap_expected(self.volume_info(volume_name));
        }, nb::arg("volume_name"))
        .def("default_volume_info", [](const cricodecs::cvm::CvmVolumeSet& self) {
            return unwrap_expected(self.default_volume_info());
        })
        .def("scramble_info", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.scramble_info(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path"))
        .def("descramble", [](const cricodecs::cvm::CvmVolumeSet& self, const nb::bytes& sector_data, cricodecs::cvm::CvmRofsScrambleInfo& scramble_info) {
            auto bytes = copy_python_bytes(sector_data);
            unwrap_expected(self.descramble(std::span<uint8_t>(bytes.data(), bytes.size()), scramble_info));
            return to_python_bytes(bytes);
        }, nb::arg("sector_data"), nb::arg("scramble_info"))
        .def_static("advance_scramble_info", &cricodecs::cvm::CvmVolumeSet::advance_scramble_info, nb::arg("scramble_info"), nb::arg("sector_count"))
        .def("current_directory", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) -> nb::object {
            const auto directory = self.current_directory(volume_name);
            return directory.has_value() ? nb::cast(directory->generic_string()) : nb::none();
        }, nb::arg("volume_name"))
        .def("find_entry", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) -> nb::object {
            const auto* entry = self.find_entry(std::filesystem::path(runtime_path));
            if (entry == nullptr) {
                return nb::none();
            }
            return nb::cast(*entry);
        }, nb::arg("runtime_path"))
        .def("file_exists", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return self.file_exists(std::filesystem::path(runtime_path));
        }, nb::arg("runtime_path"))
        .def("file_exists_relative", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& relative_path, const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return self.file_exists(std::filesystem::path(relative_path), as_byte_span(view));
        }, nb::arg("relative_path"), nb::arg("rofs_directory_record"))
        .def("file_size", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.file_size(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path"))
        .def("file_size64", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.file_size64(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path"))
        .def("file_size_relative", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& relative_path, const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(self.file_size(std::filesystem::path(relative_path), as_byte_span(view)));
        }, nb::arg("relative_path"), nb::arg("rofs_directory_record"))
        .def("file_size64_relative", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& relative_path, const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(self.file_size64(std::filesystem::path(relative_path), as_byte_span(view)));
        }, nb::arg("relative_path"), nb::arg("rofs_directory_record"))
        .def_static("rofs_num_files_record", [](const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(cricodecs::cvm::CvmVolumeSet::rofs_num_files(as_byte_span(view)));
        }, nb::arg("rofs_directory_record"))
        .def_static("rofs_num_files_from_record", [](const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(cricodecs::cvm::CvmVolumeSet::rofs_num_files(as_byte_span(view)));
        }, nb::arg("rofs_directory_record"))
        .def("rofs_num_files", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.rofs_num_files(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path") = "")
        .def("rofs_num_files_for_volume", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) {
            return unwrap_expected(self.rofs_num_files_for_volume(volume_name));
        }, nb::arg("volume_name"))
        .def_static("rofs_directory_info_record", [](const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(cricodecs::cvm::CvmVolumeSet::rofs_directory_info(as_byte_span(view)));
        }, nb::arg("rofs_directory_record"))
        .def_static("rofs_directory_info_from_record", [](const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return unwrap_expected(cricodecs::cvm::CvmVolumeSet::rofs_directory_info(as_byte_span(view)));
        }, nb::arg("rofs_directory_record"))
        .def("rofs_directory_info", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.rofs_directory_info(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path") = "")
        .def("rofs_directory_info_for_volume", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) {
            return unwrap_expected(self.rofs_directory_info_for_volume(volume_name));
        }, nb::arg("volume_name"))
        .def("load_iso_directory_record", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return to_python_bytes(unwrap_expected(self.load_iso_directory_record(std::filesystem::path(runtime_path))));
        }, nb::arg("runtime_path"))
        .def("load_rofs_directory_record", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path, uint32_t max_entries) {
            return to_python_bytes(unwrap_expected(self.load_rofs_directory_record(std::filesystem::path(runtime_path), max_entries)));
        }, nb::arg("runtime_path"), nb::arg("max_entries"))
        .def("directory_record", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return unwrap_expected(self.directory_record(std::filesystem::path(runtime_path)));
        }, nb::arg("runtime_path") = "")
        .def("directory_record_for_volume", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& volume_name) {
            return unwrap_expected(self.directory_record_for_volume(volume_name));
        }, nb::arg("volume_name"))
        .def("file_bytes", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& runtime_path) {
            return to_python_bytes(unwrap_expected(self.file_data(std::filesystem::path(runtime_path))));
        }, nb::arg("runtime_path"))
        .def("file_bytes_relative", [](const cricodecs::cvm::CvmVolumeSet& self, const std::string& relative_path, const nb::bytes& rofs_directory_record) {
            const auto view = borrow_python_bytes(rofs_directory_record);
            return to_python_bytes(unwrap_expected(self.file_data(std::filesystem::path(relative_path), as_byte_span(view))));
        }, nb::arg("relative_path"), nb::arg("rofs_directory_record"));

    install_attr_repr(module, "CvmBuildFile", {"archive_path", "source_path"});
    install_attr_repr(module, "CvmBuildInput", {"disc_name", "recording_date", "media", "system_identifier", "volume_identifier", "volume_set_identifier", "publisher_identifier", "data_preparer_identifier", "application_identifier", "files"});
    install_attr_repr(module, "CvmBuildScriptFile", {"index", "archive_path", "source_path"});
    install_attr_repr(module, "CvmBuildScript", {"source_path", "disc_name", "recording_date", "media", "system_identifier", "volume_identifier", "volume_set_identifier", "publisher_identifier", "data_preparer_identifier", "application_identifier", "files"});
    install_attr_repr(module, "CvmRofsFileInfo", {"name", "size", "is_directory"});
    install_attr_repr(module, "CvmRofsVolumeInfo", {"name", "source_path", "current_directory", "is_default", "is_scrambled"});
    install_attr_repr(module, "CvmRofsScrambleInfo", {"volume_name", "volume_token", "initial_sector", "current_sector", "is_scrambled", "raw_words"});
    install_attr_repr(module, "CvmRofsRangeHandle", {"volume_name", "start_sector", "sector_count", "current_sector", "byte_size", "last_transfer_sector_count", "last_transfer_status"});
    install_attr_repr(module, "CvmHeader", {"chunk_length", "total_size", "flags", "filesystem_id", "maker_id", "zone_sector_index", "iso_start_sector"});
    install_attr_repr(module, "CvmZoneLayout", {"chunk_length", "zone_sector", "sector_length_1", "sector_length_2", "data_sector", "data_length", "iso_sector", "iso_length"});
    install_attr_repr(module, "CvmPrimaryVolume", {"system_identifier", "volume_identifier", "volume_set_identifier", "publisher_identifier", "data_preparer_identifier", "application_identifier", "volume_space_size", "logical_block_size"});
    install_attr_repr(module, "CvmEntry", {"index", "path", "extent_sector", "size"});
    install_attr_repr(module, "CvmDirectoryEntry", {"name", "archive_path", "is_directory", "size"});
    install_attr_repr(module, "CvmDirectoryRecord", {"directory_path", "extent_sector", "byte_size", "entries"});
    install_attr_repr(module, "Cvm", {"source_path", "disc_name", "recording_date", "header", "zone", "primary_volume", "is_scrambled", "has_accessible_contents", "embedded_iso_offset", "embedded_iso_size", "embedded_iso_sector_count", "entry_count", "entries"});
    install_attr_repr(module, "CvmVolumeSet", {"volume_count", "default_volume_name"});

    module.def("build_from_input", [](const cricodecs::cvm::CvmBuildInput& input, const std::string& key) {
        return to_python_bytes(unwrap_expected(cricodecs::cvm::CvmBuilder{}.build(input, key)));
    }, nb::arg("input"), nb::arg("key") = "");
    module.def("build_from_script", [](const cricodecs::cvm::CvmBuildScript& script, const std::string& key) {
        return to_python_bytes(unwrap_expected(cricodecs::cvm::CvmBuilder{}.build(script, key)));
    }, nb::arg("script"), nb::arg("key") = "");
    module.def("build_to_file_from_input", [](const std::string& output_path, const cricodecs::cvm::CvmBuildInput& input, const std::string& key) {
        unwrap_expected(cricodecs::cvm::CvmBuilder{}.build_to_file(std::filesystem::path(output_path), input, key));
    }, nb::arg("output_path"), nb::arg("input"), nb::arg("key") = "");
    module.def("build_to_file_from_script", [](const std::string& output_path, const cricodecs::cvm::CvmBuildScript& script, const std::string& key) {
        unwrap_expected(cricodecs::cvm::CvmBuilder{}.build_to_file(std::filesystem::path(output_path), script, key));
    }, nb::arg("output_path"), nb::arg("script"), nb::arg("key") = "");
    module.def("build", [](const cricodecs::cvm::CvmBuildInput& input, const std::string& key) {
        return to_python_bytes(unwrap_expected(cricodecs::cvm::CvmBuilder{}.build(input, key)));
    }, nb::arg("config"), nb::arg("key") = "");
    module.def("build", [](const cricodecs::cvm::CvmBuildScript& script, const std::string& key) {
        return to_python_bytes(unwrap_expected(cricodecs::cvm::CvmBuilder{}.build(script, key)));
    }, nb::arg("config"), nb::arg("key") = "");
    module.def("build", [](const cricodecs::cvm::CvmBuildInput& input, const nb::object& output_path, const std::string& key) {
        unwrap_expected(cricodecs::cvm::CvmBuilder{}.build_to_file(require_python_path(output_path, "output_path"), input, key));
    }, nb::arg("config"), nb::arg("output_path"), nb::arg("key") = "");
    module.def("build", [](const cricodecs::cvm::CvmBuildScript& script, const nb::object& output_path, const std::string& key) {
        unwrap_expected(cricodecs::cvm::CvmBuilder{}.build_to_file(require_python_path(output_path, "output_path"), script, key));
    }, nb::arg("config"), nb::arg("output_path"), nb::arg("key") = "");
    module.def("build", [](
        const nb::object& input_dir,
        const std::string& disc_name,
        const std::string& recording_date,
        const std::string& media,
        const std::string& system_identifier,
        const std::string& volume_identifier,
        const std::string& volume_set_identifier,
        const std::string& publisher_identifier,
        const std::string& data_preparer_identifier,
        const std::string& application_identifier,
        const std::string& key
    ) {
        const auto root = require_python_path(input_dir, "input_dir");
        auto input = unwrap_expected(cricodecs::cvm::CvmBuildInput::from_directory(
            root,
            make_directory_options(
                disc_name,
                recording_date,
                media,
                system_identifier,
                volume_identifier,
                volume_set_identifier,
                publisher_identifier,
                data_preparer_identifier,
                application_identifier
            )
        ));
        return to_python_bytes(unwrap_expected(cricodecs::cvm::CvmBuilder{}.build(input, key)));
    },
        nb::arg("input_dir"),
        nb::arg("disc_name") = "",
        nb::arg("recording_date") = "",
        nb::arg("media") = "DVD",
        nb::arg("system_identifier") = "CRI ROFS",
        nb::arg("volume_identifier") = "",
        nb::arg("volume_set_identifier") = "",
        nb::arg("publisher_identifier") = "",
        nb::arg("data_preparer_identifier") = "",
        nb::arg("application_identifier") = "",
        nb::arg("key") = ""
    );
    module.def("load", &load_cvm_any, nb::arg("source"), nb::arg("key") = "");
    module.def("extract", [](const nb::object& source, const std::string& output_dir, const std::string& key) {
        auto cvm = load_cvm_any(source, key);
        unwrap_expected(cvm.extract(std::filesystem::path(output_dir)));
    }, nb::arg("source"), nb::arg("output_dir"), nb::arg("key") = "");
    module.def("load_script", [](const nb::object& path) {
        return unwrap_expected(cricodecs::cvm::CvmBuildScript::load(require_python_path(path, "path")));
    }, nb::arg("path"));
    module.def("parse_script", [](const std::string& script_text, const nb::object& script_directory) {
        const auto directory = script_directory.is_none()
            ? std::filesystem::path{}
            : require_python_path(script_directory, "script_directory");
        return unwrap_expected(cricodecs::cvm::CvmBuildScript::parse(script_text, directory));
    }, nb::arg("script_text"), nb::arg("script_directory") = nb::none());
    module.def("export_script", [](
        const nb::object& input_dir,
        const std::string& disc_name,
        const std::string& recording_date,
        const std::string& media,
        const std::string& system_identifier,
        const std::string& volume_identifier,
        const std::string& volume_set_identifier,
        const std::string& publisher_identifier,
        const std::string& data_preparer_identifier,
        const std::string& application_identifier
    ) {
        const auto root = require_python_path(input_dir, "input_dir");
        auto input = unwrap_expected(cricodecs::cvm::CvmBuildInput::from_directory(
            root,
            make_directory_options(
                disc_name,
                recording_date,
                media,
                system_identifier,
                volume_identifier,
                volume_set_identifier,
                publisher_identifier,
                data_preparer_identifier,
                application_identifier
            )
        ));
        cricodecs::cvm::CvmBuildScriptExport script;
        script.disc_name = input.disc_name;
        script.recording_date = input.recording_date;
        script.media = input.media;
        script.system_identifier = input.system_identifier;
        script.volume_identifier = input.volume_identifier;
        script.volume_set_identifier = input.volume_set_identifier;
        script.publisher_identifier = input.publisher_identifier;
        script.data_preparer_identifier = input.data_preparer_identifier;
        script.application_identifier = input.application_identifier;
        script.files.reserve(input.files.size());
        for (const auto& file : input.files) {
            script.files.push_back(cricodecs::cvm::CvmBuildScriptExportFile{
                .archive_path = file.archive_path,
                .source_path = file.source_path,
            });
        }
        return cricodecs::cvm::format_cvm_build_script(script);
    },
        nb::arg("input_dir"),
        nb::arg("disc_name") = "",
        nb::arg("recording_date") = "",
        nb::arg("media") = "DVD",
        nb::arg("system_identifier") = "CRI ROFS",
        nb::arg("volume_identifier") = "",
        nb::arg("volume_set_identifier") = "",
        nb::arg("publisher_identifier") = "",
        nb::arg("data_preparer_identifier") = "",
        nb::arg("application_identifier") = ""
    );
}

} // namespace cricodecs::python
