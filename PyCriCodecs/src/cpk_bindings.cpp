#include "binding_helpers.hpp"

#include <filesystem>
#include <utility>
#include <vector>

#include <nanobind/stl/optional.h>

#include "../../CriCodecs/src/cpk/cpk_container.hpp"

namespace cricodecs::python {
namespace {

[[nodiscard]] const cricodecs::cpk::CpkEntry& file_at(
    const cricodecs::cpk::Cpk& self,
    uint32_t index
) {
    const auto* file = self.try_file(index);
    if (file == nullptr) {
        raise_value_error("CPK file index is out of range");
    }
    return *file;
}

[[nodiscard]] cricodecs::cpk::Cpk create_cpk(
    cricodecs::cpk::CpkPreset preset,
    uint16_t align,
    nb::object encoding)
{
    cricodecs::cpk::CpkOptions options;
    options.preset = preset;
    options.align = align;
    options.encoding = python_encoding_options_from_object(encoding);
    return cricodecs::cpk::Cpk::create(options);
}

[[nodiscard]] cricodecs::cpk::Cpk load_cpk_any(const nb::object& source, nb::object encoding) {
    const auto options = python_encoding_options_from_object(encoding);
    if (auto path = python_text_path(source)) {
        return unwrap_expected(cricodecs::cpk::Cpk::load(std::filesystem::path(*path), options));
    }
    auto borrowed = borrow_python_source(source);
    const auto data = borrowed.as_span();
    std::vector<uint8_t> owned(data.begin(), data.end());
    return unwrap_expected(cricodecs::cpk::Cpk::load(std::move(owned), options));
}

} // namespace

void bind_cpk_module(nb::module_& module) {
    nb::enum_<cricodecs::cpk::CpkMode>(module, "CpkMode")
        .value("MODE0", cricodecs::cpk::CpkMode::Mode0)
        .value("MODE1", cricodecs::cpk::CpkMode::Mode1)
        .value("MODE2", cricodecs::cpk::CpkMode::Mode2)
        .value("MODE3", cricodecs::cpk::CpkMode::Mode3);

    nb::enum_<cricodecs::cpk::CpkPreset>(module, "CpkPreset")
        .value("CUSTOM", cricodecs::cpk::CpkPreset::Custom)
        .value("ID", cricodecs::cpk::CpkPreset::Id)
        .value("FILENAME", cricodecs::cpk::CpkPreset::Filename)
        .value("FILENAME_ID", cricodecs::cpk::CpkPreset::FilenameId)
        .value("FILENAME_GROUP", cricodecs::cpk::CpkPreset::FilenameGroup)
        .value("ID_GROUP", cricodecs::cpk::CpkPreset::IdGroup)
        .value("FILENAME_ID_GROUP", cricodecs::cpk::CpkPreset::FilenameIdGroup);

    nb::class_<cricodecs::cpk::CpkEntry>(module, "CpkEntry")
        .def_ro("dirname", &cricodecs::cpk::CpkEntry::dirname)
        .def_prop_ro("dirname_raw", [](const cricodecs::cpk::CpkEntry& entry) {
            return string_to_python_bytes(entry.dirname_raw);
        })
        .def_ro("filename", &cricodecs::cpk::CpkEntry::filename)
        .def_prop_ro("filename_raw", [](const cricodecs::cpk::CpkEntry& entry) {
            return string_to_python_bytes(entry.filename_raw);
        })
        .def_ro("id", &cricodecs::cpk::CpkEntry::id)
        .def_ro("toc_index", &cricodecs::cpk::CpkEntry::toc_index)
        .def_ro("file_offset", &cricodecs::cpk::CpkEntry::file_offset)
        .def_ro("file_size", &cricodecs::cpk::CpkEntry::file_size)
        .def_ro("extract_size", &cricodecs::cpk::CpkEntry::extract_size)
        .def_ro("is_compressed", &cricodecs::cpk::CpkEntry::is_compressed)
        .def_ro("request_compress", &cricodecs::cpk::CpkEntry::request_compress)
        .def_ro("user_string", &cricodecs::cpk::CpkEntry::user_string)
        .def_prop_ro("full_path", [](const cricodecs::cpk::CpkEntry& entry) {
            return entry.full_path().generic_string();
        });

    nb::class_<cricodecs::cpk::Cpk>(module, "Cpk")
        .def(nb::init<>())
        .def_static(
            "create",
            &create_cpk,
            nb::arg("preset") = cricodecs::cpk::CpkPreset::Filename,
            nb::arg("align") = static_cast<uint16_t>(0x800),
            nb::arg("encoding") = nb::none()
        )
        .def_static(
            "load",
            &load_cpk_any,
            nb::arg("source"),
            nb::arg("encoding") = nb::none()
        )
        .def_static(
            "load_bytes",
            [](const nb::bytes& data, nb::object encoding) {
                auto owned = copy_python_bytes(data);
                return unwrap_expected(cricodecs::cpk::Cpk::load(
                    std::move(owned),
                    python_encoding_options_from_object(encoding)
                ));
            },
            nb::arg("data"),
            nb::arg("encoding") = nb::none()
        )
        .def_prop_ro("source_path", [](const cricodecs::cpk::Cpk& self) {
            return path_or_none(self.source_path());
        })
        .def_prop_ro("file_count", &cricodecs::cpk::Cpk::file_count)
        .def_prop_ro("layout_mode", &cricodecs::cpk::Cpk::layout_mode)
        .def_prop_ro("preset", &cricodecs::cpk::Cpk::preset)
        .def_prop_ro("has_declared_preset", &cricodecs::cpk::Cpk::has_declared_preset)
        .def_prop_ro("declared_preset", &cricodecs::cpk::Cpk::declared_preset)
        .def_prop_rw(
            "alignment",
            &cricodecs::cpk::Cpk::alignment,
            [](cricodecs::cpk::Cpk& self, uint16_t align) {
                self.edit_options().align = align;
            }
        )
        .def_prop_ro("content_offset", &cricodecs::cpk::Cpk::content_offset)
        .def_prop_ro("has_toc", &cricodecs::cpk::Cpk::has_toc)
        .def_prop_ro("has_itoc", &cricodecs::cpk::Cpk::has_itoc)
        .def_prop_ro("has_gtoc", &cricodecs::cpk::Cpk::has_gtoc)
        .def_prop_ro("has_etoc", &cricodecs::cpk::Cpk::has_etoc)
        .def_prop_rw(
            "tver",
            [](const cricodecs::cpk::Cpk& self) { return self.options().tver; },
            [](cricodecs::cpk::Cpk& self, const std::string& value) { self.edit_options().tver = value; }
        )
        .def_prop_rw(
            "comment",
            [](const cricodecs::cpk::Cpk& self) { return self.options().comment; },
            [](cricodecs::cpk::Cpk& self, const std::string& value) { self.edit_options().comment = value; }
        )
        .def_prop_rw(
            "etoc_local_dir",
            [](const cricodecs::cpk::Cpk& self) { return self.options().etoc_local_dir; },
            [](cricodecs::cpk::Cpk& self, const std::string& value) {
                self.edit_options().etoc_local_dir = value;
            }
        )
        .def_prop_rw(
            "declared_preset_for_save",
            [](const cricodecs::cpk::Cpk& self) { return self.options().preset; },
            [](cricodecs::cpk::Cpk& self, cricodecs::cpk::CpkPreset preset) { self.edit_options().preset = preset; }
        )
        .def_prop_rw(
            "enable_toc",
            [](const cricodecs::cpk::Cpk& self) { return self.options().enable_toc.value_or(false); },
            [](cricodecs::cpk::Cpk& self, bool value) { self.edit_options().enable_toc = value; }
        )
        .def_prop_rw(
            "enable_itoc",
            [](const cricodecs::cpk::Cpk& self) { return self.options().enable_itoc.value_or(false); },
            [](cricodecs::cpk::Cpk& self, bool value) { self.edit_options().enable_itoc = value; }
        )
        .def_prop_rw(
            "enable_gtoc",
            [](const cricodecs::cpk::Cpk& self) { return self.options().enable_gtoc.value_or(false); },
            [](cricodecs::cpk::Cpk& self, bool value) { self.edit_options().enable_gtoc = value; }
        )
        .def_prop_rw(
            "enable_etoc",
            [](const cricodecs::cpk::Cpk& self) { return self.options().enable_etoc.value_or(false); },
            [](cricodecs::cpk::Cpk& self, bool value) { self.edit_options().enable_etoc = value; }
        )
        .def_prop_ro("files", [](const cricodecs::cpk::Cpk& self) {
            nb::list files;
            for (const auto& entry : self.files()) {
                files.append(entry);
            }
            return files;
        })
        .def("info", [](const cricodecs::cpk::Cpk& self) {
            nb::object info = simple_namespace();
            info.attr("source_path") = path_or_none(self.source_path());
            info.attr("file_count") = self.file_count();
            info.attr("alignment") = self.alignment();
            info.attr("content_offset") = self.content_offset();
            info.attr("layout_mode") = self.layout_mode();
            info.attr("preset") = self.preset();
            info.attr("has_declared_preset") = self.has_declared_preset();
            info.attr("declared_preset") = self.declared_preset();
            info.attr("tver") = self.options().tver;
            info.attr("comment") = self.options().comment;
            info.attr("etoc_local_dir") = self.options().etoc_local_dir;
            info.attr("has_toc") = self.has_toc();
            info.attr("has_itoc") = self.has_itoc();
            info.attr("has_gtoc") = self.has_gtoc();
            info.attr("has_etoc") = self.has_etoc();
            nb::list files;
            for (const auto& entry : self.files()) {
                files.append(entry);
            }
            info.attr("files") = files;
            return info;
        })
        .def(
            "file_info",
            [](const cricodecs::cpk::Cpk& self, uint32_t index) -> const cricodecs::cpk::CpkEntry& {
                return file_at(self, index);
            },
            nb::rv_policy::reference_internal,
            nb::arg("index")
        )
        .def("file_infos", [](const cricodecs::cpk::Cpk& self) {
            nb::list files;
            for (const auto& entry : self.files()) {
                files.append(entry);
            }
            return files;
        })
        .def(
            "file_bytes",
            [](const cricodecs::cpk::Cpk& self, uint32_t index) {
                auto bytes = unwrap_expected(self.file_bytes(index));
                return to_python_bytes(bytes);
            },
            nb::arg("index")
        )
        .def(
            "extract",
            [](const cricodecs::cpk::Cpk& self, const nb::object& output_dir, bool disambiguate_conflicts) {
                unwrap_expected(self.extract(require_python_path(output_dir, "output_dir"), disambiguate_conflicts));
            },
            nb::arg("output_dir"),
            nb::arg("disambiguate_conflicts") = true
        )
        .def(
            "save_bytes",
            [](cricodecs::cpk::Cpk& self) {
                auto bytes = unwrap_expected(self.save());
                return to_python_bytes(bytes);
            }
        )
        .def(
            "save",
            [](cricodecs::cpk::Cpk& self) {
                auto bytes = unwrap_expected(self.save());
                return to_python_bytes(bytes);
            }
        )
        .def(
            "save",
            [](cricodecs::cpk::Cpk& self, const nb::object& output_path) {
                unwrap_expected(self.save_to_file(require_python_path(output_path, "output_path")));
            },
            nb::arg("output_path")
        )
        .def(
            "add_file",
            [](cricodecs::cpk::Cpk& self,
               const nb::object& local_path,
               const std::string& cpk_path,
               bool compress,
               std::optional<uint32_t> id) {
                self.add_file(require_python_path(local_path, "local_path"), cpk_path, compress, id);
            },
            nb::arg("local_path"),
            nb::arg("cpk_path"),
            nb::arg("compress") = false,
            nb::arg("id") = nb::none()
        )
        .def(
            "add_bytes",
            [](cricodecs::cpk::Cpk& self,
               const nb::bytes& data,
               const std::string& cpk_path,
               bool compress,
               std::optional<uint32_t> id) {
                const auto bytes = copy_python_bytes(data);
                self.add_bytes(std::span<const uint8_t>(bytes.data(), bytes.size()), cpk_path, compress, id);
            },
            nb::arg("data"),
            nb::arg("cpk_path"),
            nb::arg("compress") = false,
            nb::arg("id") = nb::none()
        )
        .def(
            "remove",
            [](cricodecs::cpk::Cpk& self, uint32_t index) {
                unwrap_expected(self.remove(index));
            },
            nb::arg("index")
        )
        .def(
            "rename",
            [](cricodecs::cpk::Cpk& self, uint32_t index, const std::string& cpk_path) {
                unwrap_expected(self.rename(index, cpk_path));
            },
            nb::arg("index"),
            nb::arg("cpk_path")
        )
        .def(
            "replace_file",
            [](cricodecs::cpk::Cpk& self,
               uint32_t index,
               const nb::object& local_path,
               std::optional<bool> compress) {
                unwrap_expected(self.replace_file(index, require_python_path(local_path, "local_path"), compress));
            },
            nb::arg("index"),
            nb::arg("local_path"),
            nb::arg("compress") = nb::none()
        )
        .def(
            "replace_bytes",
            [](cricodecs::cpk::Cpk& self,
               uint32_t index,
               const nb::bytes& data,
               std::optional<bool> compress) {
                const auto bytes = copy_python_bytes(data);
                unwrap_expected(self.replace_bytes(index, std::span<const uint8_t>(bytes.data(), bytes.size()), compress));
            },
            nb::arg("index"),
            nb::arg("data"),
            nb::arg("compress") = nb::none()
        )
        .def(
            "set_file_path",
            [](cricodecs::cpk::Cpk& self, uint32_t index, const std::string& cpk_path) {
                unwrap_expected(self.rename(index, cpk_path));
            },
            nb::arg("index"),
            nb::arg("cpk_path")
        )
        .def(
            "set_dirname",
            [](cricodecs::cpk::Cpk& self, uint32_t index, const std::string& dirname) {
                unwrap_expected(self.set_dirname(index, dirname));
            },
            nb::arg("index"),
            nb::arg("dirname")
        )
        .def(
            "set_filename",
            [](cricodecs::cpk::Cpk& self, uint32_t index, const std::string& filename) {
                unwrap_expected(self.set_filename(index, filename));
            },
            nb::arg("index"),
            nb::arg("filename")
        )
        .def(
            "set_request_compress",
            [](cricodecs::cpk::Cpk& self, uint32_t index, bool compress) {
                unwrap_expected(self.set_request_compress(index, compress));
            },
            nb::arg("index"),
            nb::arg("compress")
        )
        .def(
            "set_all_request_compress",
            [](cricodecs::cpk::Cpk& self, bool compress) {
                self.set_all_request_compress(compress);
            },
            nb::arg("compress")
        )
        .def(
            "move_file",
            [](cricodecs::cpk::Cpk& self, size_t from_index, size_t to_index) {
                unwrap_expected(self.move_file(from_index, to_index));
            },
            nb::arg("from_index"),
            nb::arg("to_index")
        )
        .def("encrypt", [](cricodecs::cpk::Cpk& self) {
            return to_python_bytes(unwrap_expected(self.encrypt()));
        })
        .def("decrypt", [](cricodecs::cpk::Cpk& self) {
            return to_python_bytes(unwrap_expected(self.decrypt()));
        });

    install_attr_repr(module, "CpkEntry", {"dirname", "filename", "id", "toc_index", "file_offset", "file_size", "extract_size", "is_compressed", "request_compress", "user_string", "full_path"});
    install_attr_repr(module, "Cpk", {"source_path", "file_count", "layout_mode", "preset", "has_declared_preset", "declared_preset", "alignment", "content_offset", "has_toc", "has_itoc", "has_gtoc", "has_etoc", "files"});

    module.def(
        "create",
        &create_cpk,
        nb::arg("preset") = cricodecs::cpk::CpkPreset::Filename,
        nb::arg("align") = static_cast<uint16_t>(0x800),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "load",
        &load_cpk_any,
        nb::arg("source"),
        nb::arg("encoding") = nb::none()
    );
    module.def(
        "extract",
        [](const nb::object& source, const nb::object& output_dir, bool disambiguate_conflicts, nb::object encoding) {
            auto cpk = load_cpk_any(source, encoding);
            unwrap_expected(cpk.extract(require_python_path(output_dir, "output_dir"), disambiguate_conflicts));
        },
        nb::arg("source"),
        nb::arg("output_dir"),
        nb::arg("disambiguate_conflicts") = true,
        nb::arg("encoding") = nb::none()
    );
}

} // namespace cricodecs::python
