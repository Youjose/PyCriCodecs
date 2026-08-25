/**
 * @file acb_commands.cpp
 * @brief ACB command-stream parser implementation.
 *
 * Command coverage comes from sample scans, vgmstream, PyCriCodecsEx, and the
 * official libraries. I added the typed dictionary and TLV validation here.
 */

#include "acb_commands.hpp"

#include "../utilities/io_endian.hpp"

#include <cstddef>

namespace cricodecs::acb {

std::string_view command_family_name(AcbCommandFamily family) noexcept {
    switch (family) {
        case AcbCommandFamily::terminator:        return "terminator";
        case AcbCommandFamily::target_reference:  return "target_reference";
        case AcbCommandFamily::timing:            return "timing";
        case AcbCommandFamily::runtime_parameter: return "runtime_parameter";
        case AcbCommandFamily::compact_runtime:   return "compact_runtime";
        case AcbCommandFamily::category:          return "category";
        case AcbCommandFamily::cue_limit:         return "cue_limit";
        case AcbCommandFamily::bus_send:          return "bus_send";
        case AcbCommandFamily::action:            return "action";
        case AcbCommandFamily::selector:          return "selector";
        case AcbCommandFamily::midi:              return "midi";
        case AcbCommandFamily::official_handled:  return "official_handled";
        case AcbCommandFamily::unknown:           return "unknown";
    }

    return "unknown";
}

std::string_view command_payload_kind_name(AcbCommandPayloadKind kind) noexcept {
    switch (kind) {
        case AcbCommandPayloadKind::none:                return "none";
        case AcbCommandPayloadKind::u8:                  return "u8";
        case AcbCommandPayloadKind::target_reference:    return "target_reference";
        case AcbCommandPayloadKind::be_u16:              return "be_u16";
        case AcbCommandPayloadKind::be_i16:              return "be_i16";
        case AcbCommandPayloadKind::be_u32:              return "be_u32";
        case AcbCommandPayloadKind::be_u16_pair:         return "be_u16_pair";
        case AcbCommandPayloadKind::be_u16_pair_u8:      return "be_u16_pair_u8";
        case AcbCommandPayloadKind::be_f32:              return "be_f32";
        case AcbCommandPayloadKind::be_f32_pair:         return "be_f32_pair";
        case AcbCommandPayloadKind::u8_pair:             return "u8_pair";
        case AcbCommandPayloadKind::parameter_curve_7:   return "parameter_curve_7";
        case AcbCommandPayloadKind::parameter_curve_11:  return "parameter_curve_11";
        case AcbCommandPayloadKind::parameter_record_12: return "parameter_record_12";
        case AcbCommandPayloadKind::category_id_list:    return "category_id_list";
        case AcbCommandPayloadKind::bus_name_send:       return "bus_name_send";
        case AcbCommandPayloadKind::sequence_wait_timer: return "sequence_wait_timer";
        case AcbCommandPayloadKind::raw:                 return "raw";
        case AcbCommandPayloadKind::variable:            return "variable";
    }

    return "raw";
}

std::expected<std::vector<AcbCommand>, std::string> parse_command_stream(
    std::span<const uint8_t> data,
    AcbCommandDispatcher dispatcher
) {
    std::vector<AcbCommand> commands;

    size_t pos = 0;
    while (pos < data.size()) {
        if (data.size() - pos < 3) {
            return std::unexpected("ACB command stream has a truncated TLV header");
        }

        const uint16_t code = io::read_be<uint16_t>(data, pos);
        const uint8_t size = data[pos + 2];
        pos += 3;

        if (data.size() - pos < size) {
            return std::unexpected("ACB command stream payload exceeds command data size");
        }

        commands.push_back(AcbCommand{
            .code = code,
            .dispatcher = dispatcher,
            .family = classify_command(dispatcher, code),
            .payload = data.subspan(pos, size),
        });

        pos += size;

        if (code == 0 && size == 0) {
            break;
        }
    }

    return commands;
}

std::optional<AcbCommandTarget> command_target_reference(const AcbCommand& command) noexcept {
    if (!is_target_reference_command(command.code) || command.payload.size() < 4) {
        return std::nullopt;
    }

    const uint16_t raw_type = io::read_be<uint16_t>(command.payload, 0);
    if (raw_type == 0) {
        return std::nullopt;
    }

    // Preserve the raw reference domain even when it postdates the currently
    // known SDK dispatcher. Graph assembly can resolve a known type or expose
    // a precise unresolved edge instead of dropping the event.
    return AcbCommandTarget{
        .type = static_cast<AcbCommandTargetType>(raw_type),
        .index = io::read_be<uint16_t>(command.payload, 2),
    };
}

} // namespace cricodecs::acb
