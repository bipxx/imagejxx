#pragma once
#include <imagejxx/image.hpp>
namespace imagejxx { class ResolvedTarget; }
namespace imagejxx::detail {
std::size_t pooled_threshold_working_bytes(const ResolvedTarget &, int bins);
int imagej_histogram_threshold(const std::vector<std::uint64_t> &,
                              const std::string &, bool, bool, bool progress);
double threshold_from_histogram(Histogram, const std::string &method,
                               bool ignore_black, bool ignore_white);
}
