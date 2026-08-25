/**
 * @file hca_packing.cpp
 * @brief HCA frame bitstream writer.
 *
 * Frame packing follows the public HCA bitstream behavior from VGAudio and vgmstream.
 */

#include "hca_packing.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "../utilities/io.hpp"

namespace cricodecs::hca::packing {

namespace {

using BitWriter = io::bit_writer;
using io::write_be;

void write_scalefactors(BitWriter& writer, const HcaHeader& info, const EncoderChannel& channel) {
    const uint8_t scalefactor_count = scalefactor_count_for_header(info, channel);
    const int delta_bits = channel.scalefactor_delta_bits;
    writer.write(delta_bits, 3);

    if (delta_bits == 0) {
        return;
    }

    if (delta_bits == 6) {
        for (uint8_t band = 0; band < scalefactor_count; ++band) {
            writer.write(channel.scalefactors[band], 6);
        }
        return;
    }

    writer.write(channel.scalefactors[0], 6);
    const int max_delta = (1 << (delta_bits - 1)) - 1;
    const int escape = (1 << delta_bits) - 1;

    for (uint8_t band = 1; band < scalefactor_count; ++band) {
        const int delta = static_cast<int>(channel.scalefactors[band]) - static_cast<int>(channel.scalefactors[band - 1]);
        if (std::abs(delta) > max_delta) {
            writer.write(escape, delta_bits);
            writer.write(channel.scalefactors[band], 6);
        } else {
            writer.write(max_delta + delta, delta_bits);
        }
    }
}

void write_v3_intensity(BitWriter& writer, const EncoderChannel& channel) {
    if (std::all_of(channel.intensity.begin(), channel.intensity.end(), [](uint8_t value) { return value == 7; })) {
        writer.write(15, 4);
        return;
    }

    writer.write(channel.intensity[0], 4);
    writer.write(3, 2);
    for (int subframe = 1; subframe < HCA_SUBFRAMES; ++subframe) {
        writer.write(channel.intensity[subframe], 4);
    }
}

void write_spectra(BitWriter& writer, const EncoderChannel& channel, int subframe) {
    for (uint8_t band = 0; band < channel.coded_count; ++band) {
        const uint8_t resolution = channel.resolution[band];
        if (resolution == 0) {
            continue;
        }

        const int quantized = channel.quantized_spectra[subframe][band];
        if (resolution < 8) {
            const tables::QuantizedSpectrumCode* code = tables::quantized_spectrum_code(resolution, quantized);
            if (code != nullptr) {
                writer.write(code->bits, code->bit_count);
            }
            continue;
        }

        const int bits = tables::QUANTIZED_SPECTRUM_MAX_BITS[resolution];
        writer.write(std::abs(quantized), bits - 1);
        if (quantized != 0) {
            writer.write(quantized > 0 ? 0 : 1, 1);
        }
    }
}

} // namespace

void pack_frame(EncoderFrame& frame, uint8_t* buffer) {
    const auto& info = frame.info;
    write_be<uint16_t>(buffer, 0xFFFF);

    BitWriter writer;
    writer.set_buffer(buffer + 2, info.codec.frame_size - 2);
    writer.write(frame.acceptable_noise_level, 9);
    writer.write(frame.evaluation_boundary, 7);

    for (uint8_t c = 0; c < info.fmt.channel_count; ++c) {
        const auto& channel = frame.channels[c];
        write_scalefactors(writer, info, channel);

        if (channel.type == ChannelType::StereoSecondary) {
            if (detail::uses_v3_frame_layout(info.file.version)) {
                write_v3_intensity(writer, channel);
            } else {
                for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
                    writer.write(channel.intensity[subframe], 4);
                }
            }
        } else if (!detail::uses_v3_frame_layout(info.file.version) && info.codec.hfr_group_count > 0) {
            for (uint8_t group = 0; group < info.codec.hfr_group_count; ++group) {
                writer.write(channel.hfr_scales[group], 6);
            }
        }
    }

    for (int subframe = 0; subframe < HCA_SUBFRAMES; ++subframe) {
        for (uint8_t c = 0; c < info.fmt.channel_count; ++c) {
            write_spectra(writer, frame.channels[c], subframe);
        }
    }

    writer.align(8);
    const size_t used_bytes = writer.position() / 8;
    std::memset(buffer + 2 + used_bytes, 0, info.codec.frame_size - 4 - used_bytes);

    const uint16_t crc = tables::crc16_checksum(buffer, info.codec.frame_size - 2);
    write_be<uint16_t>(buffer + info.codec.frame_size - 2, crc);
}

} // namespace cricodecs::hca::packing
