#pragma once
#include <imagejxx/target_histogram.hpp>
namespace imagejxx::detail {
// Source-independent validation shared by captures and portable authored intent.
void validate_histogram_command_options(const TargetHistogramOptions &);
}
