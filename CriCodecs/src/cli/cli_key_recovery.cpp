#include "cli_internal.hpp"

namespace cricodecs::cli::detail {

namespace {

void print_cri_key_recovery_note(std::ostream& out) {
    out << "note: CRI key recovery returns only the effective low 56 bits; "
           "the original upper byte is unrecoverable.\n";
}

[[nodiscard]] bool is_hca_payload(std::span<const uint8_t> bytes) {
    const auto formats = sniff_format_order(bytes, false);
    return std::ranges::find(formats, Format::hca) != formats.end();
}

[[nodiscard]] bool has_any_format(
    const std::filesystem::path& path,
    std::initializer_list<Format> accepted
) {
    const auto formats = sniff_format_order(path, false);
    return std::ranges::any_of(accepted, [&](Format format) {
        return std::ranges::contains(formats, format);
    });
}

struct RecoveryPath {
    std::filesystem::path path;
    bool explicit_input = false;
};

template <class Accept>
[[nodiscard]] std::expected<std::vector<RecoveryPath>, std::string> expand_recovery_paths(
    std::span<const std::filesystem::path> input_paths,
    Accept accept
) {
    std::vector<RecoveryPath> paths;
    for (const auto& input : input_paths) {
        if (!std::filesystem::is_directory(input)) {
            paths.push_back({.path = input, .explicit_input = true});
            continue;
        }

        auto files = collect_directory_files(input);
        if (!files) {
            return std::unexpected(files.error());
        }
        for (const auto& [path, _] : *files) {
            if (accept(path)) {
                paths.push_back({.path = path, .explicit_input = false});
            }
        }
    }
    return paths;
}

[[nodiscard]] std::expected<std::vector<std::vector<uint8_t>>, std::string>
collect_adx_family_sources(
    std::span<const std::filesystem::path> input_paths,
    bool want_ahx
) {
    auto paths = expand_recovery_paths(input_paths, [](const auto& path) {
        return has_any_format(path, {
            Format::adx, Format::aax, Format::awb, Format::acb, Format::csb, Format::cpk
        });
    });
    if (!paths) {
        return std::unexpected(paths.error());
    }

    const std::string_view label = want_ahx ? "AHX" : "ADX";
    std::vector<std::vector<uint8_t>> sources;
    for (const auto& input : *paths) {
        auto collected = adx::collect_recovery_streams(
            input.path,
            want_ahx ? adx::RecoveryStreamKind::Ahx : adx::RecoveryStreamKind::Adx);
        if (!collected) {
            if (!input.explicit_input) {
                continue;
            }
            return std::unexpected(std::string(label) + " key recovery failed: " + collected.error());
        }
        sources.insert(
            sources.end(),
            std::make_move_iterator(collected->begin()),
            std::make_move_iterator(collected->end()));
    }
    if (sources.empty()) {
        return std::unexpected(std::string(label) + " key recovery failed: inputs contain no encrypted " +
            std::string(label) + " files");
    }
    return sources;
}

struct CollectedHca {
    hca::Hca hca;
    uint16_t subkey = 0;
    size_t group = 0;
};

[[nodiscard]] std::expected<void, std::string> append_awb_hcas(
    const awb::AwbContainer& archive,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    for (uint32_t index = 0; index < archive.file_count(); ++index) {
        auto payload = archive.file_data(index);
        if (!payload) {
            return std::unexpected(payload.error());
        }
        if (!is_hca_payload(*payload)) {
            continue;
        }
        auto source = hca::Hca::load(*payload);
        if (!source) {
            return std::unexpected(
                "HCA key recovery failed: AWB entry " + std::to_string(index)
                + " could not be loaded: " + source.error());
        }
        if (source->header().cipher.type == 56) {
            sources.push_back(CollectedHca{
                .hca = std::move(*source),
                .subkey = archive.subkey(),
                .group = group,
            });
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, std::string> append_usm_hcas(
    usm::UsmReader& movie,
    size_t group,
    std::vector<CollectedHca>& sources
) {
    for (size_t index = 0; index < movie.streams().size(); ++index) {
        if (movie.streams()[index].stream_id != usm::UsmChunkType::SFA) {
            continue;
        }
        auto payload = movie.extract_stream(static_cast<uint32_t>(index));
        if (!payload) {
            return std::unexpected(
                "HCA key recovery failed: USM audio stream " + std::to_string(index)
                + " could not be extracted: " + payload.error());
        }
        if (!is_hca_payload(*payload)) {
            continue;
        }
        auto source = hca::Hca::load(*payload);
        if (!source) {
            return std::unexpected(
                "HCA key recovery failed: USM audio stream " + std::to_string(index)
                + " could not be loaded: " + source.error());
        }
        if (source->header().cipher.type == 56) {
            sources.push_back(CollectedHca{
                .hca = std::move(*source),
                .subkey = 0,
                .group = group,
            });
        }
    }
    return {};
}

[[nodiscard]] std::string key_text(uint64_t key) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(14)
           << std::setfill('0') << key;
    return stream.str();
}

[[nodiscard]] std::string aac_key_text(uint64_t key) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(13)
           << std::setfill('0') << key;
    return stream.str();
}

[[nodiscard]] std::string triplet_text(uint16_t start, uint16_t mult, uint16_t add) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << start
           << ",0x" << std::setw(4) << mult
           << ",0x" << std::setw(4) << add;
    return stream.str();
}

void print_u64_list(std::ostream& out, std::span<const uint64_t> values) {
    for (size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << values[index];
    }
}

} // namespace

std::expected<hca::KeyRecoveryResult, std::string> perform_hca_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
) {
    auto paths = expand_recovery_paths(input_paths, [](const auto& path) {
        return has_any_format(path, {Format::hca, Format::awb, Format::acb, Format::usm});
    });
    if (!paths) {
        return std::unexpected(paths.error());
    }

    std::vector<CollectedHca> collected;
    Options load_options = options;
    load_options.force_type.reset();
    for (size_t group = 0; group < paths->size(); ++group) {
        const auto& input = (*paths)[group];
        auto loaded = load_best_effort(input.path, load_options);
        if (!loaded) {
            if (!input.explicit_input) {
                continue;
            }
            return std::unexpected(loaded.error());
        }

        if (auto* source = std::get_if<hca::Hca>(&loaded->document)) {
            collected.push_back(CollectedHca{
                .hca = std::move(*source),
                .subkey = 0,
                .group = group,
            });
            continue;
        }
        if (const auto* archive = std::get_if<awb::AwbContainer>(&loaded->document)) {
            if (auto appended = append_awb_hcas(*archive, group, collected); !appended) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected(appended.error());
            }
            continue;
        }
        if (const auto* container = std::get_if<acb::AcbContainer>(&loaded->document)) {
            auto archive = container->load_awb();
            if (!archive) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected(archive.error());
            }
            if (auto appended = append_awb_hcas(*archive, group, collected); !appended) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected(appended.error());
            }
            continue;
        }
        if (auto* movie = std::get_if<usm::UsmReader>(&loaded->document)) {
            if (auto appended = append_usm_hcas(*movie, group, collected); !appended) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected(appended.error());
            }
            continue;
        }
        if (!input.explicit_input) {
            continue;
        }
        return std::unexpected(
            "HCA key recovery failed: input `" + input.path.string()
            + "` is not an HCA, AWB, ACB, or USM file");
    }

    if (collected.empty()) {
        return std::unexpected("HCA key recovery failed: inputs contain no cipher type-56 HCA payloads");
    }
    std::vector<hca::RecoverySource> sources;
    sources.reserve(collected.size());
    for (const auto& source : collected) {
        sources.push_back(hca::RecoverySource{
            .hca = &source.hca,
            .subkey = source.subkey,
            .group = source.group,
        });
    }
    auto recovery = hca::recover_key(
        sources,
        options.independent_key_recovery
            ? KeyRecoveryMode::Independent
            : KeyRecoveryMode::SharedBaseKey);
    if (!recovery) {
        return std::unexpected(recovery.error());
    }
    return std::move(*recovery);
}

void print_hca_key_recovery_text(std::ostream& out, const hca::KeyRecoveryResult& result) {
    print_cri_key_recovery_note(out);
    out << "candidates: " << result.candidates.size() << '\n';
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        const auto& candidate = result.candidates[index];
        out << index + 1 << ". key: " << key_text(candidate.key)
            << ", score: " << std::fixed << std::setprecision(6) << candidate.score
            << ", files: " << candidate.source_count
            << ", evidence: " << candidate.evidence_count
            << ", equivalents: " << candidate.equivalent_count << '\n';
    }
    out << "hca_count: " << result.source_count << '\n';
}

void print_hca_key_recovery_json(std::ostream& out, const hca::KeyRecoveryResult& result) {
    out << "{\"candidates\":[";
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto& candidate = result.candidates[index];
        out << "{\"key\":" << quote_json(key_text(candidate.key))
            << ",\"score\":" << std::fixed << std::setprecision(6) << candidate.score
            << ",\"source_count\":" << candidate.source_count
            << ",\"evidence_count\":" << candidate.evidence_count
            << ",\"unknown_high_bits\":" << static_cast<unsigned>(candidate.unknown_high_bits)
            << ",\"equivalent_count\":" << candidate.equivalent_count << '}';
    }
    out << "],\"hca_count\":" << result.source_count << '}';
}

std::expected<AacRecoveryOutput, std::string> perform_aac_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    Format container_format,
    const Options& options
) {
    auto paths = expand_recovery_paths(input_paths, [container_format](const auto& path) {
        return has_any_format(path, {container_format});
    });
    if (!paths) {
        return std::unexpected(paths.error());
    }

    std::vector<awb::KeyCandidate> combined;
    size_t container_count = 0;
    for (const auto& input : *paths) {
        std::expected<awb::KeyRecoveryResult, std::string> recovered = std::unexpected("not attempted");
        if (container_format == Format::acb) {
            auto container = acb::AcbContainer::load(input.path);
            if (!container) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected("ACB AAC key recovery failed: " + container.error());
            }
            if (!container->has_aac_waveforms()) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected(
                    "ACB AAC key recovery failed: input `" + input.path.string() +
                    "` contains no AAC/M4A waveforms");
            }
            recovered = container->recover_aac_key();
        } else {
            auto container = awb::AwbContainer::load(input.path);
            if (!container) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected("AWB AAC key recovery failed: " + container.error());
            }
            if (!container->has_aac_key_recovery_candidates()) {
                if (!input.explicit_input) {
                    continue;
                }
                return std::unexpected(
                    "AWB AAC key recovery failed: input `" + input.path.string() +
                    "` contains no encrypted AAC/M4A entry group");
            }
            recovered = container->recover_aac_key();
        }

        if (!recovered) {
            if (!input.explicit_input) {
                continue;
            }
            return std::unexpected(recovered.error());
        }
        if (combined.empty()) {
            combined = recovered->candidates;
        } else if (options.independent_key_recovery) {
            for (const auto& candidate : recovered->candidates) {
                auto existing = std::ranges::find(combined, candidate.key, &awb::KeyCandidate::key);
                if (existing == combined.end()) {
                    combined.push_back(candidate);
                } else {
                    const size_t count = existing->source_count + candidate.source_count;
                    existing->score = static_cast<float>(
                        (static_cast<double>(existing->score) * existing->source_count +
                         static_cast<double>(candidate.score) * candidate.source_count) / count);
                    existing->source_count = count;
                    existing->validated_sources += candidate.validated_sources;
                    existing->candidate_count += candidate.candidate_count;
                }
            }
        } else {
            std::vector<awb::KeyCandidate> intersection;
            for (const auto& left : combined) {
                const auto right = std::ranges::find(recovered->candidates, left.key, &awb::KeyCandidate::key);
                if (right == recovered->candidates.end()) continue;
                auto candidate = left;
                const size_t count = left.source_count + right->source_count;
                candidate.score = static_cast<float>(
                    (static_cast<double>(left.score) * left.source_count +
                     static_cast<double>(right->score) * right->source_count) / count);
                candidate.source_count = count;
                candidate.validated_sources += right->validated_sources;
                candidate.candidate_count += right->candidate_count;
                intersection.push_back(candidate);
            }
            if (intersection.empty()) {
                return std::unexpected("AAC key recovery failed: selected containers do not share one effective key candidate");
            }
            combined = std::move(intersection);
        }
        ++container_count;
    }

    if (combined.empty()) {
        return std::unexpected(
            "AAC key recovery failed: inputs contain no supported encrypted AAC/M4A payloads");
    }
    std::ranges::sort(combined, [](const auto& left, const auto& right) {
        if (left.source_count != right.source_count) return left.source_count > right.source_count;
        if (left.score != right.score) return left.score > right.score;
        return left.key < right.key;
    });
    if (combined.size() > MaxKeyRecoveryCandidates) combined.resize(MaxKeyRecoveryCandidates);
    size_t source_count = 0;
    for (const auto& candidate : combined) source_count = std::max(source_count, candidate.source_count);
    return AacRecoveryOutput{
        .recovery = awb::KeyRecoveryResult{
            .candidates = std::move(combined),
            .source_count = source_count,
            .evidence_count = source_count,
        },
        .container_count = container_count,
    };
}

void print_aac_key_recovery_text(std::ostream& out, const AacRecoveryOutput& result) {
    for (size_t index = 0; index < result.recovery.candidates.size(); ++index) {
        const auto& candidate = result.recovery.candidates[index];
        out << index + 1 << ". key: " << aac_key_text(candidate.key)
            << ", score: " << std::fixed << std::setprecision(6) << candidate.score
            << ", validated_sources: " << candidate.validated_sources
            << ", source_count: " << candidate.source_count
            << ", candidates: " << candidate.candidate_count << '\n';
    }
    out << "container_count: " << result.container_count << '\n';
}

void print_aac_key_recovery_json(std::ostream& out, const AacRecoveryOutput& result) {
    out << "{\"candidates\":[";
    for (size_t index = 0; index < result.recovery.candidates.size(); ++index) {
        if (index != 0) out << ',';
        const auto& candidate = result.recovery.candidates[index];
        out << "{\"key\":" << quote_json(aac_key_text(candidate.key))
            << ",\"score\":" << std::fixed << std::setprecision(6) << candidate.score
            << ",\"validated_sources\":" << candidate.validated_sources
            << ",\"source_count\":" << candidate.source_count
            << ",\"candidate_count\":" << candidate.candidate_count << '}';
    }
    out << "],\"container_count\":" << result.container_count << '}';
}

std::expected<adx::AdxRecoveryResult, std::string> perform_adx_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
) {
    auto bytes = collect_adx_family_sources(input_paths, false);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<adx::AdxRecoverySource> sources;
    sources.reserve(bytes->size());
    for (const auto& source : *bytes) {
        sources.push_back({.bytes = source});
    }
    auto guess = adx::recover_key(
        sources,
        options.independent_key_recovery
            ? KeyRecoveryMode::Independent
            : KeyRecoveryMode::SharedBaseKey);
    if (!guess) {
        return std::unexpected("ADX key recovery failed: " + guess.error());
    }
    return std::move(*guess);
}

void print_adx_key_recovery_text(std::ostream& out, const adx::AdxRecoveryResult& result) {
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        const auto& candidate = result.candidates[index];
        out << index + 1 << ". key: " << triplet_text(
                candidate.key.xor_value, candidate.key.mult, candidate.key.add)
            << ", score: " << std::fixed << std::setprecision(6) << candidate.score
            << ", sources: " << candidate.source_count
            << ", evidence: " << candidate.evidence_count << '\n';
    }
    out << "encryption_type: " << static_cast<unsigned>(result.encryption_type) << '\n'
        << "source_count: " << result.source_count << '\n'
        << "source_frames: ";
    print_u64_list(out, result.source_frames);
    out << '\n'
        << "total_frames: " << result.total_frames << '\n'
        << "examined_frames: " << result.examined_frames << '\n'
        << "evidence_frames: " << result.evidence_frames << '\n';
    if (result.encryption_type == 9u) {
        out << "canonical_type9_code: " << hex_text(result.canonical_type9_code) << '\n';
    }
}

void print_adx_key_recovery_json(std::ostream& out, const adx::AdxRecoveryResult& result) {
    out << "{\"candidates\":[";
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        if (index != 0) out << ',';
        const auto& candidate = result.candidates[index];
        out << "{\"key\":" << quote_json(triplet_text(
                candidate.key.xor_value, candidate.key.mult, candidate.key.add))
            << ",\"score\":" << std::fixed << std::setprecision(6) << candidate.score
            << ",\"source_count\":" << candidate.source_count
            << ",\"evidence_count\":" << candidate.evidence_count
            << ",\"canonical_type9_code\":";
        if (result.encryption_type == 9u) {
            out << quote_json(hex_text(candidate.canonical_type9_code));
        } else {
            out << "null";
        }
        out << '}';
    }
    out << "],\"encryption_type\":" << static_cast<unsigned>(result.encryption_type)
        << ",\"source_count\":" << result.source_count
        << ",\"source_frames\":[";
    print_u64_list(out, result.source_frames);
    out << "]"
        << ",\"total_frames\":" << result.total_frames
        << ",\"examined_frames\":" << result.examined_frames
        << ",\"evidence_frames\":" << result.evidence_frames
        << ",\"canonical_type9_code\":";
    if (result.encryption_type == 9u) {
        out << quote_json(hex_text(result.canonical_type9_code));
    } else {
        out << "null";
    }
    out << '}';
}

std::expected<ahx::AhxRecoveryResult, std::string> perform_ahx_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
) {
    auto bytes = collect_adx_family_sources(input_paths, true);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::vector<ahx::AhxRecoverySource> sources;
    sources.reserve(bytes->size());
    for (const auto& source : *bytes) {
        sources.push_back({.bytes = source});
    }
    auto guess = ahx::recover_key(
        sources,
        options.independent_key_recovery
            ? KeyRecoveryMode::Independent
            : KeyRecoveryMode::SharedBaseKey);
    if (!guess) {
        return std::unexpected("AHX key recovery failed: " + guess.error());
    }
    return std::move(*guess);
}

void print_ahx_key_recovery_text(std::ostream& out, const ahx::AhxRecoveryResult& result) {
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        const auto& candidate = result.candidates[index];
        out << index + 1 << ". key: " << triplet_text(
                candidate.key.start, candidate.key.mult, candidate.key.add)
            << ", score: " << std::fixed << std::setprecision(6) << candidate.score
            << ", sources: " << candidate.source_count
            << ", evidence: " << candidate.evidence_count
            << ", ambiguity: " << candidate.candidate_counts[0] << ','
            << candidate.candidate_counts[1] << ',' << candidate.candidate_counts[2] << '\n';
    }
    out << "encryption_type: " << static_cast<unsigned>(result.encryption_type) << '\n'
        << "source_count: " << result.source_count << '\n'
        << "source_frames: ";
    print_u64_list(out, result.source_frames);
    out << '\n'
        << "total_frames: " << result.total_frames << '\n'
        << "evidence_frames: " << result.evidence_frames << '\n'
        << "component_frames: " << result.component_frames[0] << ','
        << result.component_frames[1] << ',' << result.component_frames[2] << '\n'
        << "candidate_counts: " << result.candidate_counts[0] << ','
        << result.candidate_counts[1] << ',' << result.candidate_counts[2] << '\n';
    if (result.encryption_type == 9u) {
        out << "canonical_type9_code: " << hex_text(result.canonical_type9_code) << '\n';
    }
}

void print_ahx_key_recovery_json(std::ostream& out, const ahx::AhxRecoveryResult& result) {
    out << "{\"candidates\":[";
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        if (index != 0) out << ',';
        const auto& candidate = result.candidates[index];
        out << "{\"key\":" << quote_json(triplet_text(
                candidate.key.start, candidate.key.mult, candidate.key.add))
            << ",\"score\":" << std::fixed << std::setprecision(6) << candidate.score
            << ",\"source_count\":" << candidate.source_count
            << ",\"evidence_count\":" << candidate.evidence_count
            << ",\"candidate_counts\":[" << candidate.candidate_counts[0] << ','
            << candidate.candidate_counts[1] << ',' << candidate.candidate_counts[2] << ']'
            << ",\"canonical_type9_code\":";
        if (result.encryption_type == 9u) {
            out << quote_json(hex_text(candidate.canonical_type9_code));
        } else {
            out << "null";
        }
        out << '}';
    }
    out << "],\"encryption_type\":" << static_cast<unsigned>(result.encryption_type)
        << ",\"source_count\":" << result.source_count
        << ",\"source_frames\":[";
    print_u64_list(out, result.source_frames);
    out << "]"
        << ",\"total_frames\":" << result.total_frames
        << ",\"evidence_frames\":" << result.evidence_frames
        << ",\"component_frames\":[" << result.component_frames[0] << ','
        << result.component_frames[1] << ',' << result.component_frames[2] << ']'
        << ",\"candidate_counts\":[" << result.candidate_counts[0] << ','
        << result.candidate_counts[1] << ',' << result.candidate_counts[2] << ']'
        << ",\"canonical_type9_code\":";
    if (result.encryption_type == 9u) {
        out << quote_json(hex_text(result.canonical_type9_code));
    } else {
        out << "null";
    }
    out << '}';
}

std::expected<std::vector<UsmRecoveryOutput>, std::string> perform_usm_key_recovery(
    std::span<const std::filesystem::path> input_paths,
    const Options& options
) {
    auto paths = expand_recovery_paths(input_paths, [](const auto& path) {
        return has_any_format(path, {Format::usm});
    });
    if (!paths) {
        return std::unexpected(paths.error());
    }

    std::vector<UsmRecoveryOutput> results;
    results.reserve(paths->size());
    Options load_options = options;
    load_options.force_type.reset();
    for (const auto& input : *paths) {
        auto loaded = load_best_effort(input.path, load_options);
        if (!loaded) {
            if (!input.explicit_input) {
                continue;
            }
            return std::unexpected(loaded.error());
        }
        const auto* movie = std::get_if<usm::UsmReader>(&loaded->document);
        if (movie == nullptr) {
            if (!input.explicit_input) {
                continue;
            }
            return std::unexpected(
                "USM key recovery failed: input `" + input.path.string() + "` is not a USM file");
        }
        auto guess = usm::recover_key(*movie);
        if (!guess) {
            return std::unexpected(
                "USM key recovery failed for `" + input.path.string() + "`: " + guess.error());
        }
        results.push_back(UsmRecoveryOutput{
            .input_path = input.path,
            .recovery = std::move(*guess),
        });
    }
    if (results.empty()) {
        return std::unexpected("USM key recovery failed: inputs contain no recoverable USM files");
    }
    if (!options.independent_key_recovery && results.size() > 1u) {
        std::map<uint64_t, size_t> support;
        for (const auto& result : results) {
            for (const auto& candidate : result.recovery.candidates) {
                ++support[candidate.key];
            }
        }
        for (auto& result : results) {
            std::erase_if(result.recovery.candidates, [&](const auto& candidate) {
                return support[candidate.key] != results.size();
            });
            if (result.recovery.candidates.empty()) {
                return std::unexpected(
                    "USM key recovery failed: no candidate is shared by every supplied USM; use --independent for unrelated keys");
            }
        }
    }
    return results;
}

void print_usm_key_recovery_text(std::ostream& out, std::span<const UsmRecoveryOutput> results) {
    print_cri_key_recovery_note(out);
    for (size_t index = 0; index < results.size(); ++index) {
        if (index != 0) {
            out << '\n';
        }
        const auto& result = results[index];
        out << "input: " << result.input_path.string() << '\n';
        for (size_t candidate_index = 0; candidate_index < result.recovery.candidates.size(); ++candidate_index) {
            const auto& candidate = result.recovery.candidates[candidate_index];
            out << candidate_index + 1 << ". key: " << key_text(candidate.key)
                << ", score: " << std::fixed << std::setprecision(6) << candidate.score
                << ", sample_blocks: " << candidate.sample_blocks
                << ", video_chunks: " << candidate.video_chunks
                << ", audio_chunks: " << candidate.audio_chunks
                << ", audio_score: " << candidate.audio_score
                << ", hca_streams: " << candidate.hca_streams
                << ", hca_score: " << candidate.hca_score
                << ", hca_video_supported: " << bool_text(candidate.hca_video_supported) << '\n';
        }
    }
}

void print_usm_key_recovery_json(std::ostream& out, std::span<const UsmRecoveryOutput> results) {
    out << "{\"results\":[";
    for (size_t index = 0; index < results.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const auto& result = results[index];
        out << "{\"input\":" << quote_json(result.input_path.string()) << ",\"candidates\":[";
        for (size_t candidate_index = 0; candidate_index < result.recovery.candidates.size(); ++candidate_index) {
            if (candidate_index != 0) out << ',';
            const auto& candidate = result.recovery.candidates[candidate_index];
            out << "{\"key\":" << quote_json(key_text(candidate.key))
                << ",\"score\":" << std::fixed << std::setprecision(6) << candidate.score
                << ",\"sample_blocks\":" << candidate.sample_blocks
                << ",\"video_chunks\":" << candidate.video_chunks
                << ",\"audio_chunks\":" << candidate.audio_chunks
                << ",\"audio_score\":" << candidate.audio_score
                << ",\"hca_streams\":" << candidate.hca_streams
                << ",\"hca_score\":" << candidate.hca_score
                << ",\"hca_video_supported\":" << bool_text(candidate.hca_video_supported) << '}';
        }
        out << "]}";
    }
    out << "]}";
}

} // namespace cricodecs::cli::detail
