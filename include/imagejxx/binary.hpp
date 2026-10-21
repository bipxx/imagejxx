#pragma once
#include <imagejxx/image.hpp>

namespace imagejxx {
struct BinaryMorphologyOptions {
  int iterations = 1, neighbors = 1;
  bool black_background = true, pad_edges = false;
  void validate() const;
};
// Eight-neighbor binary erosion/dilation. Input must be single-sample uint8
// with only 0/255 values. Open/close repeat the first step N times, then the
// second step N times. Pixel foreground is 255 for black_background=true.
Image binary_morphology(const Image &, const std::string &operation,
                        BinaryMorphologyOptions options = {});
} // namespace imagejxx
