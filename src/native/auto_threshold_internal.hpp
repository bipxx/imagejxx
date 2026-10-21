#pragma once
#include <imagejxx/image.hpp>
namespace imagejxx::detail {
int imagej_histogram_threshold(const std::vector<std::uint64_t> &,
                              const std::string &, bool, bool, bool progress);
double threshold_from_histogram(Histogram, const std::string &method,
                               bool ignore_black, bool ignore_white);
}
