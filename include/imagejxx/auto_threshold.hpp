#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace imagejxx {
// Missing ImageJ/Fiji histogram methods. Counts are never modified. A histogram
// has 2..65536 bins, positive total <= INT32_MAX; empty histograms are refused.
// Bilevel and >256-bin occupied-range bracketing match ImageJ 1.54r.
// Only default, ij_isodata, huang, huang2, renyi_entropy and shanbhag.
int auto_threshold_histogram_imagej(const std::vector<std::uint64_t> &counts,
                                    const std::string &method);
bool is_imagej_threshold_method(const std::string &method);
} // namespace imagejxx
