# Include after the unchanged retained consumer CMakeLists to add consumer26.
# Source comes from this held packet; only the declared installed SDK is linked.
add_executable(imagejxx_axis_range_reslice_example "${CMAKE_CURRENT_LIST_DIR}/axis_range_reslice.cpp")
target_link_libraries(imagejxx_axis_range_reslice_example PRIVATE imagejxx::core)
target_compile_features(imagejxx_axis_range_reslice_example PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(imagejxx_axis_range_reslice_example PRIVATE /fp:strict)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(imagejxx_axis_range_reslice_example PRIVATE -fno-fast-math -ffp-contract=off)
endif()
if(BUILD_TESTING)
  add_test(NAME imagejxx_axis_range_reslice_example COMMAND imagejxx_axis_range_reslice_example)
  set_tests_properties(imagejxx_axis_range_reslice_example PROPERTIES TIMEOUT 120)
endif()
