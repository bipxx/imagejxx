cmake_minimum_required(VERSION 3.18)
project(imagejxx VERSION 0.2.0 LANGUAGES C CXX)
option(IMAGEJXX_BUILD_PYTHON "Build the NumPy Python extension" ON)
option(IMAGEJXX_BUILD_CPP_UI "Build the standalone Dear ImGui desktop application" OFF)
option(IMAGEJXX_BUILD_PYTHON_UI "Build optional Python bindings to the Dear ImGui desktop" OFF)
set(IMAGEJXX_DEFAULT_IO OFF)
if(IMAGEJXX_BUILD_CPP_UI OR IMAGEJXX_BUILD_PYTHON_UI)
  set(IMAGEJXX_DEFAULT_IO ON)
endif()
option(IMAGEJXX_BUILD_IO "Build scientific Dataset TIFF IO (enabled by default for desktop builds)" ${IMAGEJXX_DEFAULT_IO})
include(GNUInstallDirs)
include(cmake/StaticCodecSymbols.cmake)

add_library(imagejxx_core STATIC
  src/native/playback.cpp
  src/native/auto_threshold.cpp
  src/native/avi_movie.cpp
  src/native/image.cpp src/native/filters.cpp src/native/shadow.cpp src/native/gaussian.cpp src/native/gaussian_command.cpp src/native/rank.cpp src/native/rank_command.cpp src/native/axis_extraction_command.cpp src/native/transforms.cpp
  src/native/segmentation.cpp src/native/analysis.cpp src/native/frequency.cpp src/native/binary.cpp src/native/conversion.cpp
  src/native/dataset.cpp src/native/settings.cpp src/native/preferences.cpp src/native/document.cpp src/native/axis_remap.cpp src/native/z_subset_command.cpp src/native/manual_threshold.cpp src/native/manual_threshold_command.cpp src/native/selection_edit.cpp src/native/selection_boolean.cpp src/native/roi_style.cpp src/native/application.cpp src/native/metadata.cpp src/native/session_metadata.cpp src/native/operations.cpp src/native/targets.cpp src/native/execution.cpp src/native/jobs.cpp src/native/kernel_control.cpp)
add_library(imagejxx::core ALIAS imagejxx_core)
set(IMAGEJXX_PRIVATE_ZLIB_SOURCES
  third_party/zlib/adler32.c third_party/zlib/compress.c
  third_party/zlib/crc32.c third_party/zlib/deflate.c
  third_party/zlib/infback.c third_party/zlib/inffast.c
  third_party/zlib/inflate.c third_party/zlib/inftrees.c
  third_party/zlib/trees.c third_party/zlib/uncompr.c third_party/zlib/zutil.c)
target_sources(imagejxx_core PRIVATE
  src/native/axis_command_record.cpp src/native/axis_orientation_command.cpp src/native/axis_spacing_command.cpp src/native/slicer_resize_command.cpp
  src/native/shortcuts.cpp
  src/native/startup.cpp
  src/native/atomic_file.cpp src/native/session.cpp src/native/session_io.cpp
  src/native/selection_codec.cpp src/native/saved_roi_geometry.cpp
  src/native/saved_roi_geometry_application.cpp
  src/native/float32_display.cpp
  src/native/channel_display.cpp src/native/channel_display_command.cpp
  src/native/dataset_histogram.cpp
  src/native/auto_contrast.cpp src/native/auto_contrast_job.cpp
  src/native/selection_creation.cpp
  src/native/selection_creation_geometry.cpp
  src/native/roi_manager.cpp
  src/native/planar_annotations.cpp src/native/planar_annotation_command.cpp
  src/native/planar_display_annotations.cpp src/native/planar_display_annotation_command.cpp
  src/native/imagej_roi.cpp
  src/native/imagej_shape.cpp
  src/native/imagej_arrow.cpp
  src/native/imagej_spline.cpp
  src/native/imagej_image.cpp
  src/native/imagej_roi_zip.cpp
  src/native/imagej_roi_mapping.cpp
  src/native/imagej_roi_position.cpp src/native/imagej_display_roi.cpp
  src/native/imagej_roi_envelope.cpp
  src/native/imagej_roi_native_envelope.cpp
  src/native/imagej_roi_import.cpp
  src/native/native_workflow_batch.cpp
  src/native/native_workflow_batch_setup.cpp
  src/native/workflow_stage_metadata.cpp src/native/native_workflow.cpp
    src/native/rgb_dataset_command.cpp src/native/indexed_color.cpp src/native/indexed_palette.cpp src/native/indexed_type.cpp src/native/indexed_dataset_command.cpp src/native/indexed_rgb_command.cpp src/native/legacy_rgb_palette.cpp src/native/color_components.cpp src/native/color_stack_command.cpp
  src/native/native_workflow_program.cpp
  src/native/native_workflow_originals.cpp
  src/native/imagej_annotation_import.cpp
  src/native/imagej_annotation_export.cpp
  src/native/imagej_roi_import_command.cpp
  src/native/imagej_roi_pairing.cpp
  src/native/imagej_roi_export.cpp
  src/native/imagej_roi_zip_export.cpp
  ${IMAGEJXX_PRIVATE_ZLIB_SOURCES})
# Private prefixed compression symbols keep headless wheels/SDKs self-contained
# and separate this implementation from the desktop codecs' external zlib.
target_compile_definitions(imagejxx_core PRIVATE Z_PREFIX)
# Z_PREFIX leaves zutil's internal error/debug globals unprefixed. Namespace
# those too so the private codec can coexist with a static desktop zlib.
set_source_files_properties(${IMAGEJXX_PRIVATE_ZLIB_SOURCES} PROPERTIES
  COMPILE_DEFINITIONS "z_errmsg=imagejxx_z_errmsg;z_verbose=imagejxx_z_verbose;z_error=imagejxx_z_error")
target_include_directories(imagejxx_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib)
set_target_properties(imagejxx_core PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME core)
target_compile_features(imagejxx_core PUBLIC cxx_std_17)
find_package(Threads REQUIRED)
target_link_libraries(imagejxx_core PUBLIC Threads::Threads)
target_include_directories(imagejxx_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Reviewed complete header-only inputs are source-distributed. Keep JSON private:
# installed core consumers require neither JSON discovery nor a desktop prefix.
target_include_directories(imagejxx_core SYSTEM PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
if(MSVC)
  target_compile_options(imagejxx_core PRIVATE /W4 $<$<COMPILE_LANGUAGE:CXX>:/permissive-> /utf-8)
  set_source_files_properties(${IMAGEJXX_PRIVATE_ZLIB_SOURCES} PROPERTIES COMPILE_OPTIONS /w)
    set_source_files_properties(src/native/shadow.cpp src/native/gaussian.cpp src/native/rank.cpp src/native/color_components.cpp src/native/indexed_color.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
else()
  target_compile_options(imagejxx_core PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(src/native/color_components.cpp src/native/indexed_color.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_source_files_properties(src/native/shadow.cpp src/native/gaussian.cpp src/native/rank.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
  endif()
endif()

# ImageJ bilinear evaluation preserves separate binary64 operations.
if(MSVC)
  set_source_files_properties(src/native/execution.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
  set_source_files_properties(src/native/channel_display.cpp src/native/float32_display.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
  set_source_files_properties(src/native/dataset_histogram.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
  set_source_files_properties(src/native/auto_contrast.cpp PROPERTIES COMPILE_OPTIONS /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(src/native/execution.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
  set_source_files_properties(src/native/channel_display.cpp src/native/float32_display.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
  set_source_files_properties(src/native/dataset_histogram.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
  set_source_files_properties(src/native/auto_contrast.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
endif()

if(IMAGEJXX_BUILD_PYTHON)
  set(PYBIND11_FINDPYTHON ON)
  find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
  find_package(pybind11 CONFIG REQUIRED)
  pybind11_add_module(_native src/native/auto_threshold_bindings.cpp src/native/bindings.cpp src/native/avi_movie_bindings.cpp src/native/playback_bindings.cpp src/native/dataset_bindings.cpp src/native/settings_bindings.cpp src/native/preferences_bindings.cpp src/native/conversion_bindings.cpp src/native/document_bindings.cpp src/native/manual_threshold_bindings.cpp src/native/z_subset_bindings.cpp src/native/axis_spacing_command_bindings.cpp src/native/slicer_resize_command_bindings.cpp src/native/application_bindings.cpp src/native/operation_bindings.cpp src/native/target_bindings.cpp src/native/gaussian_command_bindings.cpp src/native/rank_command_bindings.cpp)
  target_sources(_native PRIVATE src/native/session_bindings.cpp)
  target_sources(_native PRIVATE src/native/axis_extraction_command_bindings.cpp)
  target_sources(_native PRIVATE src/native/axis_orientation_command_bindings.cpp)
  target_sources(_native PRIVATE src/native/channel_display_bindings.cpp src/native/float32_display_bindings.cpp)
  target_sources(_native PRIVATE src/native/selection_creation_bindings.cpp)
  target_sources(_native PRIVATE src/native/roi_manager_bindings.cpp)
  target_sources(_native PRIVATE src/native/planar_annotations_bindings.cpp)
  target_sources(_native PRIVATE src/native/imagej_display_roi_bindings.cpp)
  target_sources(_native PRIVATE src/native/planar_display_annotations_bindings.cpp)
  target_sources(_native PRIVATE src/native/imagej_roi_bindings.cpp)
  target_sources(_native PRIVATE src/native/imagej_roi_import_bindings.cpp)
  target_sources(_native PRIVATE src/native/imagej_annotation_import_bindings.cpp)
  target_sources(_native PRIVATE src/native/imagej_annotation_export_bindings.cpp)
  target_sources(_native PRIVATE src/native/native_workflow_bindings.cpp)
  target_sources(_native PRIVATE src/native/rgb_dataset_bindings.cpp)
    target_sources(_native PRIVATE src/native/color_components_bindings.cpp)
    target_sources(_native PRIVATE src/native/indexed_dataset_bindings.cpp src/native/indexed_color_bindings.cpp src/native/color_stack_bindings.cpp)
  target_sources(_native PRIVATE src/native/imagej_roi_zip_export_bindings.cpp)
  target_sources(_native PRIVATE src/native/planar_annotation_command_bindings.cpp)
  target_sources(_native PRIVATE src/native/saved_roi_geometry_bindings.cpp)
  target_sources(_native PRIVATE src/native/dataset_histogram_bindings.cpp)
  target_sources(_native PRIVATE src/native/auto_contrast_bindings.cpp)
  target_sources(_native PRIVATE src/native/shortcuts_bindings.cpp)
  target_sources(_native PRIVATE src/native/startup_bindings.cpp)
  target_link_libraries(_native PRIVATE imagejxx_core)
  target_include_directories(_native SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  install(TARGETS _native LIBRARY DESTINATION imagejxx RUNTIME DESTINATION imagejxx)
  # Explicit component installs are complete Python packages, independently of
  # scikit-build's wheel.packages copy and optional desktop delivery.
  function(imagejxx_install_python_component component)
    install(FILES $<TARGET_FILE:_native> DESTINATION imagejxx COMPONENT ${component})
    install(DIRECTORY src/imagejxx/ DESTINATION imagejxx COMPONENT ${component} FILES_MATCHING PATTERN "*.py")
    install(FILES src/imagejxx/ui/commands.json DESTINATION imagejxx/ui COMPONENT ${component})
    install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION imagejxx/licenses COMPONENT ${component})
    install(DIRECTORY licenses/ DESTINATION imagejxx/licenses COMPONENT ${component})
  endfunction()
  imagejxx_install_python_component(processing-python)
endif()

install(TARGETS imagejxx_core EXPORT imagejxxTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT development)
install(EXPORT imagejxxTargets NAMESPACE imagejxx:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imagejxx COMPONENT development)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/imagejxxConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/imagejxxConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imagejxx)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/imagejxxConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/imagejxxConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/imagejxxConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imagejxx COMPONENT development)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION imagejxx/licenses)
install(DIRECTORY licenses/ DESTINATION imagejxx/licenses)
# The standalone SDK component redistributes native implementation code too.
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/imagejxx/licenses COMPONENT development)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/imagejxx/licenses COMPONENT development)

include(CTest)
if(IMAGEJXX_BUILD_IO)
  find_package(TIFF 4.7.1 EXACT REQUIRED)
  include(cmake/ReviewedTiffRuntime.cmake)
  imagejxx_checked_tiff_runtime(IMAGEJXX_IO_RUNTIME_FILES)
  add_library(imagejxx_io STATIC src/io/io.cpp src/io/file.cpp src/io/layout.cpp src/io/decode.cpp src/io/encode.cpp src/io/deflate.cpp src/io/packbits.cpp src/io/lzw.cpp src/io/predictor.cpp)
  add_library(imagejxx::io ALIAS imagejxx_io)
  set_target_properties(imagejxx_io PROPERTIES EXPORT_NAME io POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(imagejxx_io PUBLIC imagejxx::core PRIVATE TIFF::TIFF)
  target_include_directories(imagejxx_io PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_features(imagejxx_io PUBLIC cxx_std_17)
  target_compile_definitions(imagejxx_io PRIVATE IMAGEJXX_IO_VERSION="${PROJECT_VERSION}")
  if(MSVC)
    target_compile_definitions(imagejxx_io PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(imagejxx_io PRIVATE /W4 /permissive- /utf-8 /fp:strict)
  else()
    target_compile_options(imagejxx_io PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off -fno-fast-math)
  endif()
  install(TARGETS imagejxx_io EXPORT imagejxxIoTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT io-development)
  install(EXPORT imagejxxIoTargets NAMESPACE imagejxx:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imagejxx COMPONENT io-development)
  install(FILES cmake/ReviewedTiffRuntime.cmake cmake/TiffRuntimeManifest.cmake DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/imagejxx COMPONENT io-development)
  if(WIN32)
    add_custom_target(imagejxx_verify_tiff_runtime COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" VERBATIM)
    add_dependencies(imagejxx_io imagejxx_verify_tiff_runtime)
    install(SCRIPT "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMPONENT io-development)
    # The reviewed ten-file development/runtime closure makes the installed
    # optional SDK independently usable without a graphics/Python prefix.
    get_target_property(io_tiff_library TIFF::TIFF IMPORTED_LOCATION_RELEASE)
    if(NOT io_tiff_library)
      get_target_property(io_tiff_library TIFF::TIFF IMPORTED_IMPLIB_RELEASE)
    endif()
    get_filename_component(io_tiff_libdir "${io_tiff_library}" DIRECTORY)
    get_filename_component(io_tiff_prefix "${io_tiff_libdir}" DIRECTORY)
    foreach(row IN LISTS IMAGEJXX_TIFF_REVIEWED_FILES)
      string(REPLACE "|" ";" fields "${row}")
      list(GET fields 1 relative)
      get_filename_component(destination "${relative}" DIRECTORY)
      install(FILES "${io_tiff_prefix}/${relative}" DESTINATION "${destination}" COMPONENT io-development)
    endforeach()
  endif()
  if(BUILD_TESTING)
    add_executable(imagejxx_tiff_metadata_tests tests/native/test_tiff_metadata.cpp)
    target_link_libraries(imagejxx_tiff_metadata_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_tiff_metadata_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_tiff_metadata COMMAND imagejxx_tiff_metadata_tests "${CMAKE_CURRENT_BINARY_DIR}/tiff-metadata-artifacts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-metadata" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u16" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-refusals" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-single-axis")
    if(WIN32)
      add_custom_command(TARGET imagejxx_tiff_metadata_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_tiff_metadata_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_tiff_acceptance_tests tests/native/test_tiff_acceptance.cpp)
    target_link_libraries(imagejxx_tiff_acceptance_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_tiff_acceptance_tests PRIVATE src/native)
    add_test(NAME native_tiff_acceptance COMMAND imagejxx_tiff_acceptance_tests "${CMAKE_CURRENT_BINARY_DIR}/tiff-acceptance-artifacts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-acceptance")
    if(WIN32)
      add_custom_command(TARGET imagejxx_tiff_acceptance_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_tiff_acceptance_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_staged_tiff_tests tests/native/test_staged_tiff.cpp)
    target_link_libraries(imagejxx_staged_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_staged_tiff_tests PRIVATE src/io src/native)
    add_test(NAME native_staged_tiff COMMAND imagejxx_staged_tiff_tests "${CMAKE_CURRENT_BINARY_DIR}/staged-tiff-fixtures")
    if(WIN32)
      add_custom_command(TARGET imagejxx_staged_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_staged_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    set(IMAGEJXX_SESSION_TEST_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/session-tests"
        CACHE PATH "Fresh output root for scientific session test artifacts")
    add_executable(imagejxx_floating_tiff_tests tests/native/test_floating_tiff.cpp)
    target_link_libraries(imagejxx_floating_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_floating_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_floating_tiff COMMAND imagejxx_floating_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-floating" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/floating-tiff" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/floating-tiff-libtiff471")
    set_tests_properties(native_floating_tiff PROPERTIES TIMEOUT 90)
    if(WIN32)
      add_custom_command(TARGET imagejxx_floating_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_floating_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_horizontal_tiff_tests tests/native/test_horizontal_tiff.cpp)
    target_link_libraries(imagejxx_horizontal_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_horizontal_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_horizontal_tiff COMMAND imagejxx_horizontal_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-horizontal" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/horizontal-tiff")
    set_tests_properties(native_horizontal_tiff PROPERTIES TIMEOUT 90)
    if(WIN32)
      add_custom_command(TARGET imagejxx_horizontal_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_horizontal_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_lzw_tiff_tests tests/native/test_lzw_tiff.cpp)
    target_link_libraries(imagejxx_lzw_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_lzw_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_lzw_tiff COMMAND imagejxx_lzw_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-lzw" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/lzw-tiff")
    set_tests_properties(native_lzw_tiff PROPERTIES TIMEOUT 90)
    if(WIN32)
      add_custom_command(TARGET imagejxx_lzw_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_lzw_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_packbits_tiff_tests tests/native/test_packbits_tiff.cpp)
    target_link_libraries(imagejxx_packbits_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_packbits_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_packbits_tiff COMMAND imagejxx_packbits_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-packbits" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/packbits-tiff")
    set_tests_properties(native_packbits_tiff PROPERTIES TIMEOUT 90)
    if(WIN32)
      add_custom_command(TARGET imagejxx_packbits_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_packbits_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_deflate_tiff_tests tests/native/test_deflate_tiff.cpp)
    target_link_libraries(imagejxx_deflate_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_deflate_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_deflate_tiff COMMAND imagejxx_deflate_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-deflate" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/deflate-tiff")
    set_tests_properties(native_deflate_tiff PROPERTIES TIMEOUT 90)
    if(WIN32)
      add_custom_command(TARGET imagejxx_deflate_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_deflate_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_rgb_tiff_tests tests/native/test_rgb_tiff.cpp)
    target_link_libraries(imagejxx_rgb_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_rgb_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_rgb_tiff COMMAND imagejxx_rgb_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-rgb8" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/rgb-tiff")
    set_tests_properties(native_rgb_tiff PROPERTIES TIMEOUT 60)
    if(WIN32)
      add_custom_command(TARGET imagejxx_rgb_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_rgb_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_uint8_tiff_tests tests/native/test_uint8_tiff.cpp)
    target_link_libraries(imagejxx_uint8_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_uint8_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_uint8_tiff COMMAND imagejxx_uint8_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u8" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/uint8-tiff")
    set_tests_properties(native_uint8_tiff PROPERTIES TIMEOUT 60)
    if(WIN32)
      add_custom_command(TARGET imagejxx_uint8_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_uint8_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_float32_tiff_tests tests/native/test_float32_tiff.cpp)
    target_link_libraries(imagejxx_float32_tiff_tests PRIVATE imagejxx::io)
    target_include_directories(imagejxx_float32_tiff_tests PRIVATE third_party/nlohmann_json/include)
    add_test(NAME native_float32_tiff COMMAND imagejxx_float32_tiff_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-float32" "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/float32-tiff")
    set_tests_properties(native_float32_tiff PROPERTIES TIMEOUT 60)
    if(WIN32)
      add_custom_command(TARGET imagejxx_float32_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_float32_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
    add_executable(imagejxx_scientific_tiff_tests tests/native/test_scientific_tiff.cpp)
    target_link_libraries(imagejxx_scientific_tiff_tests PRIVATE imagejxx::io)
    add_test(NAME native_scientific_tiff COMMAND imagejxx_scientific_tiff_tests "${CMAKE_CURRENT_BINARY_DIR}/scientific-tiff-fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u16")
    if(WIN32)
      add_custom_command(TARGET imagejxx_scientific_tiff_tests POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:imagejxx_scientific_tiff_tests> COMMAND_EXPAND_LISTS)
    endif()
  endif()
  if(IMAGEJXX_BUILD_PYTHON)
    pybind11_add_module(_scientific_io src/io/python.cpp)
    target_link_libraries(_scientific_io PRIVATE imagejxx::io)
    imagejxx_bind_static_codecs(_scientific_io)
    add_dependencies(_scientific_io _native)
    if(MSVC)
      target_compile_options(_scientific_io PRIVATE /W4 /utf-8)
    endif()
    install(TARGETS _scientific_io LIBRARY DESTINATION imagejxx COMPONENT scientific-python RUNTIME DESTINATION imagejxx COMPONENT scientific-python)
    imagejxx_install_python_component(scientific-python)
    if(WIN32)
      add_dependencies(_scientific_io imagejxx_verify_tiff_runtime)
      add_custom_command(TARGET _scientific_io POST_BUILD COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IMAGEJXX_IO_RUNTIME_FILES} $<TARGET_FILE_DIR:_scientific_io> COMMAND_EXPAND_LISTS)
      install(SCRIPT "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMPONENT scientific-python)
      install(FILES ${IMAGEJXX_IO_RUNTIME_FILES} DESTINATION imagejxx COMPONENT scientific-python)
    endif()
  endif()
endif()
if(BUILD_TESTING)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # A preload-like control must interpose; the same archive bound through the
    # production helper must remain local. No external codec SDK is required.
    add_library(imagejxx_codec_symbol_archive STATIC tests/native/codec_symbol_archive.c)
    set_target_properties(imagejxx_codec_symbol_archive PROPERTIES
      OUTPUT_NAME z POSITION_INDEPENDENT_CODE ON
      ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/codec-symbol-fixture")
    add_library(imagejxx_codec_symbol_host MODULE tests/native/codec_symbol_host.c)
    add_library(imagejxx_codec_symbol_control MODULE tests/native/codec_symbol_consumer.c)
    add_library(imagejxx_codec_symbol_bound MODULE tests/native/codec_symbol_consumer.c)
    target_link_libraries(imagejxx_codec_symbol_control PRIVATE imagejxx_codec_symbol_archive)
    target_link_libraries(imagejxx_codec_symbol_bound PRIVATE imagejxx_codec_symbol_archive)
    imagejxx_bind_static_codecs(imagejxx_codec_symbol_bound)
    add_executable(imagejxx_static_codec_symbol_tests tests/native/test_static_codec_symbols.cpp)
    add_dependencies(imagejxx_static_codec_symbol_tests imagejxx_codec_symbol_host
      imagejxx_codec_symbol_control imagejxx_codec_symbol_bound)
    target_link_libraries(imagejxx_static_codec_symbol_tests PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME native_static_codec_symbols COMMAND imagejxx_static_codec_symbol_tests
      $<TARGET_FILE:imagejxx_codec_symbol_host> $<TARGET_FILE:imagejxx_codec_symbol_control>
      $<TARGET_FILE:imagejxx_codec_symbol_bound>)
    set_tests_properties(native_static_codec_symbols PROPERTIES TIMEOUT 30)
  endif()
  if(NOT DEFINED IMAGEJXX_SESSION_TEST_ARTIFACT_DIR)
    set(IMAGEJXX_SESSION_TEST_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/session-tests"
      CACHE PATH "Owned directory for native session test artifacts")
  endif()
  add_executable(imagejxx_atomic_stream_patch_tests tests/native/test_atomic_stream_patch.cpp)
  target_compile_features(imagejxx_atomic_stream_patch_tests PRIVATE cxx_std_17)
  target_include_directories(imagejxx_atomic_stream_patch_tests PRIVATE include src/native)
  add_test(NAME native_atomic_stream_patch COMMAND imagejxx_atomic_stream_patch_tests "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/atomic-stream-patch")
  set_tests_properties(native_atomic_stream_patch PROPERTIES TIMEOUT 30)
  if(UNIX)
    add_executable(imagejxx_atomic_posix_ownership_tests tests/native/test_atomic_posix_ownership.cpp)
    target_link_libraries(imagejxx_atomic_posix_ownership_tests PRIVATE imagejxx::core)
    target_include_directories(imagejxx_atomic_posix_ownership_tests PRIVATE src/native)
    add_test(NAME native_atomic_posix_ownership COMMAND imagejxx_atomic_posix_ownership_tests "${CMAKE_CURRENT_BINARY_DIR}/atomic-posix-ownership-artifacts")
    set_tests_properties(native_atomic_posix_ownership PROPERTIES TIMEOUT 30)
  endif()
  if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      add_executable(imagejxx_atomic_link_race_tests tests/native/test_atomic_link_race.cpp)
      target_link_libraries(imagejxx_atomic_link_race_tests PRIVATE imagejxx::core)
      target_include_directories(imagejxx_atomic_link_race_tests PRIVATE src/native)
      target_link_options(imagejxx_atomic_link_race_tests PRIVATE -Wl,--wrap=linkat)
      add_test(NAME native_atomic_link_race COMMAND imagejxx_atomic_link_race_tests "${CMAKE_CURRENT_BINARY_DIR}/atomic-link-race-artifacts")
      set_tests_properties(native_atomic_link_race PROPERTIES TIMEOUT 30)
    endif()
    add_executable(imagejxx_atomic_flush_close_tests tests/native/test_atomic_flush_close.cpp)
    target_compile_features(imagejxx_atomic_flush_close_tests PRIVATE cxx_std_17)
    target_include_directories(imagejxx_atomic_flush_close_tests PRIVATE include src/native)
    add_test(NAME native_atomic_flush_close COMMAND imagejxx_atomic_flush_close_tests "${CMAKE_CURRENT_BINARY_DIR}/atomic-flush-close-artifacts")
    set_tests_properties(native_atomic_flush_close PROPERTIES TIMEOUT 30)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      target_link_options(imagejxx_atomic_flush_close_tests PRIVATE -Wl,--wrap=open -Wl,--wrap=openat -Wl,--wrap=fsync -Wl,--wrap=close)
    endif()
  endif()
  add_executable(imagejxx_atomic_file_fault_tests tests/native/test_atomic_file_faults.cpp)
  target_link_libraries(imagejxx_atomic_file_fault_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_atomic_file_fault_tests PRIVATE src/native)
  add_test(NAME native_atomic_file_faults COMMAND imagejxx_atomic_file_fault_tests "${CMAKE_CURRENT_BINARY_DIR}/atomic-file-fault-artifacts")
  set_tests_properties(native_atomic_file_faults PROPERTIES TIMEOUT 30)
  if(UNIX)
    add_test(NAME native_atomic_file_rlimit COMMAND imagejxx_atomic_file_fault_tests "${CMAKE_CURRENT_BINARY_DIR}/atomic-file-limit-artifacts" fsize)
    set_tests_properties(native_atomic_file_rlimit PROPERTIES TIMEOUT 30)
  endif()
  add_executable(imagejxx_pnm_tests tests/native/test_pnm_io.cpp)
  target_link_libraries(imagejxx_pnm_tests PRIVATE imagejxx::core)
  add_test(NAME native_pnm COMMAND imagejxx_pnm_tests "${CMAKE_CURRENT_BINARY_DIR}/pnm-artifacts")
  set_tests_properties(native_pnm PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_startup_policy_tests tests/native/test_startup_policy.cpp)
  target_link_libraries(imagejxx_startup_policy_tests PRIVATE imagejxx::core)
  add_test(NAME native_startup_policy COMMAND imagejxx_startup_policy_tests)
  set_tests_properties(native_startup_policy PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_startup_adoption_tests tests/native/test_startup_adoption.cpp)
  target_link_libraries(imagejxx_startup_adoption_tests PRIVATE imagejxx::core)
  add_test(NAME native_startup_adoption COMMAND imagejxx_startup_adoption_tests)
  set_tests_properties(native_startup_adoption PROPERTIES TIMEOUT 60)
  add_executable(test_dataset_read_observer tests/native/test_dataset_read_observer.cpp)
  target_link_libraries(test_dataset_read_observer PRIVATE imagejxx::core)
  target_include_directories(test_dataset_read_observer PRIVATE src/native)
  add_test(NAME native_dataset_read_observer COMMAND test_dataset_read_observer)
  set_tests_properties(native_dataset_read_observer PROPERTIES TIMEOUT 30)
  add_executable(test_shortcuts tests/native/test_shortcuts.cpp)
  target_link_libraries(test_shortcuts PRIVATE imagejxx::core)
  add_test(NAME native_shortcuts COMMAND test_shortcuts)
  set_tests_properties(native_shortcuts PROPERTIES TIMEOUT 30)
  add_executable(test_shortcut_settings tests/native/test_shortcut_settings.cpp)
  target_link_libraries(test_shortcut_settings PRIVATE imagejxx::core)
  add_test(NAME native_shortcut_settings COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:test_shortcut_settings>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/shortcut-settings
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOwnedFixtureTest.cmake)
  set_tests_properties(native_shortcut_settings PROPERTIES TIMEOUT 30)
  add_executable(test_history_policy_batch tests/native/test_history_policy_batch.cpp)
  target_link_libraries(test_history_policy_batch PRIVATE imagejxx::core)
  add_test(NAME native_history_policy_batch COMMAND test_history_policy_batch)
  set_tests_properties(native_history_policy_batch PROPERTIES TIMEOUT 30)
  # Compile the same implementation separately for the private staging-order
  # observations; shipping core builds expose no test counter or callback.
  add_executable(test_shortcuts_materialization
    tests/native/test_shortcuts.cpp src/native/shortcuts.cpp)
  target_include_directories(test_shortcuts_materialization PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_features(test_shortcuts_materialization PRIVATE cxx_std_17)
  target_compile_definitions(test_shortcuts_materialization PRIVATE
    IMAGEJXX_SHORTCUTS_TEST_MATERIALIZATION=1)
  add_test(NAME native_shortcuts_materialization COMMAND test_shortcuts_materialization)
  set_tests_properties(native_shortcuts_materialization PROPERTIES TIMEOUT 30)
  if(IMAGEJXX_BUILD_PYTHON)
    # Actual native counting providers for Python no-read assertions. This is a
    # private fixture, never installed or exported as a product API.
    pybind11_add_module(_imagejxx_test_saved_roi_source
      tests/native/saved_roi_python_source.cpp)
    target_link_libraries(_imagejxx_test_saved_roi_source PRIVATE imagejxx::core)
  endif()
  add_executable(imagejxx_session_tests tests/native/test_session.cpp)
  target_link_libraries(imagejxx_session_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_session_tests PRIVATE
    third_party/zlib third_party/nlohmann_json/include)
  target_compile_definitions(imagejxx_session_tests PRIVATE Z_PREFIX)
  set(IMAGEJXX_SESSION_TEST_ARTIFACT_DIR "${CMAKE_CURRENT_BINARY_DIR}/session-tests"
      CACHE PATH "Fresh output root for scientific session test artifacts")
  foreach(group IN ITEMS literal axes state malformed v2 admission batch io)
    add_test(NAME native_session_${group} COMMAND imagejxx_session_tests
      ${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/${group} ${group})
    set_tests_properties(native_session_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(imagejxx_gaussian_imagej_tests tests/native/test_gaussian_imagej.cpp)
  target_link_libraries(imagejxx_gaussian_imagej_tests PRIVATE imagejxx::core)
  add_test(NAME native_gaussian_imagej COMMAND imagejxx_gaussian_imagej_tests)
  add_executable(imagejxx_rank_display_tests tests/native/test_rank_display.cpp)
  add_executable(imagejxx_outliers_command_tests tests/native/test_outliers_command.cpp)
  target_link_libraries(imagejxx_outliers_command_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_outliers_command_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_outliers_command COMMAND imagejxx_outliers_command_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/outliers-imagej154r.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/outliers-lut-imagej154r.json")
  set_tests_properties(native_outliers_command PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_tophat_preview_tests tests/native/test_tophat_preview.cpp)
  target_link_libraries(imagejxx_tophat_preview_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_tophat_preview_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_tophat_preview COMMAND imagejxx_tophat_preview_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-preview-imagej154r.json
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-mask-imagej154r.json)
  set_tests_properties(native_tophat_preview PROPERTIES TIMEOUT 60)

  add_executable(imagejxx_tophat_command_tests tests/native/test_tophat_command.cpp)
  target_link_libraries(imagejxx_tophat_command_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_tophat_command_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_tophat_command COMMAND imagejxx_tophat_command_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-imagej154r.json)
  set_tests_properties(native_tophat_command PROPERTIES TIMEOUT 60)

  add_executable(imagejxx_tophat_display_tests tests/native/test_tophat_display.cpp)
  target_link_libraries(imagejxx_tophat_display_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_tophat_display_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_tophat_display COMMAND imagejxx_tophat_display_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-imagej154r.json
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-channel-imagej154r.json
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-preview-imagej154r.json)
  set_tests_properties(native_tophat_display PROPERTIES TIMEOUT 60)

  add_executable(imagejxx_remove_nans_command_tests tests/native/test_remove_nans_command.cpp)
  target_link_libraries(imagejxx_remove_nans_command_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_remove_nans_command_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_remove_nans_command COMMAND imagejxx_remove_nans_command_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/remove-nans-imagej154r.json)
  set_tests_properties(native_remove_nans_command PROPERTIES TIMEOUT 60)

  add_executable(imagejxx_remove_nans_tests tests/native/test_remove_nans.cpp)
  target_link_libraries(imagejxx_remove_nans_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_remove_nans_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_remove_nans COMMAND imagejxx_remove_nans_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/remove-nans-imagej154r.json)
  set_tests_properties(native_remove_nans PROPERTIES TIMEOUT 60)

  add_executable(imagejxx_tophat_imagej_tests tests/native/test_tophat_imagej.cpp)
  target_link_libraries(imagejxx_tophat_imagej_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_tophat_imagej_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_tophat_imagej COMMAND imagejxx_tophat_imagej_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tophat-imagej154r.json)
  set_tests_properties(native_tophat_imagej PROPERTIES TIMEOUT 60)

  add_executable(imagejxx_outliers_imagej_tests tests/native/test_outliers_imagej.cpp)
  target_link_libraries(imagejxx_outliers_imagej_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_outliers_imagej_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_outliers_imagej COMMAND imagejxx_outliers_imagej_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/outliers-imagej154r.json")
  set_tests_properties(native_outliers_imagej PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_despeckle_tests tests/native/test_despeckle.cpp)
  target_link_libraries(imagejxx_despeckle_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_despeckle_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_despeckle COMMAND imagejxx_despeckle_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/despeckle-imagej154r.json")
  set_tests_properties(native_despeckle PROPERTIES TIMEOUT 60)
  target_link_libraries(imagejxx_rank_display_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_rank_display_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_rank_display COMMAND imagejxx_rank_display_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/variance-display-imagej154r.json")
  set_tests_properties(native_rank_display PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_rank_imagej_tests tests/native/test_rank_imagej.cpp)
  target_link_libraries(imagejxx_rank_imagej_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_rank_imagej_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  target_compile_definitions(imagejxx_rank_imagej_tests PRIVATE
    IMAGEJXX_RANK_IMAGEJ_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rank_imagej_154r_001.json")
  add_test(NAME native_rank_imagej COMMAND imagejxx_rank_imagej_tests)
  set_tests_properties(native_rank_imagej PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_rank_command_tests tests/native/test_rank_command.cpp)
  target_link_libraries(imagejxx_rank_command_tests PRIVATE imagejxx::core)
  add_test(NAME native_rank_command COMMAND imagejxx_rank_command_tests)
  set_tests_properties(native_rank_command PROPERTIES TIMEOUT 60)
  add_executable(test_saved_roi_geometry tests/native/test_saved_roi_geometry.cpp)
  target_link_libraries(test_saved_roi_geometry PRIVATE imagejxx::core)
  add_test(NAME native_saved_roi_geometry COMMAND test_saved_roi_geometry)
  add_executable(test_rounded_saved_resources tests/native/test_rounded_saved_resources.cpp)
  target_link_libraries(test_rounded_saved_resources PRIVATE imagejxx::core)
  add_test(NAME native_rounded_saved_resources COMMAND test_rounded_saved_resources)
  add_executable(test_ieee_saved_geometry tests/native/test_ieee_saved_geometry.cpp)
  target_link_libraries(test_ieee_saved_geometry PRIVATE imagejxx::core)
  add_test(NAME native_ieee_saved_geometry COMMAND test_ieee_saved_geometry)
  set_tests_properties(native_saved_roi_geometry PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_gaussian_command_tests tests/native/test_gaussian_command.cpp)
  target_link_libraries(imagejxx_gaussian_command_tests PRIVATE imagejxx::core)
  add_test(NAME native_gaussian_command COMMAND imagejxx_gaussian_command_tests)

  add_executable(imagejxx_axis_extraction_command_tests tests/native/test_axis_extraction_command.cpp)
  target_link_libraries(imagejxx_axis_extraction_command_tests PRIVATE imagejxx::core)
  foreach(group metadata records replay execution)
    add_test(NAME native_axis_extraction_command_${group}
      COMMAND imagejxx_axis_extraction_command_tests ${group})
  endforeach()
  add_executable(imagejxx_scientific_axis_orientation_command_tests
    tests/native/test_scientific_axis_orientation_command.cpp)
  target_link_libraries(imagejxx_scientific_axis_orientation_command_tests PRIVATE imagejxx::core)
  foreach(group
      N01.metadata_descriptor_zero_reads_288
      N02.oriented_command_prepare_owned72_singletons
      N03.v1_preserved_v2_closed_canonical_records
      N04.grammar_flags_and_derived_tamper_no_reads
      N05.strict_replay_complete_source_dependencies
      N06.explicit_full_rebind_original_first
      N07.cap_before_callback_request_immutability
      N08.cancellation_callbacks_final_validation
      N09.new_output_source_annotations_and_samples
      N10.publication_stale_closed_foreign_modified_repeated
      N11.display_selection_navigation_independence
      N12.unchanged_public_two_type_visitors_and_v1_grammar)
    add_test(NAME native_scientific_axis_orientation_${group}
      COMMAND imagejxx_scientific_axis_orientation_command_tests ${group})
    set_tests_properties(native_scientific_axis_orientation_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(imagejxx_channel_display_tests tests/native/test_channel_display.cpp)
  target_link_libraries(imagejxx_channel_display_tests PRIVATE imagejxx::core)
  foreach(group state mapping ranges limits)
    add_test(NAME native_channel_display_${group}
      COMMAND imagejxx_channel_display_tests ${group})
    set_tests_properties(native_channel_display_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(imagejxx_channel_display_command_tests tests/native/test_channel_display_command.cpp)
  target_link_libraries(imagejxx_channel_display_command_tests PRIVATE imagejxx::core)
  foreach(group records replay mutations reset)
    add_test(NAME native_channel_display_command_${group}
      COMMAND imagejxx_channel_display_command_tests ${group})
    set_tests_properties(native_channel_display_command_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(imagejxx_channel_display_projection_tests tests/native/test_channel_display_projection.cpp)
  target_link_libraries(imagejxx_channel_display_projection_tests PRIVATE imagejxx::core)
  foreach(group state mapping transition)
    add_test(NAME native_channel_display_projection_${group}
      COMMAND imagejxx_channel_display_projection_tests ${group})
    set_tests_properties(native_channel_display_projection_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(imagejxx_channel_display_command_v2_tests tests/native/test_channel_display_command_v2.cpp)
  target_link_libraries(imagejxx_channel_display_command_v2_tests PRIVATE imagejxx::core)
  add_executable(imagejxx_channel_display_select_min_tests tests/native/test_channel_display_select_min.cpp)
  target_link_libraries(imagejxx_channel_display_select_min_tests PRIVATE imagejxx::core)
  foreach(group IN ITEMS transition records replay rebind limits)
    add_test(NAME native_channel_display_select_min_${group}
      COMMAND imagejxx_channel_display_select_min_tests ${group})
    set_tests_properties(native_channel_display_select_min_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  foreach(group records resolve replay rebind limits)
    add_test(NAME native_channel_display_command_v2_${group}
      COMMAND imagejxx_channel_display_command_v2_tests ${group})
    set_tests_properties(native_channel_display_command_v2_${group} PROPERTIES TIMEOUT 60)
  endforeach()
  add_executable(imagejxx_gaussian_command_execution_tests tests/native/test_gaussian_command_execution.cpp)
  target_link_libraries(imagejxx_gaussian_command_execution_tests PRIVATE imagejxx::core)
  add_test(NAME native_gaussian_command_execution COMMAND imagejxx_gaussian_command_execution_tests)
  set_tests_properties(native_gaussian_command_execution PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_gaussian_command_rebind_tests tests/native/test_gaussian_command_rebind.cpp)
  target_link_libraries(imagejxx_gaussian_command_rebind_tests PRIVATE imagejxx::core)
  add_test(NAME native_gaussian_command_rebind COMMAND imagejxx_gaussian_command_rebind_tests)
  set_tests_properties(native_gaussian_command_rebind PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_core_tests tests/native/test_core.cpp)
  target_link_libraries(imagejxx_core_tests PRIVATE imagejxx::core)
  add_test(NAME native_core COMMAND imagejxx_core_tests)
  add_executable(imagejxx_dataset_tests tests/native/test_dataset.cpp)
  target_link_libraries(imagejxx_dataset_tests PRIVATE imagejxx::core)
  add_test(NAME native_dataset COMMAND imagejxx_dataset_tests)
  add_executable(imagejxx_remove_nans_ieee_tests tests/native/test_remove_nans_ieee.cpp)
  target_link_libraries(imagejxx_remove_nans_ieee_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_remove_nans_ieee_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  target_include_directories(imagejxx_remove_nans_ieee_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_remove_nans_ieee COMMAND imagejxx_remove_nans_ieee_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/remove-nans-ieee-imagej154r.json)
  set_tests_properties(native_remove_nans_ieee PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_ieee_computed_display_tests tests/native/test_ieee_computed_display.cpp)
  target_link_libraries(imagejxx_ieee_computed_display_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_ieee_computed_display_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_ieee_computed_display COMMAND imagejxx_ieee_computed_display_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ieee-display-imagej154r.json)
  set_tests_properties(native_ieee_computed_display PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_ieee_channel_tests tests/native/test_ieee_channel.cpp)
  target_link_libraries(imagejxx_ieee_channel_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_ieee_channel_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_ieee_channel COMMAND imagejxx_ieee_channel_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ieee-display-imagej154r.json)
  set_tests_properties(native_ieee_channel PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_ieee_display_tests tests/native/test_ieee_display.cpp)
  target_link_libraries(imagejxx_ieee_display_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_ieee_display_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_ieee_display COMMAND imagejxx_ieee_display_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ieee-display-imagej154r.json)
  set_tests_properties(native_ieee_display PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_ieee_source_tests tests/native/test_ieee_float32_source.cpp)
  target_link_libraries(imagejxx_ieee_source_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_ieee_source_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_ieee_source COMMAND imagejxx_ieee_source_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ieee-float32-source001.json
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ieee-float32-source001.ijx)
  set_tests_properties(native_ieee_source PROPERTIES TIMEOUT 30)

  add_executable(imagejxx_nan_source_tests tests/native/nan_source.cpp)
  target_link_libraries(imagejxx_nan_source_tests PRIVATE imagejxx::core)
  add_test(NAME native_nan_source COMMAND imagejxx_nan_source_tests)
  set_tests_properties(native_nan_source PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_document_tests tests/native/test_document.cpp)
  target_link_libraries(imagejxx_document_tests PRIVATE imagejxx::core)
  add_test(NAME native_document COMMAND imagejxx_document_tests)
  add_executable(imagejxx_session_metadata_tests tests/native/test_session_metadata.cpp)
  target_link_libraries(imagejxx_session_metadata_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_session_metadata_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  target_include_directories(imagejxx_session_metadata_tests SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_session_metadata COMMAND imagejxx_session_metadata_tests)
  set_tests_properties(native_session_metadata PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_selection_edit_tests tests/native/test_selection_edit.cpp)
  add_executable(imagejxx_nan_threshold_display_tests tests/native/test_nan_threshold_display.cpp)
  target_link_libraries(imagejxx_nan_threshold_display_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_nan_threshold_display_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  add_test(NAME native_nan_threshold_display COMMAND imagejxx_nan_threshold_display_tests)
  set_tests_properties(native_nan_threshold_display PROPERTIES TIMEOUT 30)

  add_executable(imagejxx_manual_threshold_tests tests/native/test_manual_threshold.cpp)
  target_link_libraries(imagejxx_manual_threshold_tests PRIVATE imagejxx::core)
  add_test(NAME native_manual_threshold COMMAND imagejxx_manual_threshold_tests)
  set_tests_properties(native_manual_threshold PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_manual_threshold_command_tests tests/native/test_manual_threshold_command.cpp)
  target_link_libraries(imagejxx_manual_threshold_command_tests PRIVATE imagejxx::core)
  add_test(NAME native_manual_threshold_command COMMAND imagejxx_manual_threshold_command_tests)
  set_tests_properties(native_manual_threshold_command PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_z_subset_tests tests/native/test_z_subset.cpp)
  target_link_libraries(imagejxx_z_subset_tests PRIVATE imagejxx::core)
  add_test(NAME native_z_subset COMMAND imagejxx_z_subset_tests)
  set_tests_properties(native_z_subset PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_dataset_histogram_tests tests/native/test_dataset_histogram.cpp)
  target_link_libraries(imagejxx_dataset_histogram_tests PRIVATE imagejxx::core)
  add_test(NAME native_dataset_histogram COMMAND imagejxx_dataset_histogram_tests)
  set_tests_properties(native_dataset_histogram PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_auto_contrast_tests tests/native/test_auto_contrast.cpp)
  target_link_libraries(imagejxx_auto_contrast_tests PRIVATE imagejxx::core)
  add_test(NAME native_auto_contrast COMMAND imagejxx_auto_contrast_tests)
  set_tests_properties(native_auto_contrast PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_auto_contrast_jobs_tests tests/native/test_auto_contrast_jobs.cpp)
  target_link_libraries(imagejxx_auto_contrast_jobs_tests PRIVATE imagejxx::core)
  add_test(NAME native_auto_contrast_jobs COMMAND imagejxx_auto_contrast_jobs_tests)
  set_tests_properties(native_auto_contrast_jobs PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_auto_contrast_display_reset_tests tests/native/test_auto_contrast_display_reset.cpp)
  target_link_libraries(imagejxx_auto_contrast_display_reset_tests PRIVATE imagejxx::core)
  add_test(NAME native_auto_contrast_display_reset COMMAND imagejxx_auto_contrast_display_reset_tests)
  set_tests_properties(native_auto_contrast_display_reset PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_auto_contrast_sdk_probe examples/cpp/auto_contrast.cpp)
  target_link_libraries(imagejxx_auto_contrast_sdk_probe PRIVATE imagejxx::core)
  add_test(NAME native_auto_contrast_sdk_probe COMMAND imagejxx_auto_contrast_sdk_probe)
  set_tests_properties(native_auto_contrast_sdk_probe PROPERTIES TIMEOUT 60)
  target_link_libraries(imagejxx_selection_edit_tests PRIVATE imagejxx::core)
  add_test(NAME native_selection_edit COMMAND imagejxx_selection_edit_tests)
  add_executable(imagejxx_selection_boolean_tests tests/native/test_selection_boolean.cpp)
  target_link_libraries(imagejxx_selection_boolean_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_selection_boolean_tests SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  target_compile_definitions(imagejxx_selection_boolean_tests PRIVATE
    IMAGEJXX_ROI_BOOLEAN_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/roi-boolean-imagej154r.json")
  add_test(NAME native_selection_boolean COMMAND imagejxx_selection_boolean_tests)
  set_tests_properties(native_selection_boolean PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_selection_creation_tests tests/native/test_selection_creation.cpp)
  target_link_libraries(imagejxx_selection_creation_tests PRIVATE imagejxx::core)
  add_test(NAME native_selection_creation COMMAND imagejxx_selection_creation_tests)
  set_tests_properties(native_selection_creation PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_selection_creation_geometry_tests
    tests/native/test_selection_creation_geometry.cpp)
  target_link_libraries(imagejxx_selection_creation_geometry_tests PRIVATE imagejxx::core)
  add_test(NAME native_selection_creation_geometry
    COMMAND imagejxx_selection_creation_geometry_tests)
  set_tests_properties(native_selection_creation_geometry PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_rounded_path_tests tests/native/test_rounded_path.cpp)
  target_link_libraries(imagejxx_rounded_path_tests PRIVATE imagejxx::core)
  add_test(NAME native_rounded_path COMMAND imagejxx_rounded_path_tests)
  set_tests_properties(native_rounded_path PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_roi_style_tests tests/native/test_roi_style.cpp)
  add_executable(imagejxx_roi_manager_tests tests/native/test_roi_manager.cpp)
  target_link_libraries(imagejxx_roi_manager_tests PRIVATE imagejxx::core)
  add_test(NAME native_roi_manager COMMAND imagejxx_roi_manager_tests)
  set_tests_properties(native_roi_manager PROPERTIES TIMEOUT 30)
  target_link_libraries(imagejxx_roi_style_tests PRIVATE imagejxx::core)
  add_test(NAME native_roi_style COMMAND imagejxx_roi_style_tests)
  add_executable(imagejxx_application_tests tests/native/test_application.cpp)
  target_link_libraries(imagejxx_application_tests PRIVATE imagejxx::core)
  add_test(NAME native_application COMMAND imagejxx_application_tests)
  add_executable(imagejxx_shadow_tests examples/cpp/shadows.cpp)
  target_link_libraries(imagejxx_shadow_tests PRIVATE imagejxx::core)
  add_test(NAME native_shadow_imagej COMMAND imagejxx_shadow_tests)
  add_executable(imagejxx_operations_tests tests/native/test_operations.cpp)
  target_link_libraries(imagejxx_operations_tests PRIVATE imagejxx::core)
  add_test(NAME native_operations COMMAND imagejxx_operations_tests)
  add_executable(imagejxx_target_tests tests/native/test_targets.cpp)
  target_link_libraries(imagejxx_target_tests PRIVATE imagejxx::core)
  add_test(NAME native_targets COMMAND imagejxx_target_tests)
  add_executable(imagejxx_execution_tests tests/native/test_execution.cpp)
  target_link_libraries(imagejxx_execution_tests PRIVATE imagejxx::core)
  add_test(NAME native_execution COMMAND imagejxx_execution_tests)
  add_executable(imagejxx_axis_plane_export_tests tests/native/test_axis_plane_export.cpp)
  target_link_libraries(imagejxx_axis_plane_export_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_plane_export COMMAND imagejxx_axis_plane_export_tests)
  set_tests_properties(native_axis_plane_export PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_axis_range_reslice_tests tests/native/test_axis_range_reslice.cpp)
  target_link_libraries(imagejxx_axis_range_reslice_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_range_reslice COMMAND imagejxx_axis_range_reslice_tests)
  set_tests_properties(native_axis_range_reslice PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_axis_range_validation_tests tests/native/test_axis_range_validation.cpp)
  target_link_libraries(imagejxx_axis_range_validation_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_range_validation COMMAND imagejxx_axis_range_validation_tests)
  set_tests_properties(native_axis_range_validation PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_axis_range_orientation_tests tests/native/test_axis_range_orientation.cpp)
  target_link_libraries(imagejxx_axis_range_orientation_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_range_orientation COMMAND imagejxx_axis_range_orientation_tests)
  set_tests_properties(native_axis_range_orientation PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_axis_range_orientation_validation_tests tests/native/test_axis_range_orientation_validation.cpp)
  target_link_libraries(imagejxx_axis_range_orientation_validation_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_range_orientation_validation COMMAND imagejxx_axis_range_orientation_validation_tests)

  add_executable(imagejxx_axis_range_spacing_tests tests/native/test_axis_range_spacing.cpp)
  add_executable(imagejxx_axis_spacing_command_tests tests/native/test_axis_spacing_command.cpp)
  add_executable(imagejxx_planar_annotations_tests tests/native/test_planar_annotations.cpp)
  add_executable(imagejxx_planar_display_annotations_tests tests/native/test_planar_display_annotations.cpp)
  add_executable(imagejxx_imagej_display_roi_tests tests/native/test_imagej_display_roi.cpp)
  add_executable(imagejxx_native_workflow_batch_tests tests/native/test_native_workflow_batch.cpp)
  target_link_libraries(imagejxx_native_workflow_batch_tests PRIVATE imagejxx::core)
  add_test(NAME native_workflow_batch COMMAND imagejxx_native_workflow_batch_tests "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/batch")
  add_executable(imagejxx_native_workflow_tests tests/native/test_native_workflow.cpp)
  target_link_libraries(imagejxx_native_workflow_tests PRIVATE imagejxx::core)
  add_test(NAME native_workflow COMMAND imagejxx_native_workflow_tests)
  add_executable(imagejxx_imagej_annotation_import_tests tests/native/test_imagej_annotation_import.cpp)
  add_executable(imagejxx_imagej_annotation_export_tests tests/native/test_imagej_annotation_export.cpp)
  target_link_libraries(imagejxx_imagej_annotation_export_tests PRIVATE imagejxx::core)
  add_test(NAME native_imagej_annotation_export COMMAND imagejxx_imagej_annotation_export_tests)
  target_link_libraries(imagejxx_imagej_annotation_import_tests PRIVATE imagejxx::core)
  add_test(NAME native_imagej_annotation_import COMMAND imagejxx_imagej_annotation_import_tests)
  target_link_libraries(imagejxx_imagej_display_roi_tests PRIVATE imagejxx::core)
  add_test(NAME native_imagej_display_roi COMMAND imagejxx_imagej_display_roi_tests)
  target_link_libraries(imagejxx_planar_display_annotations_tests PRIVATE imagejxx::core)
  add_test(NAME native_planar_display_annotations COMMAND imagejxx_planar_display_annotations_tests)
  add_executable(imagejxx_imagej_roi_tests tests/native/test_imagej_roi.cpp)
  target_link_libraries(imagejxx_imagej_roi_tests PRIVATE imagejxx::core)
  add_test(NAME native_imagej_roi COMMAND imagejxx_imagej_roi_tests)
  add_executable(imagejxx_planar_annotation_command_tests tests/native/test_planar_annotation_command.cpp)
  target_link_libraries(imagejxx_planar_annotation_command_tests PRIVATE imagejxx::core)
  add_test(NAME native_planar_annotation_command COMMAND imagejxx_planar_annotation_command_tests)
  target_link_libraries(imagejxx_planar_annotations_tests PRIVATE imagejxx::core)
  add_test(NAME native_planar_annotations COMMAND imagejxx_planar_annotations_tests)
  target_link_libraries(imagejxx_axis_spacing_command_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_spacing_command COMMAND imagejxx_axis_spacing_command_tests)
  add_executable(imagejxx_slicer_resize_command_tests tests/native/test_slicer_resize_command.cpp)
  target_link_libraries(imagejxx_slicer_resize_command_tests PRIVATE imagejxx::core)
  add_test(NAME native_slicer_resize_command COMMAND imagejxx_slicer_resize_command_tests)
  target_link_libraries(imagejxx_axis_range_spacing_tests PRIVATE imagejxx::core)
  add_test(NAME native_axis_range_spacing COMMAND imagejxx_axis_range_spacing_tests mappings)
  add_test(NAME native_axis_range_spacing_validation COMMAND imagejxx_axis_range_spacing_tests validation)
  set_tests_properties(native_axis_range_orientation_validation PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_axis_range_slicer_resize_tests tests/native/test_axis_range_slicer_resize.cpp)
  target_link_libraries(imagejxx_axis_range_slicer_resize_tests PRIVATE imagejxx::core)
  if(MSVC)
    target_compile_options(imagejxx_axis_range_slicer_resize_tests PRIVATE /fp:strict)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(imagejxx_axis_range_slicer_resize_tests PRIVATE -ffp-contract=off -fno-fast-math)
  endif()
  add_test(NAME native_axis_range_slicer_resize COMMAND imagejxx_axis_range_slicer_resize_tests mappings)
  add_test(NAME native_axis_range_slicer_resize_validation COMMAND imagejxx_axis_range_slicer_resize_tests validation)
  set_tests_properties(native_axis_range_slicer_resize native_axis_range_slicer_resize_validation PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_job_tests tests/native/test_jobs.cpp)
  target_link_libraries(imagejxx_job_tests PRIVATE imagejxx::core)
  add_test(NAME native_jobs COMMAND imagejxx_job_tests)
  set_tests_properties(native_jobs PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_retained_jobs_tests tests/native/test_retained_jobs.cpp)
  target_link_libraries(imagejxx_retained_jobs_tests PRIVATE imagejxx::core)
  add_test(NAME native_retained_jobs COMMAND imagejxx_retained_jobs_tests)
  set_tests_properties(native_retained_jobs PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_operation_job_probe_tests tests/native/test_operation_job_probe.cpp)
  target_link_libraries(imagejxx_operation_job_probe_tests PRIVATE imagejxx::core)
  add_test(NAME native_operation_job_probe COMMAND imagejxx_operation_job_probe_tests)
  set_tests_properties(native_operation_job_probe PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_gaussian_preview_jobs_tests
    tests/native/test_gaussian_preview_jobs.cpp src/desktop/gaussian_jobs.cpp)
  target_link_libraries(imagejxx_gaussian_preview_jobs_tests PRIVATE imagejxx::core)
  add_test(NAME native_gaussian_preview_jobs COMMAND imagejxx_gaussian_preview_jobs_tests)
  set_tests_properties(native_gaussian_preview_jobs PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_rank_preview_jobs_tests
    tests/native/test_rank_preview_jobs.cpp src/desktop/gaussian_jobs.cpp)
  target_link_libraries(imagejxx_rank_preview_jobs_tests PRIVATE imagejxx::core)
  add_test(NAME native_rank_preview_jobs COMMAND imagejxx_rank_preview_jobs_tests)
  set_tests_properties(native_rank_preview_jobs PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_rank_staged_output_tests tests/native/test_rank_staged_output.cpp)
  target_link_libraries(imagejxx_rank_staged_output_tests PRIVATE imagejxx::core)
  add_test(NAME native_rank_staged_output COMMAND imagejxx_rank_staged_output_tests)
  set_tests_properties(native_rank_staged_output PROPERTIES TIMEOUT 60)
  add_executable(imagejxx_kernel_control_tests tests/native/test_kernel_control.cpp)
  target_link_libraries(imagejxx_kernel_control_tests PRIVATE imagejxx::core)
  add_test(NAME native_kernel_control COMMAND imagejxx_kernel_control_tests)
  set_tests_properties(native_kernel_control PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_kernel_control_reentry_tests
    tests/native/test_kernel_control_reentry.cpp)
  target_link_libraries(imagejxx_kernel_control_reentry_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_kernel_control_reentry_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
  add_test(NAME native_kernel_control_reentry COMMAND imagejxx_kernel_control_reentry_tests)
  set_tests_properties(native_kernel_control_reentry PROPERTIES TIMEOUT 30)
  add_executable(imagejxx_settings_tests tests/native/test_settings.cpp)
  add_executable(imagejxx_avi_movie_tests tests/native/test_avi_movie.cpp)
  target_link_libraries(imagejxx_avi_movie_tests PRIVATE imagejxx::core)
  add_test(NAME native_avi_movie COMMAND imagejxx_avi_movie_tests "${CMAKE_CURRENT_BINARY_DIR}/avi-movie-native-work")
  add_executable(imagejxx_playback_tests tests/native/test_playback.cpp)
  target_link_libraries(imagejxx_playback_tests PRIVATE imagejxx::core)
  add_test(NAME native_playback COMMAND imagejxx_playback_tests)
  target_link_libraries(imagejxx_settings_tests PRIVATE imagejxx::core)
  add_test(NAME native_settings COMMAND imagejxx_settings_tests)
  add_executable(imagejxx_preferences_tests tests/native/test_preferences.cpp)
  target_link_libraries(imagejxx_preferences_tests PRIVATE imagejxx::core)
  add_test(NAME native_preferences COMMAND ${CMAKE_COMMAND}
    -DTEST_EXECUTABLE=$<TARGET_FILE:imagejxx_preferences_tests>
    -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/preferences
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOwnedFixtureTest.cmake)
  add_executable(imagejxx_binary_tests tests/native/test_binary.cpp)
  target_link_libraries(imagejxx_binary_tests PRIVATE imagejxx::core)
  add_test(NAME native_binary COMMAND imagejxx_binary_tests)
  add_executable(imagejxx_conversion_tests tests/native/test_conversion.cpp)
  target_link_libraries(imagejxx_conversion_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_conversion_tests SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  target_compile_definitions(imagejxx_conversion_tests PRIVATE
    "IMAGEJXX_RGB_REFERENCE_FIXTURE=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rgb-conversion-imagej154r.json\"")
  add_test(NAME native_conversion COMMAND imagejxx_conversion_tests)
  add_executable(imagejxx_indexed_color_tests tests/native/test_indexed_color.cpp)
  target_link_libraries(imagejxx_indexed_color_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_indexed_color_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_indexed_color COMMAND imagejxx_indexed_color_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/indexed-color-imagej154r.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/indexed-type-imagej154r.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/indexed-filter-imagej154r.json ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/byte-variance-imagej154r.json)
  add_executable(imagejxx_color_components_tests tests/native/test_color_components.cpp)
  target_link_libraries(imagejxx_color_components_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_color_components_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_color_components COMMAND imagejxx_color_components_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/color-components-imagej154r.json)
  add_executable(imagejxx_color_stack_tests tests/native/test_color_stack_command.cpp)
  target_link_libraries(imagejxx_color_stack_tests PRIVATE imagejxx::core)
  add_test(NAME native_color_stack COMMAND imagejxx_color_stack_tests)
  add_executable(imagejxx_rgb_dataset_tests tests/native/test_rgb_dataset.cpp)
  target_link_libraries(imagejxx_rgb_dataset_tests PRIVATE imagejxx::core)
  target_include_directories(imagejxx_rgb_dataset_tests SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/include)
  add_test(NAME native_rgb_dataset COMMAND imagejxx_rgb_dataset_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rgb-conversion-imagej154r.json)
endif()

if(IMAGEJXX_BUILD_CPP_UI OR IMAGEJXX_BUILD_PYTHON_UI)
  find_package(glfw3 3.4.0 EXACT CONFIG REQUIRED)
  find_package(nlohmann_json 3.12.0 EXACT CONFIG REQUIRED)
  find_package(OpenGL REQUIRED)
  include(cmake/ImGui.cmake)
  find_package(ZLIB 1.3.1 EXACT REQUIRED)
  find_package(PNG 1.6.50 EXACT REQUIRED)
  find_path(IMAGEJXX_STB_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)
  find_package(TIFF 4.7.1 EXACT QUIET)
  add_library(imagejxx_desktop STATIC src/desktop/util.cpp src/desktop/document.cpp src/desktop/session.cpp src/desktop/metadata.cpp
    src/desktop/commands.cpp src/desktop/io.cpp src/desktop/rendering.cpp
    src/desktop/export.cpp src/desktop/window.cpp src/desktop/orthogonal.cpp src/desktop/smoke.cpp
    src/desktop/binary_iterations_consumer.cpp
    src/desktop/binary_pad_edges_consumer.cpp
    src/desktop/gaussian_host_consumer.cpp
    src/desktop/gaussian_cancellation_consumer.cpp
    src/desktop/gaussian_resource_consumer.cpp
    src/desktop/gaussian_ui_roi_consumer.cpp
    src/desktop/gaussian_ui_controls_consumer.cpp
    src/desktop/gaussian_ui_replay_consumer.cpp
    src/desktop/gaussian_ui_sigma_consumer.cpp
    src/desktop/gaussian_ui_preview_consumer.cpp
    src/desktop/gaussian_ui_stack_consumer.cpp
    src/desktop/gaussian_ui_jobcancel_consumer.cpp
    src/desktop/gaussian_ui_nan_failure_consumer.cpp
    src/desktop/xyz_numeric_zoom_consumer.cpp
    src/desktop/headless_session_consumer.cpp
    src/desktop/scientific_session_ui_probe.cpp
    src/desktop/scientific_navigation_ui_probe.cpp
    src/desktop/scientific_operation_targets_ui_probe.cpp
    src/desktop/scientific_slicing_ui_probe.cpp
    src/desktop/scientific_orientation_ui_probe.cpp
    src/desktop/roi_boolean_ui_probe.cpp
    src/desktop/axis_plane_ui_probe.cpp
    src/desktop/axis_range_ui_probe.cpp
    src/desktop/axis_range_orientation_ui_probe.cpp
    src/desktop/axis_range_slicer_resize_ui_probe.cpp
    src/desktop/axis_range_spacing_ui_probe.cpp
    src/desktop/orthogonal_keyboard.cpp
    src/desktop/settings_io.cpp src/desktop/settings_panel.cpp src/desktop/startup_panel.cpp src/desktop/jobs_panel.cpp src/desktop/selection_panel.cpp
    src/desktop/shortcut_input.cpp src/desktop/shortcut_editor.cpp
    src/desktop/shortcuts_ui_probe.cpp src/desktop/shortcut_policy_observations.cpp
    src/desktop/history_settings_ui_probe.cpp
    src/desktop/startup_ui_probe.cpp
    src/desktop/saved_roi_edit.cpp
    src/desktop/saved_roi_geometry_ui_probe.cpp
    src/desktop/saved_roi_geometry_dependencies.cpp
    src/desktop/gaussian_panel.cpp src/desktop/rank_panel.cpp src/desktop/operation_target_panel.cpp src/desktop/gaussian_jobs.cpp src/desktop/threshold_panel.cpp src/desktop/manual_threshold_ui_probe.cpp src/desktop/z_subset_ui_probe.cpp src/desktop/axis_spacing_ui_probe.cpp src/desktop/slicer_resize_ui_probe.cpp src/desktop/planar_annotation_panel.cpp src/desktop/planar_annotation_ui_probe.cpp src/desktop/planar_annotation_rebind.cpp src/desktop/planar_annotation_matrix.cpp
    src/desktop/native_workflow_batch_ui_probe.cpp
    src/desktop/native_workflow_ui_probe.cpp
    src/desktop/native_workflow_panel.cpp
    src/desktop/native_workflow_parameters.cpp
    src/desktop/native_workflow_viewport.cpp
    src/desktop/roi_import_panel.cpp src/desktop/roi_import_ui_probe.cpp
    src/desktop/roi_import_limits.cpp
    src/desktop/roi_import_limits_ui_probe.cpp
    src/desktop/roi_bare_edit_ui_probe.cpp
    src/desktop/roi_mixed_import_ui_probe.cpp
    src/desktop/roi_mixed_export_ui_probe.cpp
    src/desktop/roi_export_panel.cpp
    src/desktop/auto_threshold_ui_probe.cpp
    src/desktop/movie_export_panel.cpp
    src/desktop/movie_export_ui_probe.cpp
    src/desktop/roi_mixed_export_panel.cpp
    src/desktop/roi_export_common.cpp
    src/desktop/roi_export_ui_probe.cpp
    src/desktop/display_annotation_renderer.cpp src/desktop/display_annotation_panel.cpp
    src/desktop/display_annotation_ui_probe.cpp
    src/desktop/roi_zip_export_ui_probe.cpp
    src/desktop/roi_import_replacement_editor.cpp
    src/desktop/roi_display_replacement_editor.cpp
    src/desktop/roi_import_replacement_ui_probe.cpp
    src/desktop/shadow_demo_ui_probe.cpp
    src/desktop/animation_ui_probe.cpp
    src/desktop/shadow_demo.cpp
    src/desktop/shadow_ui_probe.cpp
    src/desktop/saved_roi_budget_ui_probe.cpp
    src/desktop/rounded_legacy_ui_probe.cpp
    src/desktop/roi_import_rebind_ui_probe.cpp
    src/desktop/roi_import_jobs_ui_probe.cpp
    src/desktop/roi_import_browse_ui_probe.cpp
    src/desktop/roi_import_queue_ui_probe.cpp
    src/desktop/roi_import_preview.cpp
    src/desktop/channel_display_panel.cpp src/desktop/channel_display_sources.cpp
    src/desktop/channel_display_ui_probe.cpp
    src/desktop/channel_display_projection_ui_probe.cpp
    src/desktop/auto_contrast_ui_probe.cpp
    src/desktop/selection_creation_ui_probe.cpp
    src/desktop/scientific_roi_manager_ui_probe.cpp
    src/desktop/ieee_viewing_ui_probe.cpp src/desktop/gaussian_replay_policy.cpp src/desktop/rank_ui_probe.cpp
    src/desktop/rank_runtime_probe.cpp)
  target_sources(imagejxx_desktop PRIVATE src/desktop/indexed_color_panel.cpp src/desktop/color_stack_panel.cpp src/desktop/rgb_dataset_panel.cpp src/desktop/rgb_dataset_ui_probe.cpp)
  set_target_properties(imagejxx_desktop PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(imagejxx_desktop PUBLIC imagejxx::core nlohmann_json::nlohmann_json imgui::imgui
    PRIVATE glfw OpenGL::GL ZLIB::ZLIB PNG::PNG)
  target_include_directories(imagejxx_desktop PUBLIC src/desktop PRIVATE ${IMAGEJXX_STB_INCLUDE_DIR})
  target_sources(imagejxx_desktop PRIVATE src/desktop/scientific_io_ui_probe.cpp)
  if(IMAGEJXX_BUILD_IO)
    target_sources(imagejxx_desktop PRIVATE src/desktop/scientific_io_panel.cpp)
    target_link_libraries(imagejxx_desktop PRIVATE imagejxx::io)
    target_compile_definitions(imagejxx_desktop PRIVATE IMAGEJXX_HAS_SCIENTIFIC_IO=1)
    if(BUILD_TESTING)
      add_executable(imagejxx_startup_tiff_tests tests/native/test_startup_tiff.cpp)
      add_executable(imagejxx_launch_append_tests tests/native/test_launch_append.cpp)
      target_link_libraries(imagejxx_launch_append_tests PRIVATE imagejxx_desktop)
      add_test(NAME desktop_launch_append COMMAND imagejxx_launch_append_tests "${CMAKE_CURRENT_BINARY_DIR}/launch-append-artifacts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u16/little.tif")
      set_tests_properties(desktop_launch_append PROPERTIES TIMEOUT 60)
      target_link_libraries(imagejxx_startup_tiff_tests PRIVATE imagejxx_desktop)
      add_test(NAME desktop_startup_tiff COMMAND imagejxx_startup_tiff_tests "${CMAKE_CURRENT_BINARY_DIR}/startup-tiff-artifacts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u16/little.tif")
      set_tests_properties(desktop_startup_tiff PROPERTIES TIMEOUT 60)
      add_executable(imagejxx_scientific_io_panel_tests tests/native/test_scientific_io_panel.cpp)
      add_executable(imagejxx_legacy_tiff_tests tests/native/test_legacy_tiff_io.cpp)
      target_link_libraries(imagejxx_legacy_tiff_tests PRIVATE imagejxx_desktop)
      add_test(NAME desktop_legacy_tiff COMMAND imagejxx_legacy_tiff_tests "${CMAKE_CURRENT_BINARY_DIR}/legacy-tiff-artifacts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u16/little.tif")
      target_link_libraries(imagejxx_scientific_io_panel_tests PRIVATE imagejxx_desktop)
      add_test(NAME desktop_scientific_io_panel COMMAND imagejxx_scientific_io_panel_tests "${CMAKE_CURRENT_BINARY_DIR}/scientific-io-panel-fixtures" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/scientific-tiff-u16")
    endif()
  endif()
  if(TIFF_FOUND)
    target_link_libraries(imagejxx_desktop PRIVATE TIFF::TIFF)
    target_compile_definitions(imagejxx_desktop PRIVATE IMAGEJXX_HAS_TIFF=1)
  endif()
  find_package(Threads REQUIRED)
  target_link_libraries(imagejxx_desktop PRIVATE Threads::Threads)
  if(WIN32)
    target_link_libraries(imagejxx_desktop PRIVATE comdlg32 gdi32)
  endif()
  if(BUILD_TESTING)
    add_executable(imagejxx_startup_window_tests tests/native/test_startup_window.cpp)
    target_link_libraries(imagejxx_startup_window_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS SU01 SU02 SU03 SU04 SU05 SU06 SU07 SU08 SU09 SU10)
      add_test(NAME desktop_startup_${group} COMMAND ${CMAKE_COMMAND}
        -DTEST_EXECUTABLE=$<TARGET_FILE:imagejxx_startup_window_tests>
        -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/startup-${group}
        -DTEST_ARGUMENT=${group}
        -DTEST_TIMEOUT_SECONDS=30
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOwnedFixtureTest.cmake)
      set_tests_properties(desktop_startup_${group} PROPERTIES TIMEOUT 35)
    endforeach()
    add_executable(imagejxx_rank_panel_tests
      tests/native/test_rank_panel.cpp src/desktop/rank_panel.cpp
      src/desktop/operation_target_panel.cpp src/desktop/metadata.cpp)
    target_link_libraries(imagejxx_rank_panel_tests PRIVATE
      imagejxx::core imgui::imgui nlohmann_json::nlohmann_json)
    add_executable(test_shortcut_window tests/native/test_shortcut_window.cpp)
    target_link_libraries(test_shortcut_window PRIVATE imagejxx_desktop)
    add_test(NAME desktop_shortcut_window COMMAND ${CMAKE_COMMAND}
      -DTEST_EXECUTABLE=$<TARGET_FILE:test_shortcut_window>
      -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/shortcut-window
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOwnedFixtureTest.cmake)
    set_tests_properties(desktop_shortcut_window PROPERTIES TIMEOUT 30)
    add_executable(test_shortcuts_host tests/native/test_shortcuts_host.cpp)
    target_link_libraries(test_shortcuts_host PRIVATE imagejxx_desktop)
    add_test(NAME desktop_shortcuts_host COMMAND ${CMAKE_COMMAND}
      -DTEST_EXECUTABLE=$<TARGET_FILE:test_shortcuts_host>
      -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/shortcuts-host
      -DTEST_TIMEOUT_SECONDS=60
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOwnedFixtureTest.cmake)
    set_tests_properties(desktop_shortcuts_host PROPERTIES TIMEOUT 65)
    add_test(NAME desktop_rank_panel COMMAND imagejxx_rank_panel_tests)
    set_tests_properties(desktop_rank_panel PROPERTIES TIMEOUT 60)
    add_executable(imagejxx_rank_host_tests tests/native/test_rank_host.cpp)
    add_executable(test_rounded_saved_resources_host tests/native/test_rounded_saved_resources_host.cpp)
    target_include_directories(test_rounded_saved_resources_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop)
    target_link_libraries(test_rounded_saved_resources_host PRIVATE imagejxx_desktop)
    add_test(NAME desktop_rounded_saved_resources COMMAND test_rounded_saved_resources_host)
    add_executable(test_ieee_saved_geometry_host tests/native/test_ieee_saved_geometry_host.cpp)
    target_include_directories(test_ieee_saved_geometry_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop)
    target_link_libraries(test_ieee_saved_geometry_host PRIVATE imagejxx_desktop)
    add_test(NAME desktop_ieee_saved_geometry COMMAND test_ieee_saved_geometry_host)
    add_executable(test_saved_roi_geometry_host tests/native/test_saved_roi_geometry_host.cpp)
    add_executable(test_planar_annotation_host tests/native/test_planar_annotation_host.cpp)
    target_link_libraries(test_planar_annotation_host PRIVATE imagejxx_desktop)
    add_test(NAME desktop_planar_annotation_host COMMAND test_planar_annotation_host)
    target_link_libraries(test_saved_roi_geometry_host PRIVATE imagejxx_desktop)
    add_test(NAME desktop_saved_roi_geometry_host COMMAND test_saved_roi_geometry_host)
    set_tests_properties(desktop_saved_roi_geometry_host PROPERTIES TIMEOUT 60)
    target_link_libraries(imagejxx_rank_host_tests PRIVATE imagejxx_desktop)
    if(MSVC)
      target_compile_options(imagejxx_rank_host_tests PRIVATE /fp:strict)
    else()
      target_compile_options(imagejxx_rank_host_tests PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
    # The bounded qualification runner supplies a fresh scratch directory to
    # this public-host suite, preserving NativeV3 files and failed attempts.
    add_executable(imagejxx_desktop_resource_admission_tests
      tests/native/test_desktop_resource_admission.cpp)
    target_link_libraries(imagejxx_desktop_resource_admission_tests PRIVATE imagejxx_desktop)
    if(MSVC)
      target_compile_options(imagejxx_desktop_resource_admission_tests PRIVATE /fp:strict)
    else()
      target_compile_options(imagejxx_desktop_resource_admission_tests PRIVATE -fno-fast-math -ffp-contract=off)
    endif()
    # The lifetime groups also require a fresh, caller-owned scratch directory.
    add_executable(imagejxx_selection_creation_host_tests tests/native/test_selection_creation_host.cpp)
    target_link_libraries(imagejxx_selection_creation_host_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS publication noop admission recording replay sessions guards busy)
      add_test(NAME desktop_selection_creation_${group}
        COMMAND imagejxx_selection_creation_host_tests ${CMAKE_CURRENT_BINARY_DIR}/p5l/n/${group} ${group})
      set_tests_properties(desktop_selection_creation_${group} PROPERTIES TIMEOUT 30)
    endforeach()
    add_executable(imagejxx_ieee_desktop_display_tests tests/native/test_ieee_desktop_display.cpp)
    target_link_libraries(imagejxx_ieee_desktop_display_tests PRIVATE imagejxx_desktop)
    target_include_directories(imagejxx_ieee_desktop_display_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/native)
    add_test(NAME desktop_ieee_display COMMAND imagejxx_ieee_desktop_display_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ieee-display-imagej154r.json)
    set_tests_properties(desktop_ieee_display PROPERTIES TIMEOUT 30)
    add_executable(imagejxx_channel_display_host_tests tests/native/test_channel_display_host.cpp)
    target_link_libraries(imagejxx_channel_display_host_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS admission rendering lifecycle records)
      add_test(NAME desktop_channel_display_${group}
        COMMAND imagejxx_channel_display_host_tests ${CMAKE_CURRENT_BINARY_DIR}/p6c/n/${group} ${group})
      set_tests_properties(desktop_channel_display_${group} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(imagejxx_channel_display_projection_host_tests tests/native/test_channel_display_projection_host.cpp)
    target_link_libraries(imagejxx_channel_display_projection_host_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS rendering panel records)
      add_test(NAME desktop_channel_display_projection_${group}
        COMMAND imagejxx_channel_display_projection_host_tests ${CMAKE_CURRENT_BINARY_DIR}/p6c2/n/${group} ${group})
      set_tests_properties(desktop_channel_display_projection_${group} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(imagejxx_scientific_slicing_tests tests/native/test_scientific_slicing.cpp)
    target_link_libraries(imagejxx_scientific_slicing_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS mapping refusal lifecycle record_replay)
      add_test(NAME desktop_scientific_slicing_${group}
        COMMAND imagejxx_scientific_slicing_tests ${CMAKE_CURRENT_BINARY_DIR}/p6e/n/${group} ${group})
      set_tests_properties(desktop_scientific_slicing_${group} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(imagejxx_scientific_axis_orientation_window_tests
      tests/native/test_scientific_axis_orientation_window.cpp)
    target_link_libraries(imagejxx_scientific_axis_orientation_window_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS
        H01.scientific_admission_and_captured_position
        H02.dialog_defaults_metadata_readiness_and_cancel
        H03.common_staged_publication_and_effective_cap
        H04.strict_replay_explicit_rebind_recording
        H05.owner_busy_preview_lifecycle
        H06.eager_and_v1_plane_range_compatibility)
      add_test(NAME desktop_scientific_axis_orientation_${group}
        COMMAND imagejxx_scientific_axis_orientation_window_tests
          ${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/scientific-orientation/${group} ${group})
      set_tests_properties(desktop_scientific_axis_orientation_${group} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(imagejxx_scientific_operation_targets_tests tests/native/test_scientific_operation_targets.cpp)
    target_link_libraries(imagejxx_scientific_operation_targets_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS provenance refusal admission lifecycle noop)
      add_test(NAME desktop_scientific_operation_targets_${group}
        COMMAND imagejxx_scientific_operation_targets_tests ${CMAKE_CURRENT_BINARY_DIR}/p6d1/n/${group} ${group})
      set_tests_properties(desktop_scientific_operation_targets_${group} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(imagejxx_scientific_navigation_tests tests/native/test_scientific_navigation.cpp)
    target_link_libraries(imagejxx_scientific_navigation_tests PRIVATE imagejxx_desktop)
    foreach(group IN ITEMS scalar samples singletons guards rois)
      add_test(NAME desktop_scientific_navigation_${group}
        COMMAND imagejxx_scientific_navigation_tests ${CMAKE_CURRENT_BINARY_DIR}/p6b/n/${group} ${group})
      set_tests_properties(desktop_scientific_navigation_${group} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(imagejxx_manual_threshold_persistence_tests tests/native/test_manual_threshold_persistence.cpp)
    target_link_libraries(imagejxx_manual_threshold_persistence_tests PRIVATE imagejxx_desktop)
    add_test(NAME desktop_manual_threshold_persistence COMMAND imagejxx_manual_threshold_persistence_tests ${CMAKE_CURRENT_BINARY_DIR}/threshold-persistence-tests)
    set_tests_properties(desktop_manual_threshold_persistence PROPERTIES TIMEOUT 30)
    add_executable(imagejxx_desktop_tests tests/native/test_desktop.cpp)
    target_link_libraries(imagejxx_desktop_tests PRIVATE imagejxx_desktop ZLIB::ZLIB)
    add_test(NAME desktop_regressions COMMAND imagejxx_desktop_tests ${CMAKE_CURRENT_BINARY_DIR}/desktop-tests)
    add_executable(imagejxx_publication_tests tests/native/test_publication.cpp)
    target_link_libraries(imagejxx_publication_tests PRIVATE imagejxx_desktop)
    add_test(NAME desktop_publication COMMAND ${CMAKE_COMMAND}
      -DTEST_EXECUTABLE=$<TARGET_FILE:imagejxx_publication_tests>
      -DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/Testing/publication
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOwnedFixtureTest.cmake)
    add_executable(imagejxx_gaussian_replay_policy_tests tests/native/test_gaussian_replay_policy.cpp)
    target_link_libraries(imagejxx_gaussian_replay_policy_tests PRIVATE imagejxx_desktop)
    add_test(NAME desktop_gaussian_replay_policy COMMAND imagejxx_gaussian_replay_policy_tests)
    add_executable(imagejxx_nan_document_display_tests tests/native/test_nan_document_display.cpp)
    target_link_libraries(imagejxx_nan_document_display_tests PRIVATE imagejxx_desktop ZLIB::ZLIB)
    file(MAKE_DIRECTORY "${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}")
    add_test(NAME desktop_nan_document_display COMMAND imagejxx_nan_document_display_tests ${IMAGEJXX_SESSION_TEST_ARTIFACT_DIR}/nan-document-display-tests)
    set_tests_properties(desktop_nan_document_display PROPERTIES TIMEOUT 30)
    add_executable(imagejxx_nan_window_tests tests/native/test_nan_window.cpp)
    target_link_libraries(imagejxx_nan_window_tests PRIVATE imagejxx_desktop)
    add_test(NAME desktop_nan_window COMMAND imagejxx_nan_window_tests ${CMAKE_CURRENT_BINARY_DIR}/nan-window-tests)
    set_tests_properties(desktop_nan_window PROPERTIES TIMEOUT 30)
  endif()
  if(IMAGEJXX_BUILD_CPP_UI)
    add_executable(imagejxx_app src/desktop/main.cpp)
    set_target_properties(imagejxx_app PROPERTIES OUTPUT_NAME imagejxx)
    target_link_libraries(imagejxx_app PRIVATE imagejxx_desktop)
    install(TARGETS imagejxx_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT desktop)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/imagejxx/licenses COMPONENT desktop)
    install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/imagejxx/licenses COMPONENT desktop)
    if(BUILD_TESTING)
      add_test(NAME desktop_selection_creation_ui
        COMMAND imagejxx_app --selection-creation-ui-test ${CMAKE_CURRENT_BINARY_DIR}/p5l/u/run)
      set_tests_properties(desktop_selection_creation_ui PROPERTIES TIMEOUT 60)
      foreach(case_id IN ITEMS N01 N02 N03 N04 N05 N06)
        add_test(NAME desktop_scientific_navigation_ui_${case_id}
          COMMAND imagejxx_app --scientific-navigation-ui-case ${case_id}
            ${CMAKE_CURRENT_BINARY_DIR}/p6b/u/${case_id}/run)
        set_tests_properties(desktop_scientific_navigation_ui_${case_id} PROPERTIES TIMEOUT 60)
      endforeach()
      add_test(NAME desktop_smoke COMMAND imagejxx_app --smoke-test --smoke-dir ${CMAKE_CURRENT_BINARY_DIR}/ui-smoke)
      add_test(NAME desktop_binary_iterations COMMAND imagejxx_app
        --binary-iterations-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/binary-iterations-consumer)
      add_test(NAME desktop_binary_pad_edges COMMAND imagejxx_app
        --binary-pad-edges-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/binary-pad-edges-consumer)
      add_test(NAME desktop_orthogonal_keyboard COMMAND imagejxx_app
        --orthogonal-keyboard-test --keyboard-dir ${CMAKE_CURRENT_BINARY_DIR}/orthogonal-keyboard)
      add_test(NAME desktop_gaussian_host COMMAND imagejxx_app
        --gaussian-host-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/gaussian-host-consumer)
      set_tests_properties(desktop_gaussian_host PROPERTIES TIMEOUT 60)
      add_test(NAME desktop_gaussian_cancellation COMMAND imagejxx_app
        --gaussian-cancellation-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/gaussian-cancellation-consumer)
      set_tests_properties(desktop_gaussian_cancellation PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_resource COMMAND imagejxx_app
        --gaussian-resource-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/gaussian-resource-consumer)
      set_tests_properties(desktop_gaussian_resource PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_roi COMMAND imagejxx_app
        --gaussian-roi-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/gaussian-roi-consumer)
      set_tests_properties(desktop_gaussian_roi PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_controls COMMAND imagejxx_app
        --gaussian-controls-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/gaussian-controls-consumer)
      set_tests_properties(desktop_gaussian_controls PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_replay COMMAND imagejxx_app
        --gaussian-replay-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/gaussian-replay-consumer)
      set_tests_properties(desktop_gaussian_replay PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_sigma COMMAND imagejxx_app
        --gaussian-sigma-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/gaussian-sigma)
      set_tests_properties(desktop_gaussian_sigma PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_preview COMMAND imagejxx_app
        --gaussian-preview-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/gaussian-preview)
      set_tests_properties(desktop_gaussian_preview PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_stack COMMAND imagejxx_app
        --gaussian-stack-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/gaussian-stack)
      set_tests_properties(desktop_gaussian_stack PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_jobcancel COMMAND imagejxx_app
        --gaussian-jobcancel-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/gaussian-jobcancel)
      set_tests_properties(desktop_gaussian_jobcancel PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_gaussian_nonfinite COMMAND imagejxx_app
        --gaussian-nonfinite-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/gaussian-nonfinite)
      set_tests_properties(desktop_gaussian_nonfinite PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_xyz_numeric_zoom COMMAND imagejxx_app
        --xyz-numeric-zoom-consumer-test --numeric-zoom-dir ${CMAKE_CURRENT_BINARY_DIR}/nz)
      set_tests_properties(desktop_xyz_numeric_zoom PROPERTIES TIMEOUT 120)
      add_test(NAME desktop_headless_session COMMAND imagejxx_app
        --headless-session-consumer-test --consumer-dir ${CMAKE_CURRENT_BINARY_DIR}/headless-session-consumer)
      set_tests_properties(desktop_headless_session PROPERTIES TIMEOUT 120)
    endif()
  endif()
  if(IMAGEJXX_BUILD_PYTHON_UI)
    if(NOT IMAGEJXX_BUILD_PYTHON)
      message(FATAL_ERROR "IMAGEJXX_BUILD_PYTHON_UI requires IMAGEJXX_BUILD_PYTHON")
    endif()
    pybind11_add_module(_desktop src/desktop/python.cpp)
    target_link_libraries(_desktop PRIVATE imagejxx_desktop)
    imagejxx_bind_static_codecs(_desktop)
    install(TARGETS _desktop LIBRARY DESTINATION imagejxx RUNTIME DESTINATION imagejxx)
  endif()
  # Runtime libraries are copied from the reviewed Windows dependency prefix.
  # The processing-only build does not resolve or install desktop dependencies.
  if(WIN32)
    get_target_property(IMAGEJXX_ZLIB_LOCATION ZLIB::ZLIB IMPORTED_LOCATION_RELEASE)
    if(NOT IMAGEJXX_ZLIB_LOCATION)
      get_target_property(IMAGEJXX_ZLIB_LOCATION ZLIB::ZLIB IMPORTED_LOCATION)
    endif()
    get_filename_component(IMAGEJXX_DEP_PREFIX "${IMAGEJXX_ZLIB_LOCATION}/../.." ABSOLUTE)
    set(IMAGEJXX_RUNTIME_DLLS zlib1 libpng16)
    set(IMAGEJXX_TIFF_RUNTIME_FILES "")
    set(IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT "")
    if(TIFF_FOUND)
      include(cmake/ReviewedTiffRuntime.cmake)
      imagejxx_checked_tiff_runtime(IMAGEJXX_TIFF_RUNTIME_FILES)
      if(NOT TARGET imagejxx_verify_tiff_runtime)
        add_custom_target(imagejxx_verify_tiff_runtime
          COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}"
          VERBATIM)
      endif()
      add_dependencies(imagejxx_desktop imagejxx_verify_tiff_runtime)
      # The checked TIFF closure supplies its exact zlib runtime too.
      set(IMAGEJXX_RUNTIME_DLLS libpng16)
    endif()
    set(IMAGEJXX_RUNTIME_FILES $<TARGET_FILE:glfw> ${IMAGEJXX_TIFF_RUNTIME_FILES})
    foreach(dll IN LISTS IMAGEJXX_RUNTIME_DLLS)
      if(EXISTS "${IMAGEJXX_DEP_PREFIX}/bin/${dll}.dll")
        list(APPEND IMAGEJXX_RUNTIME_FILES "${IMAGEJXX_DEP_PREFIX}/bin/${dll}.dll")
      endif()
    endforeach()
    if(TARGET _desktop)
      if(IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT)
        install(SCRIPT "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}")
      endif()
      install(FILES ${IMAGEJXX_RUNTIME_FILES} DESTINATION imagejxx)
    endif()
    foreach(executable IN ITEMS imagejxx_app imagejxx_desktop_tests imagejxx_publication_tests imagejxx_scientific_io_panel_tests imagejxx_legacy_tiff_tests imagejxx_startup_tiff_tests imagejxx_launch_append_tests
                                imagejxx_scientific_navigation_tests imagejxx_scientific_operation_targets_tests
                                imagejxx_scientific_slicing_tests imagejxx_scientific_axis_orientation_window_tests)
      if(TARGET ${executable})
        if(IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT)
          add_custom_command(TARGET ${executable} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -P "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}")
        endif()
        add_custom_command(TARGET ${executable} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${IMAGEJXX_RUNTIME_FILES} $<TARGET_FILE_DIR:${executable}> COMMAND_EXPAND_LISTS)
      endif()
    endforeach()
    if(TARGET imagejxx_app)
      if(IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT)
        install(SCRIPT "${IMAGEJXX_TIFF_RUNTIME_VERIFY_SCRIPT}" COMPONENT desktop)
      endif()
      install(FILES ${IMAGEJXX_RUNTIME_FILES} DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT desktop)
    endif()
  endif()
  if(MSVC)
    target_compile_definitions(imagejxx_desktop PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(imagejxx_desktop PRIVATE /W4 /permissive- /utf-8)
    if(TARGET imagejxx_app)
      target_compile_options(imagejxx_app PRIVATE /W4 /permissive- /utf-8)
    endif()
  endif()
endif()

