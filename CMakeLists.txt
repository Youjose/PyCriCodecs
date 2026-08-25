cmake_minimum_required(VERSION 3.20...4.2)
set(CRICODECS_VERSION "1.2.0" CACHE STRING "CriCodecs release version")
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" cricodecs_numeric_version "${CRICODECS_VERSION}")
if(NOT cricodecs_numeric_version)
    message(FATAL_ERROR "CRICODECS_VERSION must begin with major.minor.patch")
endif()
set(CRICODECS_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(CRICODECS_VERSION_MINOR "${CMAKE_MATCH_2}")
set(CRICODECS_VERSION_PATCH "${CMAKE_MATCH_3}")
project(CriCodecs VERSION "${cricodecs_numeric_version}" LANGUAGES CXX)

option(CRICODECS_BUILD_TESTS "Build the active GoogleTest suites" ON)
option(CRICODECS_BUILD_PYTHON "Build the experimental Python binding prototype" OFF)
option(CRICODECS_BUILD_CLI "Build the shared CriCodecs CLI executable" ON)
option(CRICODECS_BUILD_CRISTUDIO "Build the CriStudio desktop GUI" OFF)
option(CRICODECS_INCLUDE_CLI_API "Compile the shared CLI API into the core library" OFF)
option(BUILD_SHARED_LIBS "Build CriCodecs as a shared library" OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(cricodecs_install_cpp_default ON)
else()
    set(cricodecs_install_cpp_default OFF)
endif()
option(CRICODECS_INSTALL_CPP "Install the standalone C++ library and CMake package" ${cricodecs_install_cpp_default})
set(CRICODECS_PREBUILT_CORE_PREFIX "" CACHE PATH "Use an installed static CriCodecs core instead of rebuilding it")

set(CRICODECS_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(${CMAKE_CURRENT_SOURCE_DIR}/cricodecs_library.cmake)

if(CRICODECS_PREBUILT_CORE_PREFIX)
    if(NOT IS_ABSOLUTE "${CRICODECS_PREBUILT_CORE_PREFIX}")
        cmake_path(ABSOLUTE_PATH CRICODECS_PREBUILT_CORE_PREFIX
            BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            NORMALIZE
        )
    endif()
    find_package(CriCodecs CONFIG REQUIRED
        PATHS "${CRICODECS_PREBUILT_CORE_PREFIX}"
        NO_DEFAULT_PATH
    )
    add_library(CriCodecs ALIAS CriCodecs::CriCodecs)
elseif(NOT TARGET CriCodecs)
    cricodecs_add_library(CriCodecs)
endif()

if(CRICODECS_INSTALL_CPP)
    if(CRICODECS_PREBUILT_CORE_PREFIX)
        message(FATAL_ERROR "CRICODECS_INSTALL_CPP cannot reinstall CRICODECS_PREBUILT_CORE_PREFIX")
    endif()
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(
        TARGETS CriCodecs
        EXPORT CriCodecsTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(
        FILES "${CMAKE_CURRENT_SOURCE_DIR}/CriCodecs/include/cricodecs/cricodecs.hpp"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cricodecs"
    )
    get_target_property(cricodecs_generated_version_header CriCodecs CRICODECS_GENERATED_VERSION_HEADER)
    install(
        FILES "${cricodecs_generated_version_header}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cricodecs"
    )
    set(cricodecs_public_headers
        aax/aax_container.hpp
        acb/acb_commands.hpp
        acb/acb_container.hpp
        acb/acb_cue_graph.hpp
        acb/acb_cue_renderer.hpp
        acb/acb_cue_resolver.hpp
        acx/acx_builder.hpp
        acx/acx_container.hpp
        adx/adx_codec.hpp
        adx/adx_crypto.hpp
        adx/adx_key_recovery.hpp
        afs/afs_container.hpp
        ahx/ahx_allocation_tables.hpp
        ahx/ahx_bit_allocation.hpp
        ahx/ahx_codec.hpp
        ahx/ahx_format.hpp
        ahx/ahx_key_recovery.hpp
        ahx/ahx_signal_tables.hpp
        ahx/ahx_table_checks.hpp
        aix/aix_container.hpp
        awb/awb_aac_encryption.hpp
        awb/awb_aac_key_recovery.hpp
        awb/awb_container.hpp
        awb/awb_entry_codec.hpp
        cpk/cpk_container.hpp
        csb/csb_container.hpp
        cvm/cvm_build_script.hpp
        cvm/cvm_builder.hpp
        cvm/cvm_container.hpp
        cvm/cvm_key_recovery.hpp
        cvm/cvm_volume_set.hpp
        hca/hca_codec.hpp
        hca/hca_format.hpp
        hca/hca_header.hpp
        hca/hca_key_recovery.hpp
        key_recovery/key_recovery.hpp
        sfd/sfd_container.hpp
        usm/usm_container.hpp
        usm/usm_crypto.hpp
        usm/usm_key_recovery.hpp
        utf/utf_table.hpp
        utilities/flat_unordered_map.hpp
        utilities/io.hpp
        utilities/io_endian.hpp
        utilities/io_reader.hpp
        utilities/io_writer.hpp
        utilities/numeric.hpp
        utilities/primes.hpp
        utilities/text_encoding.hpp
        video/h264.hpp
        video/ivf.hpp
        video/mpeg.hpp
        wav/wav_container.hpp
    )
    foreach(cricodecs_public_header IN LISTS cricodecs_public_headers)
        get_filename_component(cricodecs_public_header_dir "${cricodecs_public_header}" DIRECTORY)
        install(
            FILES "${CMAKE_CURRENT_SOURCE_DIR}/CriCodecs/src/${cricodecs_public_header}"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cricodecs/${cricodecs_public_header_dir}"
        )
    endforeach()

    get_target_property(CRICODECS_PACKAGE_NEEDS_ICONV CriCodecs CRICODECS_PACKAGE_NEEDS_ICONV)
    set(cricodecs_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/CriCodecs")
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/CriCodecs/cmake/CriCodecsConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CriCodecsConfig.cmake"
        INSTALL_DESTINATION "${cricodecs_package_dir}"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CriCodecsConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion
    )
    install(
        EXPORT CriCodecsTargets
        FILE CriCodecsTargets.cmake
        NAMESPACE CriCodecs::
        DESTINATION "${cricodecs_package_dir}"
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/CriCodecsConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/CriCodecsConfigVersion.cmake"
        DESTINATION "${cricodecs_package_dir}"
    )
endif()

if(CRICODECS_BUILD_CLI)
    add_executable(cricodecs_cli
        CriCodecs/src/cli/cricodecs_cli_main.cpp
    )
    target_link_libraries(cricodecs_cli PRIVATE CriCodecs)
    target_compile_features(cricodecs_cli PRIVATE cxx_std_26)
    set_target_properties(cricodecs_cli PROPERTIES
        OUTPUT_NAME cricodecs
    )
    if(NOT CRICODECS_BUILD_PYTHON)
        install(TARGETS cricodecs_cli RUNTIME DESTINATION .)
        if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
            install(
                FILES $<TARGET_RUNTIME_DLLS:cricodecs_cli>
                DESTINATION .
                OPTIONAL
            )
        endif()
    endif()
endif()

if(CRICODECS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()

if(CRICODECS_BUILD_PYTHON)
    add_subdirectory(PyCriCodecs)
endif()

if(CRICODECS_BUILD_CRISTUDIO)
    add_subdirectory(CriStudio)
endif()
