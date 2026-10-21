# Keep selected static C dependency definitions local to Linux extension DSOs.
# Otherwise an already loaded Python codec can interpose its incompatible copy.
# Name only codec/windowing archives; keep the C++ core/pybind ABI unchanged.
# GNU ld documents ELF archive symbols selected by --exclude-libs as hidden:
# https://sourceware.org/binutils/docs/ld/Options.html
function(imagejxx_bind_static_codecs target)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(${target} PRIVATE
      "LINKER:--exclude-libs,libtiff.a:libjpeg.a:liblzma.a:libz.a:libpng16.a:libpng.a:libglfw3.a")
  endif()
endfunction()
