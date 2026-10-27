#include "imagej_projection_numeric.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace imagejxx::detail;
using T = ImageJProjectionType;
using M = ImageJProjectionMethod;
struct Case {
  const char* id;
  T type;
  M method;
  bool signed16;
  std::size_t count;
  std::array<std::uint32_t, 5> input;
  T output_type;
  std::uint32_t expected;
};
const Case cases[] = {
#include "imagej_projection_numeric_reference.inc"
};
bool nan(std::uint32_t v) { return (v & 0x7fffffffU) > 0x7f800000U; }
int main() {
  std::size_t nan_equivalent = 0;
  for (const auto& c : cases) {
    const auto original = c.input;
    std::array<std::uint32_t, 5> scratch{};
    const auto result = project_imagej_scalar_ray(c.type, c.method,
        c.input.data(), c.count, c.signed16, scratch.data(), scratch.size());
    const bool both_nan = result.type == T::Float32 &&
        c.output_type == T::Float32 && nan(result.bits) && nan(c.expected);
    if (result.type != c.output_type || (!both_nan && result.bits != c.expected) ||
        c.input != original) {
      std::cerr << c.id << " expected " << std::hex << c.expected
                << " actual " << result.bits << '\n';
      return 1;
    }
    if (both_nan) ++nan_equivalent;
  }
  std::array<std::uint32_t, 5> scratch{9,9,9,9,9};
  const auto unchanged = scratch;
  std::uint32_t input[] = {1,256};
  auto reject = [&](auto&& call) {
    try { call(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid numerical request was accepted");
  };
  reject([&]{project_imagej_scalar_ray(T::UInt8,M::Median,input,2,false,scratch.data(),5);});
  if (scratch != unchanged) throw std::runtime_error("Validation mutated scratch");
  reject([&]{project_imagej_scalar_ray(T::UInt16,M::Mean,nullptr,1,false);});
  reject([&]{project_imagej_scalar_ray(T::UInt16,M::Mean,input,0,false);});
  reject([&]{project_imagej_scalar_ray(T::UInt8,M::Mean,input,1,true);});
  reject([&]{project_imagej_scalar_ray(T::UInt16,M::Median,input,2,false,scratch.data(),1);});
  reject([&]{project_imagej_scalar_ray(T::UInt16,M::Median,input,2,false,input,2);});
  reject([&]{project_imagej_scalar_ray(static_cast<T>(99),M::Mean,input,1,false);});
  reject([&]{project_imagej_scalar_ray(T::UInt16,static_cast<M>(99),input,1,false);});
  const std::uint32_t huge[] = {0x7f7fffffU,0x7f7fffffU};
  const auto overflow = project_imagej_scalar_ray(T::Float32,M::Median,
      huge,2,false,scratch.data(),scratch.size());
  if (overflow.type != T::Float32 || overflow.bits != 0x7f800000U)
    throw std::runtime_error("Even Float32 median must preserve addition overflow");
  std::cout << std::size(cases) << " pinned scalar rays passed; "
            << nan_equivalent << " NaN results checked by class; all other bits exact\n";
}
