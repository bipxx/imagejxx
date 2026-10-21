# One pinned ImGui build supplies both hosts and all internal consumers.
# WCHAR32 is an ABI choice: defining it only in application sources corrupts
# the 16-bit SDK layout and does not repair supplementary-plane input.
set(_imgui_root "${CMAKE_CURRENT_LIST_DIR}/../third_party/imgui")
set(_imgui_members LICENSE.txt imconfig.h imgui.h imgui_internal.h
  imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_demo.cpp
  imstb_rectpack.h imstb_textedit.h imstb_truetype.h
  misc/cpp/imgui_stdlib.h misc/cpp/imgui_stdlib.cpp
  backends/imgui_impl_glfw.h backends/imgui_impl_glfw.cpp
  backends/imgui_impl_opengl3.h backends/imgui_impl_opengl3.cpp
  backends/imgui_impl_opengl3_loader.h)
file(STRINGS "${_imgui_root}/SHA256SUMS" _imgui_pins)
set(_imgui_seen)
foreach(_row IN LISTS _imgui_pins)
  string(LENGTH "${_row}" _length)
  if(_length LESS 67)
    message(FATAL_ERROR "Invalid pinned ImGui checksum row")
  endif()
  string(SUBSTRING "${_row}" 0 64 _expected)
  string(SUBSTRING "${_row}" 64 2 _separator)
  string(SUBSTRING "${_row}" 66 -1 _member)
  if(NOT _expected MATCHES "^[0-9a-f]+$" OR NOT _separator STREQUAL "  "
      OR NOT _member IN_LIST _imgui_members OR _member IN_LIST _imgui_seen)
    message(FATAL_ERROR "Unreviewed or duplicate pinned ImGui checksum row")
  endif()
  file(SHA256 "${_imgui_root}/${_member}" _actual)
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "Pinned ImGui input changed: ${_member}")
  endif()
  list(APPEND _imgui_seen "${_member}")
endforeach()
list(LENGTH _imgui_members _expected_count)
list(LENGTH _imgui_seen _actual_count)
if(NOT _actual_count EQUAL _expected_count)
  message(FATAL_ERROR "Incomplete pinned ImGui source manifest")
endif()
if(TARGET imgui::imgui)
  message(FATAL_ERROR "Desktop requires its consistently configured pinned WCHAR32 ImGui target")
endif()
add_library(imagejxx_imgui STATIC
  "${_imgui_root}/imgui.cpp" "${_imgui_root}/imgui_draw.cpp"
  "${_imgui_root}/imgui_tables.cpp" "${_imgui_root}/imgui_widgets.cpp"
  "${_imgui_root}/imgui_demo.cpp" "${_imgui_root}/misc/cpp/imgui_stdlib.cpp"
  "${_imgui_root}/backends/imgui_impl_glfw.cpp"
  "${_imgui_root}/backends/imgui_impl_opengl3.cpp")
add_library(imgui::imgui ALIAS imagejxx_imgui)
set_target_properties(imagejxx_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(imagejxx_imgui PUBLIC IMGUI_USE_WCHAR32)
target_include_directories(imagejxx_imgui BEFORE PUBLIC
  "${_imgui_root}" "${_imgui_root}/misc/cpp" "${_imgui_root}/backends")
target_compile_features(imagejxx_imgui PUBLIC cxx_std_11)
target_link_libraries(imagejxx_imgui PUBLIC glfw OpenGL::GL PRIVATE ${CMAKE_DL_LIBS})
if(MSVC)
  target_compile_options(imagejxx_imgui PRIVATE /utf-8)
endif()
