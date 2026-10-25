# Private, source-pinned binary64 backend. No Boost target or include directory
# is exported: product owners embed the object files in imagejxx_core explicitly.
include_guard(GLOBAL)

function(_imagejxx_configure_charconv target)
  if(NOT TARGET "${target}")
    message(FATAL_ERROR "Private Charconv consumer does not exist: ${target}")
  endif()
  set(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/boost_charconv")
  target_include_directories("${target}" SYSTEM PRIVATE
    "${_root}/charconv/include"
    "${_root}/config/include"
    "${_root}/assert/include"
    "${_root}/core/include"
    "${_root}/throw_exception/include")
  target_compile_features("${target}" PRIVATE cxx_std_17)
  target_compile_definitions("${target}" PRIVATE
    BOOST_CHARCONV_STATIC_LINK BOOST_CHARCONV_NO_LIB)
  # Upstream tests this feature with #ifdef. It must be absent, not set to 0.
  # No quadmath library or upstream feature-detection project is linked here.
  if(MSVC)
    target_compile_options("${target}" PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:/UBOOST_CHARCONV_HAS_QUADMATH>)
  else()
    target_compile_options("${target}" PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-UBOOST_CHARCONV_HAS_QUADMATH>)
  endif()
endfunction()

function(_imagejxx_charconv_strict_fp target)
  # Only the backend objects and their standalone witness receive these flags.
  # Product kernels keep their existing target-specific floating-point policy.
  if(MSVC)
    target_compile_options("${target}" PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/fp:strict>)
  else()
    target_compile_options("${target}" PRIVATE
      $<$<COMPILE_LANGUAGE:CXX>:-fno-fast-math>
      $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)
  endif()
endfunction()

function(imagejxx_require_charconv_objects)
  if(TARGET imagejxx_charconv_objects)
    return()
  endif()
  set(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../third_party/boost_charconv")
  add_library(imagejxx_charconv_objects OBJECT
    "${_root}/charconv/src/from_chars.cpp"
    "${_root}/charconv/src/to_chars.cpp")
  _imagejxx_configure_charconv(imagejxx_charconv_objects)
  _imagejxx_charconv_strict_fp(imagejxx_charconv_objects)
  target_compile_definitions(imagejxx_charconv_objects PRIVATE BOOST_CHARCONV_SOURCE)
  set_target_properties(imagejxx_charconv_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
  # Hidden visibility is a private-object policy, not universal symbol isolation.
endfunction()

function(imagejxx_use_private_charconv target)
  # Deliberately unconditional: the standalone witness also forces this backend
  # on Windows/Linux. Product integration chooses when to call this function.
  _imagejxx_configure_charconv("${target}")
  target_compile_definitions("${target}" PRIVATE IMAGEJXX_PORTABLE_FLOAT_CHARCONV=1)
endfunction()

function(imagejxx_add_charconv_witness)
  if(TARGET imagejxx_floating_charconv_tests)
    return()
  endif()
  imagejxx_require_charconv_objects()
  add_executable(imagejxx_floating_charconv_tests
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tests/native/floating_charconv_compatibility_tests.cpp"
    $<TARGET_OBJECTS:imagejxx_charconv_objects>)
  imagejxx_use_private_charconv(imagejxx_floating_charconv_tests)
  _imagejxx_charconv_strict_fp(imagejxx_floating_charconv_tests)
  target_include_directories(imagejxx_floating_charconv_tests PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/native")
  set_target_properties(imagejxx_floating_charconv_tests PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_definitions(imagejxx_floating_charconv_tests PRIVATE
      IMAGEJXX_CHARCONV_TEST_MSVC_RANGE_BASELINE=1)
  endif()
  if(APPLE)
    # macOS 11 lacks floating std::charconv: this is literal-only support, not
    # original-std differential evidence. The witness reports that limitation.
    target_compile_definitions(imagejxx_floating_charconv_tests PRIVATE
      IMAGEJXX_CHARCONV_TEST_NO_STD_ORACLE=1)
  endif()
  add_test(NAME native_floating_charconv_editor
    COMMAND imagejxx_floating_charconv_tests --required-editor)
  set_tests_properties(native_floating_charconv_editor PROPERTIES TIMEOUT 30)
endfunction()
