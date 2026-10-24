#pragma once
#include <imagejxx/image.hpp>
#include <limits>
namespace imagejxx { class ResolvedTarget; }
namespace imagejxx::detail {
// The finite caller allowance is checked before target-mask dry-run scratch.
std::size_t pooled_threshold_working_bytes(const ResolvedTarget &, int bins,
    bool include_histogram=false,
    std::size_t max_working_bytes=std::numeric_limits<std::size_t>::max());
int imagej_histogram_threshold(const std::vector<std::uint64_t> &,
                              const std::string &, bool, bool, bool progress);
double threshold_from_histogram(Histogram, const std::string &method,
                               bool ignore_black, bool ignore_white);
}
