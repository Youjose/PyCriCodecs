/**
 * @file acx_container.cpp
 * @brief ACX container object helpers.
 *
 * ACX parsing and extraction are grounded in official `adxcat` behavior.
 * The object surface and validation are CriCodecs work by Youjose.
 */

#include "acx_container.hpp"

#include "acx_builder.hpp"
#include "../utilities/io.hpp"

namespace cricodecs::acx {

namespace {

std::expected<std::vector<uint8_t>, std::string> build_archive(
    std::vector<std::vector<uint8_t>> payloads) {
    AcxBuildInput input;
    input.entries.reserve(payloads.size());
    for (auto& payload : payloads) {
        input.entries.push_back({.data = std::move(payload)});
    }
    return AcxBuilder{}.build(input);
}

} // namespace

std::filesystem::path AcxEntry::suggested_path(bool include_index_prefix) const {
    const std::string stem = include_index_prefix
        ? ("stream_" + std::to_string(index))
        : std::string("stream");
    return std::filesystem::path(stem + entry_extension(type));
}

std::expected<std::vector<uint8_t>, std::string> AcxContainer::rebuild() const {
    auto payloads = copy_payloads();
    if (!payloads) return std::unexpected(payloads.error());
    return build_archive(std::move(*payloads));
}

std::expected<std::vector<std::vector<uint8_t>>, std::string>
AcxContainer::copy_payloads() const {
    std::vector<std::vector<uint8_t>> payloads;
    payloads.reserve(m_entries.size());
    for (uint32_t index = 0; index < m_entries.size(); ++index) {
        auto data = file_data(index);
        if (!data) return std::unexpected(data.error());
        payloads.emplace_back(data->begin(), data->end());
    }
    return payloads;
}

std::expected<void, std::string> AcxContainer::save_to_file(const std::filesystem::path& output_path) const {
    return rebuild().and_then([&](const auto& bytes) {
        return io::write_file_bytes(output_path, bytes, "ACX save failed");
    });
}

std::expected<void, std::string> AcxContainer::replace_payloads(std::vector<std::vector<uint8_t>> payloads) {
    auto bytes = build_archive(std::move(payloads));
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    m_source = io::SourceView::from_owned(std::move(*bytes));
    return parse();
}

std::expected<void, std::string> AcxContainer::set_file_data(uint32_t index, std::span<const uint8_t> data) {
    if (index >= m_entries.size()) {
        return std::unexpected("ACX entry index is out of range");
    }

    auto payloads = copy_payloads();
    if (!payloads) return std::unexpected(payloads.error());
    (*payloads)[index].assign(data.begin(), data.end());
    return replace_payloads(std::move(*payloads));
}

std::expected<void, std::string> AcxContainer::add_file(std::span<const uint8_t> data) {
    auto payloads = copy_payloads();
    if (!payloads) return std::unexpected(payloads.error());
    payloads->emplace_back(data.begin(), data.end());
    return replace_payloads(std::move(*payloads));
}

std::expected<void, std::string> AcxContainer::remove_file(uint32_t index) {
    if (index >= m_entries.size()) {
        return std::unexpected("ACX entry index is out of range");
    }
    if (m_entries.size() == 1) {
        return std::unexpected("ACX remove failed: archive must keep at least one entry");
    }

    auto payloads = copy_payloads();
    if (!payloads) return std::unexpected(payloads.error());
    payloads->erase(payloads->begin() + index);
    return replace_payloads(std::move(*payloads));
}

std::expected<void, std::string> AcxContainer::move_file(uint32_t from_index, uint32_t to_index) {
    if (from_index >= m_entries.size() || to_index >= m_entries.size()) {
        return std::unexpected("ACX move failed: entry index is out of range");
    }
    if (from_index == to_index) {
        return {};
    }

    auto payloads = copy_payloads();
    if (!payloads) return std::unexpected(payloads.error());

    auto moved = std::move((*payloads)[from_index]);
    payloads->erase(payloads->begin() + from_index);
    payloads->insert(payloads->begin() + to_index, std::move(moved));
    return replace_payloads(std::move(*payloads));
}

} // namespace cricodecs::acx
