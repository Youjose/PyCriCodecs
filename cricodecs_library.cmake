function(cricodecs_add_library target_name)
    include(GNUInstallDirs)

    option(CRICODECS_ENABLE_PCH
        "Use a standard-library precompiled header for local and release C++ builds"
        OFF
    )

    if(NOT DEFINED CRICODECS_REPO_ROOT)
        message(FATAL_ERROR "CRICODECS_REPO_ROOT must be set before including cricodecs_library.cmake")
    endif()

    if(NOT DEFINED CRICODECS_VERSION)
        set(CRICODECS_VERSION "1.2.0")
    endif()
    if(NOT DEFINED CRICODECS_VERSION_MAJOR)
        set(CRICODECS_VERSION_MAJOR 1)
    endif()
    if(NOT DEFINED CRICODECS_VERSION_MINOR)
        set(CRICODECS_VERSION_MINOR 2)
    endif()
    if(NOT DEFINED CRICODECS_VERSION_PATCH)
        set(CRICODECS_VERSION_PATCH 0)
    endif()

    set(cricodecs_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/cricodecs-generated")
    set(cricodecs_generated_version_header
        "${cricodecs_generated_include_dir}/cricodecs/version.hpp"
    )
    file(MAKE_DIRECTORY "${cricodecs_generated_include_dir}/cricodecs")
    configure_file(
        "${CRICODECS_REPO_ROOT}/CriCodecs/include/cricodecs/version.hpp.in"
        "${cricodecs_generated_version_header}"
        @ONLY
    )
    set(CRICODECS_SOURCE_REVISION "" CACHE STRING
        "Source revision embedded in CriCodecs CLI build information"
    )
    find_package(Git QUIET)
    if(CRICODECS_SOURCE_REVISION STREQUAL "" AND Git_FOUND AND EXISTS "${CRICODECS_REPO_ROOT}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${CRICODECS_REPO_ROOT}"
            RESULT_VARIABLE CRICODECS_GIT_RESULT
            OUTPUT_VARIABLE CRICODECS_GIT_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(CRICODECS_GIT_RESULT EQUAL 0 AND NOT CRICODECS_GIT_OUTPUT STREQUAL "")
            set(CRICODECS_SOURCE_REVISION "${CRICODECS_GIT_OUTPUT}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
                WORKING_DIRECTORY "${CRICODECS_REPO_ROOT}"
                RESULT_VARIABLE CRICODECS_GIT_DIRTY_RESULT
                ERROR_QUIET
            )
            if(NOT CRICODECS_GIT_DIRTY_RESULT EQUAL 0)
                string(APPEND CRICODECS_SOURCE_REVISION "-dirty")
            endif()
        endif()
    endif()
    if(CRICODECS_SOURCE_REVISION STREQUAL "" AND
       EXISTS "${CRICODECS_REPO_ROOT}/CriCodecs/source_revision.txt")
        file(STRINGS
            "${CRICODECS_REPO_ROOT}/CriCodecs/source_revision.txt"
            CRICODECS_SOURCE_REVISION
            LIMIT_COUNT 1
        )
    endif()
    if(CRICODECS_SOURCE_REVISION STREQUAL "" AND NOT "$ENV{CRICODECS_SOURCE_REVISION}" STREQUAL "")
        set(CRICODECS_SOURCE_REVISION "$ENV{CRICODECS_SOURCE_REVISION}")
    endif()
    if(CRICODECS_SOURCE_REVISION MATCHES "^[0-9A-Fa-f]+$")
        string(LENGTH "${CRICODECS_SOURCE_REVISION}" CRICODECS_SOURCE_REVISION_LENGTH)
        if(CRICODECS_SOURCE_REVISION_LENGTH GREATER 12)
            string(SUBSTRING "${CRICODECS_SOURCE_REVISION}" 0 12 CRICODECS_SOURCE_REVISION)
        endif()
    endif()
    if(CRICODECS_SOURCE_REVISION STREQUAL "")
        set(CRICODECS_SOURCE_REVISION "unknown")
    elseif(NOT CRICODECS_SOURCE_REVISION MATCHES "^[0-9A-Za-z._+-]+$")
        message(FATAL_ERROR
            "CRICODECS_SOURCE_REVISION must contain only revision-safe characters"
        )
    endif()

    set(cricodecs_sources
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aax/aax_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx/acx_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx/acx_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acx/acx_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_recovery_source_collector.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_commands.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_cue_graph.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_cue_graph_views.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_cue_renderer.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_cue_resolver.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx/ahx_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx/ahx_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx/ahx_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs/afs_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs/afs_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/afs/afs_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix/aix_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix/aix_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/awb/awb_aac_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_build_script.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_volume_set.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb/csb_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb/csb_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/csb/csb_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/cpk_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/cpk_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/crilayla.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_crypto.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_packing.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/hca/hca_transform.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_crypto.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_adx_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_subtitle.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video/h264.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video/ivf.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/video/mpeg.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/wav/wav_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf/utf_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf/utf_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utf/utf_table.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities/io_reader_platform.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities/io_writer_platform.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities/text_encoding.cpp
    )
    if(CRICODECS_BUILD_CLI OR CRICODECS_BUILD_PYTHON OR CRICODECS_INCLUDE_CLI_API)
        list(APPEND cricodecs_sources
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_maker.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_common.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_export.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_listing.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_metadata.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_parse.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_probe.cpp
            ${CRICODECS_REPO_ROOT}/CriCodecs/src/cli/cli_key_recovery.cpp
        )
    endif()

    add_library(${target_name} ${cricodecs_sources})

    # Some source files intentionally use the same names in separate anonymous
    # namespaces. Keep those files as normal translation units when a developer
    # enables CMake unity builds; this preserves their existing scope semantics
    # while allowing the rest of the target to use unity compilation.
    set(cricodecs_unity_safe_exclusions
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_decoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/adx/adx_encoder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/acb/acb_container.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix/aix_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/aix/aix_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/cpk_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk/cpk_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm/cvm_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_builder.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd/sfd_reader.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_adx_key_recovery.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_crypto.cpp
        ${CRICODECS_REPO_ROOT}/CriCodecs/src/usm/usm_key_recovery.cpp
    )
    set_source_files_properties(
        ${cricodecs_unity_safe_exclusions}
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
    )
    if(target_name STREQUAL "CriCodecs" AND NOT TARGET CriCodecs::CriCodecs)
        add_library(CriCodecs::CriCodecs ALIAS ${target_name})
    endif()

    find_package(Threads REQUIRED)
    target_compile_features(${target_name} PUBLIC cxx_std_26)
    target_compile_options(${target_name} PUBLIC
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-freflection>
    )

    if(CRICODECS_ENABLE_PCH)
        target_precompile_headers(${target_name} PRIVATE
            <algorithm>
            <array>
            <bit>
            <cmath>
            <concepts>
            <cstddef>
            <cstdint>
            <cstring>
            <expected>
            <filesystem>
            <fstream>
            <functional>
            <limits>
            <map>
            <optional>
            <span>
            <string>
            <string_view>
            <type_traits>
            <unordered_map>
            <utility>
            <vector>
        )
    endif()
    target_link_libraries(${target_name} PUBLIC Threads::Threads)
    target_compile_definitions(${target_name} PRIVATE
        CRICODECS_VERSION="${CRICODECS_VERSION}"
        CRICODECS_SOURCE_REVISION="${CRICODECS_SOURCE_REVISION}"
    )
    set_target_properties(${target_name} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_SCAN_FOR_MODULES OFF
        CRICODECS_GENERATED_VERSION_HEADER "${cricodecs_generated_version_header}"
        CRICODECS_SOURCE_REVISION "${CRICODECS_SOURCE_REVISION}"
        CRICODECS_PACKAGE_NEEDS_ICONV OFF
    )

    if(BUILD_SHARED_LIBS)
        set_target_properties(${target_name} PROPERTIES
            VERSION "${CRICODECS_VERSION_MAJOR}.${CRICODECS_VERSION_MINOR}.${CRICODECS_VERSION_PATCH}"
            SOVERSION "${CRICODECS_VERSION_MAJOR}"
        )
        if(WIN32)
            set_target_properties(${target_name} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
        endif()
    endif()

    target_include_directories(${target_name} PUBLIC
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/include>
        $<BUILD_INTERFACE:${cricodecs_generated_include_dir}>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/aax>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/acx>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/utilities>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/acb>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/adx>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/ahx>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/afs>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/aix>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/awb>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/cli>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/csb>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/cpk>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/cvm>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/key_recovery>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/hca>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/sfd>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/usm>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/video>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/wav>
        $<BUILD_INTERFACE:${CRICODECS_REPO_ROOT}/CriCodecs/src/utf>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    if(NOT MSVC)
        find_package(Iconv QUIET)
        if(Iconv_FOUND)
            if(TARGET Iconv::Iconv)
                target_link_libraries(${target_name} PUBLIC Iconv::Iconv)
                set_target_properties(${target_name} PROPERTIES CRICODECS_PACKAGE_NEEDS_ICONV ON)
            elseif(Iconv_LIBRARIES)
                target_link_libraries(${target_name} PUBLIC ${Iconv_LIBRARIES})
                set_target_properties(${target_name} PROPERTIES CRICODECS_PACKAGE_NEEDS_ICONV ON)
            endif()
        endif()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra)
    endif()
endfunction()
