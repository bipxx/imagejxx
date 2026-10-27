#pragma once
#include <cstddef>
#include <cstdint>

namespace imagejxx::detail {
enum class ImageJProjectionType { UInt8, UInt16, Float32 };
enum class ImageJProjectionMethod { Mean, Maximum, Minimum, Sum, StdDev, Median };
struct ImageJProjectionSample {
  ImageJProjectionType type;
  std::uint32_t bits;
};
// Streaming state for nonmedian methods; independent of contributor count in
// memory. The checked ray adapter below uses the same arithmetic implementation.
class ImageJProjectionAccumulator {
public:
  ImageJProjectionAccumulator(ImageJProjectionType type,
                             ImageJProjectionMethod method, bool signed16);
  void push(std::uint32_t bits);
  ImageJProjectionSample finish() const;
private:
  ImageJProjectionType type_;
  ImageJProjectionMethod method_;
  bool signed16_;
  float result_ = 0;
  double sum_ = 0, squares_ = 0;
  int count_ = 0, non_nan_count_ = 0;
};
// Private scalar-ray primitive for the pinned ImageJ 1.54r profile. Integer
// samples are unsigned codes; Float32 samples are raw IEEE words. Signed16
// density affects sum and median only. No dataset axes, RGB merge, metadata,
// callbacks or document publication are implied by this numerical primitive.
// Median needs count words of caller-owned, nonoverlapping scratch; all other
// methods need none. Inputs are validated before scratch is written. No heap
// allocation. NaN arithmetic payload/sign is platform dependent; numeric NaN
// equivalence is separate from source-bit transport guarantees.
ImageJProjectionSample project_imagej_scalar_ray(
    ImageJProjectionType type, ImageJProjectionMethod method,
    const std::uint32_t* samples, std::size_t count, bool signed16,
    std::uint32_t* median_scratch = nullptr, std::size_t scratch_count = 0,
    void (*poll)() = nullptr);
} // namespace imagejxx::detail
